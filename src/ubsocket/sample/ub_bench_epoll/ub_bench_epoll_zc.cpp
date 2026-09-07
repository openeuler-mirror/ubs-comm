/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * Description: 免拷贝版 UB epoll 性能测试工具
 *
 * 以 ub_bench_epoll.cpp (拷贝版) 为结构基准, 仅数据通路替换为免拷贝:
 *   - buffer 用 ubsocket_iobuf_allocate 分配 (iobuf block), 非 malloc
 *   - 收发接口用 ubsocket_readv/ubsocket_writev, 非 ::recv/::writev
 *   - 显式 ubsocket_init 初始化, 不走 LD_PRELOAD
 *
 * 免拷贝 readv 返回值语义: 返回本次可读字节数, 实际数据落在链接到
 * iov_base 对应 block 之后的 block 链表上 (头块仅作链表锚点), 调用方需
 * 遍历链表获取/回写数据, 读取完成后逐块 DecRef 归还。
 *
 * Usage:
 *   服务端: ub_bench_epoll_zc sr --threads 16 -p 11111 [-i 0.0.0.0] [--size 1024] [--ub]
 *   客户端: ub_bench_epoll_zc pp -i 127.0.0.1 -p 11111 [--threads 1] [--size 1024] \
 *           [--time 10 | --count 1000000] [--qps 0] [--ub]
 */

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include "ubsocket.h"
#include "ubsocket_epoll.h" /* ubsocket_epoll_create1 等 */
#include "ubsocket_sock.h"  /* ubsocket_socket 等 */

#ifndef AF_SMC
#define AF_SMC 43
#endif

#define MAX_EVENTS 64

#define LOG_INFO(fmt, ...)  std::fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   std::fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

/* === 免拷贝数据通路: IobufBlock + 辅助函数 (替代 malloc/free + memcpy) === */

/* Iobuf Block 头布局 (与内部 ock::ubs::Block 一致, sizeof = 32 = IOBUF_DIFF)。
 * 免拷贝接口要求 iov_base 指向 ubsocket_iobuf_allocate 返回的 block data 区,
 * 底层通过 umq_data_to_head(data) 回溯到此结构体头; data 之后的 block 通过
 * u.next 链接, 构成接收到的数据链表。 */
struct IobufBlock {
    std::atomic<int> nshared;
    uint16_t flags;
    uint16_t abi_check;
    uint32_t size;
    uint32_t cap;
    union {
        IobufBlock *next;
        uint64_t data_meta;
    } u;
    char *data;
};
static const size_t IOBUF_DIFF_ZC = sizeof(IobufBlock); /* 32 */

/* 分配一个 iobuf block, 返回 data 区指针 (iov_base 直接用该指针) */
static char *AllocIobuf(uint32_t dataSize) {
    void *raw = ubsocket_iobuf_allocate(IOBUF_DIFF_ZC + dataSize, nullptr);
    if (raw == nullptr) return nullptr;
    IobufBlock *blk = static_cast<IobufBlock *>(raw);
    blk->nshared.store(1, std::memory_order_relaxed);
    blk->flags = 0;
    blk->abi_check = 0;
    blk->size = 0;
    blk->cap = dataSize;
    blk->u.next = nullptr;
    blk->data = static_cast<char *>(raw) + IOBUF_DIFF_ZC;
    return blk->data;
}

/* 释放头块 (从 data 指针回溯到 block 头) */
static void FreeIobuf(char *data) {
    if (data == nullptr) return;
    ubsocket_iobuf_deallocate(static_cast<void *>(data - IOBUF_DIFF_ZC));
}

/* DecRef 一个链表 block, 引用归零时归还 */
static void DecRefIobufBlock(IobufBlock *blk) {
    if (blk->nshared.fetch_sub(1, std::memory_order_release) == 1) {
        ubsocket_iobuf_deallocate(blk);
    }
}

/* 释放挂在头块后面的接收数据链表 (头块本身由调用方持有, 不释放) */
static void ReleaseRxChain(char *headData) {
    IobufBlock *head = reinterpret_cast<IobufBlock *>(headData - IOBUF_DIFF_ZC);
    IobufBlock *blk = head->u.next;
    head->u.next = nullptr;
    while (blk != nullptr) {
        IobufBlock *next = blk->u.next;
        blk->u.next = nullptr;
        DecRefIobufBlock(blk);
        blk = next;
    }
}

/* 遍历接收数据链表, 构造 iovec 数组, 返回累计字节数 (封顶 want)。
 * 免拷贝回显: 直接引用接收链表上的 block, 替代 memcpy */
static size_t BuildChainIovs(char *headData, uint32_t want, std::vector<struct iovec> &iovs) {
    IobufBlock *head = reinterpret_cast<IobufBlock *>(headData - IOBUF_DIFF_ZC);
    IobufBlock *blk = head->u.next;
    size_t total = 0;
    iovs.clear();
    while (blk != nullptr && total < want) {
        size_t take = std::min(static_cast<size_t>(blk->cap), static_cast<size_t>(want - total));
        if (take == 0) break;
        struct iovec iv;
        iv.iov_base = blk->data;
        iv.iov_len = take;
        iovs.push_back(iv);
        total += take;
        blk = blk->u.next;
    }
    return total;
}

/* 单块 iobuf 数据区大小。免拷贝接口要求每个 iov_base 都指向某个块的 data 区, 且 iov_len
 * 不能超过单块容量。单块容量由 UMQ block type 决定: TP=8K(数据区 8160=8192-32), CTP=4K(4064)。 */
static size_t g_iobuf_size = 0;

/* 释放发送块链 (每个 iovec 的 iov_base 是一个独立分配的块) */
static void FreeSendChain(std::vector<struct iovec> &iovs) {
    for (auto &iv : iovs) {
        FreeIobuf(static_cast<char *>(iv.iov_base));
    }
    iovs.clear();
}

/* 探测单块最大可分配字节数。从 8K 往下探测, 命中即得块头+数据总大小, 数据区=total-32 */
static void ProbeIobufSize() {
    for (size_t total = 8192; total >= 2048; total -= 2048) {
        void *raw = ubsocket_iobuf_allocate(total, nullptr);
        if (raw != nullptr) {
            ubsocket_iobuf_deallocate(raw);
            g_iobuf_size = total - IOBUF_DIFF_ZC;
            return;
        }
    }
    g_iobuf_size = 0;
}

/* 分配一条发送块链: 把 totalSize 按单块容量切块, 每块一个 iov。
 * 发送块在 writev 时被 PostSend IncRef, TX CQE 完成 DecRef 回落到初值(块自身计数保持引用存活),
 * 因此发送块可跨迭代复用, 进程退出时再统一 FreeSendChain 归还。 */
static bool AllocSendChain(size_t totalSize, std::vector<struct iovec> &iovs) {
    size_t remaining = totalSize;
    iovs.clear();
    while (remaining > 0) {
        size_t chunkSize = std::min(remaining, g_iobuf_size);
        char *data = AllocIobuf(static_cast<uint32_t>(g_iobuf_size));
        if (data == nullptr) {
            FreeSendChain(iovs);
            return false;
        }
        struct iovec iv;
        iv.iov_base = data;
        iv.iov_len = chunkSize;
        iovs.push_back(iv);
        remaining -= chunkSize;
    }
    return true;
}

/* 根据 off 在 iovs 中找到剩余 iov 的起始索引 (writev 部分写入后, 从下一个 block 续发) */
static int FindStartIdx(const std::vector<struct iovec> &iovs, size_t off) {
    size_t acc = 0;
    for (int i = 0; i < static_cast<int>(iovs.size()); i++) {
        if (acc == off) return i;
        acc += iovs[i].iov_len;
    }
    return static_cast<int>(iovs.size());
}

/* === 以下结构与拷贝版 ub_bench_epoll.cpp 对齐, 仅数据通路替换为免拷贝 === */

struct BenchConfig {
    std::string role;
    std::string ip = "0.0.0.0";
    int port = 11111;
    int threads = 1;
    int size = 1024;
    int trans = AF_SMC;
    double timeSec = 10.0;
    long long count = 0;
    long long qps = 0;
};

static volatile sig_atomic_t g_stop = 0;
static void OnSignal(int) { g_stop = 1; }

static int SetNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (flags & O_NONBLOCK) return 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 打印分位数耗时工具函数
static void PrintLatencyStats(const char *title, std::vector<uint64_t> &latencies) {
    if (latencies.empty()) return;

    std::sort(latencies.begin(), latencies.end());

    uint64_t sumNs = 0;
    for (auto lat : latencies) sumNs += lat;

    double avgUs = (double)sumNs / latencies.size() / 1000.0;
    double minUs = (double)latencies.front() / 1000.0;
    double maxUs = (double)latencies.back() / 1000.0;

    size_t idx_p50 = static_cast<size_t>(latencies.size() * 0.50);
    size_t idx_p90 = static_cast<size_t>(latencies.size() * 0.90);
    size_t idx_p99 = static_cast<size_t>(latencies.size() * 0.99);
    size_t idx_p999 = static_cast<size_t>(latencies.size() * 0.999);

    if (idx_p50 >= latencies.size()) idx_p50 = latencies.size() - 1;
    if (idx_p90 >= latencies.size()) idx_p90 = latencies.size() - 1;
    if (idx_p99 >= latencies.size()) idx_p99 = latencies.size() - 1;
    if (idx_p999 >= latencies.size()) idx_p999 = latencies.size() - 1;

    LOG_INFO("=== %s ===", title);
    LOG_INFO("  Min:    %10.3f us", minUs);
    LOG_INFO("  Avg:    %10.3f us", avgUs);
    LOG_INFO("  P50:    %10.3f us", (double)latencies[idx_p50] / 1000.0);
    LOG_INFO("  P90:    %10.3f us", (double)latencies[idx_p90] / 1000.0);
    LOG_INFO("  P99:    %10.3f us", (double)latencies[idx_p99] / 1000.0);
    LOG_INFO("  P99.9:  %10.3f us", (double)latencies[idx_p999] / 1000.0);
    LOG_INFO("  Max:    %10.3f us", maxUs);
}

struct ClientState {
    int fd;
    char *read_buf;   /* 免拷贝: 接收头块 (iobuf block data 区, 替代 malloc read_buf) */
    size_t read_len;
    size_t write_len;
    bool write_pending;
    size_t msg_size;
    uint64_t msg_count;
    std::vector<struct iovec> write_iovs; /* 免拷贝: 回显数据 (指向接收链表上的 block, 替代 write_buf+memcpy) */

    // 单条消息调用的系统调用耗时累加器
    uint64_t accum_recv_api_ns = 0;  // 纯 readv API 总耗时
    uint64_t accum_write_api_ns = 0; // 纯 writev API 总耗时
};

struct ServerWorkerCtx {
    int epoll_fd = -1;
    int notify_fd = -1;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    std::vector<int> queue;
    BenchConfig *cfg = nullptr;
    pthread_t tid;
    std::vector<ClientState> clients;

    // 服务端只保留系统调用统计
    uint64_t total_msgs = 0;
    uint64_t total_bytes = 0;
    std::vector<uint64_t> recv_api_latencies;  // 纯 readv API 耗时
    std::vector<uint64_t> write_api_latencies; // 纯 writev API 耗时
};

struct ServerAcceptorCtx {
    BenchConfig *cfg;
    std::vector<ServerWorkerCtx> *workers;
};

static void *ServerWorkerThread(void *arg) {
    ServerWorkerCtx *ctx = static_cast<ServerWorkerCtx *>(arg);
    BenchConfig *cfg = ctx->cfg;
    int epoll_fd = ctx->epoll_fd;
    int notify_fd = ctx->notify_fd;

    ctx->recv_api_latencies.reserve(500000);
    ctx->write_api_latencies.reserve(500000);

    struct epoll_event events[MAX_EVENTS];

    while (!g_stop) {
        int nfds = ubsocket_epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            LOG_ERR("worker thread: epoll_wait failed, errno=%d", errno);
            break;
        }
        if (nfds == 0) continue;

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (fd == notify_fd) {
                if (evs & EPOLLIN) {
                    uint64_t val = 0;
                    ssize_t r = ::read(notify_fd, &val, sizeof(val));
                    (void)r;

                    std::vector<int> local_queue;
                    pthread_mutex_lock(&ctx->mutex);
                    local_queue.swap(ctx->queue);
                    pthread_mutex_unlock(&ctx->mutex);

                    for (int cfd : local_queue) {
                        /* 免拷贝: 接收头块用 iobuf block (替代 malloc) */
                        char *rbuf = AllocIobuf(static_cast<uint32_t>(g_iobuf_size));
                        if (rbuf == nullptr) {
                            LOG_ERR("worker thread: AllocIobuf failed");
                            ubsocket_close(cfd);
                            continue;
                        }

                        ClientState cs;
                        cs.fd = cfd;
                        cs.read_buf = rbuf;
                        cs.read_len = 0;
                        cs.write_len = 0;
                        cs.write_pending = false;
                        cs.msg_size = cfg->size;
                        cs.msg_count = 0;
                        cs.accum_recv_api_ns = 0;
                        cs.accum_write_api_ns = 0;

                        struct epoll_event cev {};
                        cev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                        cev.data.fd = cfd;

                        if (ubsocket_epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cfd, &cev) < 0) {
                            LOG_ERR("worker thread: epoll_ctl add failed");
                            FreeIobuf(rbuf);
                            ubsocket_close(cfd);
                            continue;
                        }
                        ctx->clients.push_back(std::move(cs));
                    }
                }
                continue;
            }

            auto it = std::find_if(ctx->clients.begin(), ctx->clients.end(),
                                   [fd](const ClientState &c) { return c.fd == fd; });
            if (it == ctx->clients.end()) continue;

            bool close_this_client = false;
            if (evs & (EPOLLERR | EPOLLHUP)) {
                close_this_client = true;
            }

            if (!close_this_client && (evs & EPOLLIN)) {
                while (true) {
                    size_t left = it->msg_size - it->read_len;
                    /* 免拷贝: readv 头块 data 区 (iov_base 不偏移, 数据落在链表上, 替代 recv 偏移) */
                    struct iovec iov;
                    iov.iov_base = it->read_buf;
                    iov.iov_len = left;

                    // --- 测量服务端纯 readv API 耗时 ---
                    auto t_start = std::chrono::steady_clock::now();
                    ssize_t n = ubsocket_readv(fd, &iov, 1);
                    auto t_end = std::chrono::steady_clock::now();
                    it->accum_recv_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        close_this_client = true;
                        break;
                    }
                    if (n == 0) {
                        close_this_client = true;
                        break;
                    }

                    it->read_len += static_cast<size_t>(n);
                    if (it->read_len == it->msg_size) {
                        /* 免拷贝: 构造回显 iovec (直接引用接收链表上的 block, 替代 memcpy) */
                        BuildChainIovs(it->read_buf, static_cast<uint32_t>(it->msg_size), it->write_iovs);
                        it->read_len = 0;
                        it->write_pending = true;

                        struct epoll_event ev {};
                        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                        ev.data.fd = fd;
                        ubsocket_epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                    }
                }
            }

            if (!close_this_client && (evs & EPOLLOUT)) {
                if (it->write_pending) {
                    while (it->write_len < it->msg_size) {
                        /* 免拷贝: writev 多块 (FindStartIdx 找剩余 iov 起始索引, 替代单块偏移) */
                        int start_idx = FindStartIdx(it->write_iovs, it->write_len);
                        if (start_idx >= static_cast<int>(it->write_iovs.size())) break;

                        // --- 测量服务端纯 writev API 耗时 ---
                        auto t_start = std::chrono::steady_clock::now();
                        ssize_t n = ubsocket_writev(fd, it->write_iovs.data() + start_idx,
                                                          static_cast<int>(it->write_iovs.size() - start_idx));
                        auto t_end = std::chrono::steady_clock::now();
                        it->accum_write_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

                        if (n < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            if (errno == EINTR) continue;
                            close_this_client = true;
                            break;
                        }
                        it->write_len += static_cast<size_t>(n);
                    }

                    if (it->write_len == it->msg_size) {
                        ctx->recv_api_latencies.push_back(it->accum_recv_api_ns);
                        ctx->write_api_latencies.push_back(it->accum_write_api_ns);
                        ctx->total_msgs++;
                        ctx->total_bytes += it->msg_size;

                        /* 免拷贝: 归还接收链表 */
                        ReleaseRxChain(it->read_buf);
                        it->write_iovs.clear();
                        it->write_len = 0;
                        it->write_pending = false;
                        it->accum_recv_api_ns = 0;
                        it->accum_write_api_ns = 0;
                        it->msg_count++;
                    }
                }
            }

            if (close_this_client) {
                ubsocket_epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                ubsocket_close(fd);
                /* 免拷贝: 释放接收链表 + 接收头块 + 回显 iovec */
                ReleaseRxChain(it->read_buf);
                FreeIobuf(it->read_buf);
                it->write_iovs.clear();
                ctx->clients.erase(it);
            }
        }
    }

    for (auto &c : ctx->clients) {
        ubsocket_epoll_ctl(epoll_fd, EPOLL_CTL_DEL, c.fd, nullptr);
        ubsocket_close(c.fd);
        ReleaseRxChain(c.read_buf);
        FreeIobuf(c.read_buf);
        c.write_iovs.clear();
    }
    ctx->clients.clear();
    return nullptr;
}

static void *ServerAcceptThread(void *arg) {
    ServerAcceptorCtx *actx = static_cast<ServerAcceptorCtx *>(arg);
    BenchConfig *cfg = actx->cfg;
    auto &workers = *(actx->workers);

    int listen_fd = ubsocket_socket(cfg->trans, SOCK_STREAM, 0);
    if (listen_fd < 0) return nullptr;

    int opt = 1;
    ubsocket_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ubsocket_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg->port));
    if (cfg->ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, cfg->ip.c_str(), &addr.sin_addr);
    }

    if (ubsocket_bind(listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        ubsocket_close(listen_fd);
        return nullptr;
    }
    if (ubsocket_listen(listen_fd, 1024) < 0) {
        ubsocket_close(listen_fd);
        return nullptr;
    }

    if (SetNonBlocking(listen_fd) < 0) {
        ubsocket_close(listen_fd);
        return nullptr;
    }

    int ep_fd = ubsocket_epoll_create1(0);
    if (ep_fd < 0) {
        ubsocket_close(listen_fd);
        return nullptr;
    }

    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (ubsocket_epoll_ctl(ep_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        ubsocket_close(ep_fd);
        ubsocket_close(listen_fd);
        return nullptr;
    }

    LOG_INFO("acceptor thread: listening on %s:%d (trans=%s)",
             cfg->ip.c_str(), cfg->port,
             cfg->trans == AF_SMC ? "ub" : "tcp");

    int rr_idx = 0;
    int num_workers = workers.size();
    struct epoll_event events[1];

    while (!g_stop) {
        int nfds = ubsocket_epoll_wait(ep_fd, events, 1, 100);
        if (nfds <= 0) continue;

        int cfd;
        while ((cfd = ubsocket_accept(listen_fd, nullptr, nullptr)) >= 0) {
            if (SetNonBlocking(cfd) < 0) {
                ubsocket_close(cfd);
                continue;
            }

            ServerWorkerCtx &worker = workers[rr_idx];
            rr_idx = (rr_idx + 1) % num_workers;

            pthread_mutex_lock(&worker.mutex);
            worker.queue.push_back(cfd);
            pthread_mutex_unlock(&worker.mutex);

            uint64_t val = 1;
            ssize_t w = ::write(worker.notify_fd, &val, sizeof(val));
            (void)w;
        }

        if (cfd < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            LOG_ERR("acceptor thread: fatal accept error, errno=%d", errno);
            break;
        }
    }

    ubsocket_close(ep_fd);
    ubsocket_close(listen_fd);
    return nullptr;
}

struct ClientCtx {
    BenchConfig *cfg;
    uint64_t msgCount = 0;
    uint64_t byteCount = 0;
    std::vector<uint64_t> rtt_latencies;       // 端到端 RTT 耗时
    std::vector<uint64_t> write_api_latencies; // 纯 writev API 耗时
    std::vector<uint64_t> recv_api_latencies;  // 纯 readv API 耗时
};

static void *ClientThread(void *arg) {
    ClientCtx *ctx = static_cast<ClientCtx *>(arg);
    BenchConfig *cfg = ctx->cfg;

    int fd = ubsocket_socket(cfg->trans, SOCK_STREAM, 0);
    if (fd < 0) return nullptr;

    if (SetNonBlocking(fd) < 0) {
        ubsocket_close(fd);
        return nullptr;
    }

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg->port));
    inet_pton(AF_INET, cfg->ip.c_str(), &addr.sin_addr);

    if (ubsocket_connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            ubsocket_close(fd);
            return nullptr;
        }
    }

    int epoll_fd = ubsocket_epoll_create1(0);
    if (epoll_fd < 0) {
        ubsocket_close(fd);
        return nullptr;
    }

    struct epoll_event ev {};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    if (ubsocket_epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        ubsocket_close(fd);
        ubsocket_close(epoll_fd);
        return nullptr;
    }

    /* 免拷贝: 发送块链 + 接收头块 (替代 malloc send_buf/recv_buf) */
    std::vector<struct iovec> send_iovs;
    if (!AllocSendChain(static_cast<size_t>(cfg->size), send_iovs)) {
        LOG_ERR("client thread: AllocSendChain failed (size=%d, block=%zu)", cfg->size, g_iobuf_size);
        ubsocket_close(fd);
        ubsocket_close(epoll_fd);
        return nullptr;
    }
    char *recv_buf = AllocIobuf(static_cast<uint32_t>(g_iobuf_size));
    if (recv_buf == nullptr) {
        FreeSendChain(send_iovs);
        ubsocket_close(fd);
        ubsocket_close(epoll_fd);
        return nullptr;
    }
    for (auto &iv : send_iovs) {
        memset(iv.iov_base, 'A', iv.iov_len);
    }

    bool connected = false;
    bool connection_pending = true;
    size_t send_off = 0;
    size_t recv_off = 0;
    bool send_pending = false;
    bool wait_response = false;

    uint64_t msg_count = 0;
    uint64_t byte_count = 0;

    std::chrono::steady_clock::time_point send_start;

    // 单条消息调用的系统调用耗时累加器
    uint64_t accum_write_api_ns = 0;
    uint64_t accum_recv_api_ns = 0;

    std::vector<uint64_t> local_rtt_latencies;
    std::vector<uint64_t> local_write_api_latencies;
    std::vector<uint64_t> local_recv_api_latencies;

    size_t reserve_cap = (cfg->count > 0) ? (cfg->count / cfg->threads + 100) : 1000000;
    local_rtt_latencies.reserve(reserve_cap);
    local_write_api_latencies.reserve(reserve_cap);
    local_recv_api_latencies.reserve(reserve_cap);

    uint64_t target_interval_ns = 0;
    if (cfg->qps > 0) {
        long long thread_qps = cfg->qps / cfg->threads;
        if (thread_qps <= 0) thread_qps = 1;
        target_interval_ns = 1000000000ULL / thread_qps;
    }

    auto start = std::chrono::steady_clock::now();
    const bool byCount = (cfg->count > 0);

    struct epoll_event events[MAX_EVENTS];

    while (!g_stop) {
        /* 检查时间是否到期 (确保 --time 到期后及时退出, 不依赖 recv_off==size 触发) */
        if (!byCount) {
            auto now = std::chrono::steady_clock::now();
            double el = std::chrono::duration<double>(now - start).count();
            if (el >= cfg->timeSec) {
                goto client_end;
            }
        }

        int nfds = ubsocket_epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (nfds == 0) continue;

        for (int i = 0; i < nfds; ++i) {
            int fd_ev = events[i].data.fd;
            if (fd_ev != fd) continue;
            uint32_t evs = events[i].events;

            if (evs & (EPOLLERR | EPOLLHUP)) goto client_end;

            if (evs & EPOLLOUT) {
                if (!connected && connection_pending) {
                    /* 免拷贝: UB connect 同步返回 0 即已连接, 不走 getsockopt(SO_ERROR) */
                    bool connected_ok = false;
                    if (cfg->trans == AF_SMC) {
                        connected_ok = true;
                    } else {
                        int err = 0;
                        socklen_t len = sizeof(err);
                        connected_ok = (ubsocket_getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0);
                    }
                    if (connected_ok) {
                        connected = true;
                        connection_pending = false;
                        send_pending = true;
                        LOG_INFO("client thread %lu: connected to %s:%d",
                                 reinterpret_cast<unsigned long>(pthread_self()) % 100000,
                                 cfg->ip.c_str(), cfg->port);
                    } else {
                        goto client_end;
                    }
                }

                if (connected && send_pending && !wait_response) {
                    if (send_off == 0) {
                        send_start = std::chrono::steady_clock::now();
                    }
                    while (send_off < static_cast<size_t>(cfg->size)) {
                        /* 免拷贝: writev 多块 (FindStartIdx 找剩余 iov 起始索引, 替代单块偏移) */
                        int start_idx = FindStartIdx(send_iovs, send_off);
                        if (start_idx >= static_cast<int>(send_iovs.size())) break;

                        // --- 1. 客户端纯 writev API 耗时 ---
                        auto t_start = std::chrono::steady_clock::now();
                        ssize_t n = ubsocket_writev(fd, send_iovs.data() + start_idx,
                                                          static_cast<int>(send_iovs.size() - start_idx));
                        auto t_end = std::chrono::steady_clock::now();
                        accum_write_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

                        if (n < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            if (errno == EINTR) continue;
                            goto client_end;
                        }
                        send_off += static_cast<size_t>(n);
                    }
                    if (send_off == static_cast<size_t>(cfg->size)) {
                        send_pending = false;
                        wait_response = true;
                        send_off = 0;
                        recv_off = 0;
                    }
                }
            }

            if (evs & EPOLLIN) {
                if (!wait_response) continue;
                while (recv_off < static_cast<size_t>(cfg->size)) {
                    /* 免拷贝: readv 头块 data 区 (iov_base 不偏移, 数据落在链表上, 替代 recv 偏移) */
                    struct iovec iov;
                    iov.iov_base = recv_buf;
                    iov.iov_len = static_cast<size_t>(cfg->size) - recv_off;

                    // --- 2. 客户端纯 readv API 耗时 ---
                    auto t_start = std::chrono::steady_clock::now();
                    ssize_t n = ubsocket_readv(fd, &iov, 1);
                    auto t_end = std::chrono::steady_clock::now();
                    accum_recv_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        goto client_end;
                    }
                    if (n == 0) goto client_end;
                    recv_off += static_cast<size_t>(n);

                    if (recv_off == static_cast<size_t>(cfg->size)) {
                        auto recv_end = std::chrono::steady_clock::now();

                        // 仅记录 RTT、writev API 和 readv API
                        uint64_t rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(recv_end - send_start).count();

                        local_rtt_latencies.push_back(rtt_ns);
                        local_write_api_latencies.push_back(accum_write_api_ns);
                        local_recv_api_latencies.push_back(accum_recv_api_ns);

                        accum_write_api_ns = 0;
                        accum_recv_api_ns = 0;

                        /* 免拷贝: 归还接收链表 */
                        ReleaseRxChain(recv_buf);

                        msg_count++;
                        byte_count += cfg->size;
                        wait_response = false;

                        if (byCount && (long long)msg_count >= cfg->count) {
                            goto client_end;
                        }
                        auto now = std::chrono::steady_clock::now();
                        double el = std::chrono::duration<double>(now - start).count();
                        if (!byCount && el >= cfg->timeSec) {
                            goto client_end;
                        }

                        if (target_interval_ns > 0) {
                            auto cycle_end = std::chrono::steady_clock::now();
                            uint64_t cycle_elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(cycle_end - send_start).count();
                            if (cycle_elapsed_ns < target_interval_ns) {
                                timespec req;
                                uint64_t sleep_ns = target_interval_ns - cycle_elapsed_ns;
                                req.tv_sec = sleep_ns / 1000000000ULL;
                                req.tv_nsec = sleep_ns % 1000000000ULL;
                                nanosleep(&req, nullptr);
                            }
                        }

                        send_off = 0;
                        send_pending = true;
                        send_start = std::chrono::steady_clock::now();

                        while (send_off < static_cast<size_t>(cfg->size)) {
                            int start_idx = FindStartIdx(send_iovs, send_off);
                            if (start_idx >= static_cast<int>(send_iovs.size())) break;

                            auto t0 = std::chrono::steady_clock::now();
                            ssize_t n = ubsocket_writev(fd, send_iovs.data() + start_idx,
                                                              static_cast<int>(send_iovs.size() - start_idx));
                            auto t1 = std::chrono::steady_clock::now();
                            accum_write_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

                            if (n < 0) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                                if (errno == EINTR) continue;
                                goto client_end;
                            }
                            send_off += static_cast<size_t>(n);
                        }
                        if (send_off == static_cast<size_t>(cfg->size)) {
                            send_pending = false;
                            wait_response = true;
                            recv_off = 0;
                        }
                    }
                }
            }
        }
    }

client_end:
    ctx->msgCount = msg_count;
    ctx->byteCount = byte_count;
    ctx->rtt_latencies = std::move(local_rtt_latencies);
    ctx->write_api_latencies = std::move(local_write_api_latencies);
    ctx->recv_api_latencies = std::move(local_recv_api_latencies);

    /* 免拷贝: 释放发送块链 + 接收头块 (替代 free) */
    FreeSendChain(send_iovs);
    FreeIobuf(recv_buf);
    ubsocket_close(epoll_fd);
    ubsocket_close(fd);
    return nullptr;
}

static std::string GetOpt(int argc, char **argv, int &i, const std::string &key) {
    std::string a = argv[i];
    if (a == key && i + 1 < argc) return std::string(argv[++i]);
    if (a.rfind(key + "=", 0) == 0) return a.substr(key.size() + 1);
    return "";
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr,
            "Usage:\n"
            "  %s sr --threads 16 -p 11111 [-i 0.0.0.0] [--size 1024] [--ub|--tcp]\n"
            "  %s pp -i 127.0.0.1 -p 11111 [--threads 1] [--size 1024] "
            "[--time 10 | --count 1000000] [--qps 0] [--ub|--tcp]\n"
            "\nZero-copy variant: direct ubsocket_ API, no LD_PRELOAD.\n",
            argv[0], argv[0]);
        return 1;
    }

    BenchConfig cfg;
    cfg.role = argv[1];

    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        std::string v;
        if (a == "--tcp") {
            cfg.trans = AF_INET;
        } else if (a == "--ub") {
            cfg.trans = AF_SMC;
        } else if (!(v = GetOpt(argc, argv, i, "--threads")).empty()) {
            cfg.threads = std::atoi(v.c_str());
        } else if (!(v = GetOpt(argc, argv, i, "-p")).empty() || !(v = GetOpt(argc, argv, i, "--port")).empty()) {
            cfg.port = std::atoi(v.c_str());
        } else if (!(v = GetOpt(argc, argv, i, "-i")).empty() || !(v = GetOpt(argc, argv, i, "--ip")).empty()) {
            cfg.ip = v;
        } else if (!(v = GetOpt(argc, argv, i, "--size")).empty()) {
            cfg.size = std::atoi(v.c_str());
        } else if (!(v = GetOpt(argc, argv, i, "--time")).empty()) {
            cfg.timeSec = std::atof(v.c_str());
            cfg.count = 0;
        } else if (!(v = GetOpt(argc, argv, i, "--count")).empty()) {
            cfg.count = std::atoll(v.c_str());
        } else if (!(v = GetOpt(argc, argv, i, "--trans")).empty()) {
            cfg.trans = (v == "tcp") ? AF_INET : AF_SMC;
        } else if (!(v = GetOpt(argc, argv, i, "--qps")).empty()) {
            cfg.qps = std::atoll(v.c_str());
        }
    }

    if (cfg.threads < 1) cfg.threads = 1;
    if (cfg.role == "pp" && cfg.ip == "0.0.0.0") cfg.ip = "127.0.0.1";

    /* 免拷贝: 显式初始化 ubsocket (不走 LD_PRELOAD) */
    u_init_options_t options;
    if (ubsocket_init_options(&options) != 0) {
        LOG_ERR("ubsocket_init_options failed");
        return 1;
    }
    options.allowed_protocol = UBS_PROTOCOL_UB_RM_RTP | UBS_PROTOCOL_UB_RC_RTP;
    if (ubsocket_init(&options) != 0) {
        LOG_ERR("ubsocket_init failed");
        return 1;
    }
    LOG_INFO("ubsocket initialized (zero-copy epoll mode)");

    ProbeIobufSize();
    if (g_iobuf_size == 0) {
        LOG_ERR("ProbeIobufSize failed, cannot determine iobuf block size");
        ubsocket_uninit();
        return 1;
    }
    LOG_INFO("iobuf block data size = %zu bytes", g_iobuf_size);

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    if (cfg.role == "sr") {
        LOG_INFO("=== UBSocket bench server (sr/zc) | threads=%d port=%d trans=%s size=%d ===",
                cfg.threads, cfg.port, cfg.trans == AF_SMC ? "ub" : "tcp", cfg.size);

        int num_workers = cfg.threads;
        std::vector<ServerWorkerCtx> workers(num_workers);

        for (int i = 0; i < num_workers; ++i) {
            workers[i].cfg = &cfg;
            workers[i].epoll_fd = ubsocket_epoll_create1(0);
            workers[i].notify_fd = eventfd(0, EFD_NONBLOCK);

            struct epoll_event ev {};
            ev.events = EPOLLIN;
            ev.data.fd = workers[i].notify_fd;
            ubsocket_epoll_ctl(workers[i].epoll_fd, EPOLL_CTL_ADD, workers[i].notify_fd, &ev);

            pthread_mutex_init(&workers[i].mutex, nullptr);
            pthread_create(&workers[i].tid, nullptr, ServerWorkerThread, &workers[i]);
        }

        ServerAcceptorCtx actx;
        actx.cfg = &cfg;
        actx.workers = &workers;

        pthread_t accept_tid;
        pthread_create(&accept_tid, nullptr, ServerAcceptThread, &actx);

        auto server_start = std::chrono::steady_clock::now();

        // 阻塞等待 Ctrl+C 退出
        pthread_join(accept_tid, nullptr);

        for (int i = 0; i < num_workers; ++i) {
            uint64_t val = 1;
            ::write(workers[i].notify_fd, &val, sizeof(val));
            pthread_join(workers[i].tid, nullptr);
            ubsocket_close(workers[i].epoll_fd);
            ::close(workers[i].notify_fd);
            pthread_mutex_destroy(&workers[i].mutex);
        }

        auto server_end = std::chrono::steady_clock::now();
        double total_el = std::chrono::duration<double>(server_end - server_start).count();
        if (total_el <= 0) total_el = 1e-9;

        uint64_t total_server_msgs = 0;
        uint64_t total_server_bytes = 0;
        std::vector<uint64_t> all_recv_api_lat;
        std::vector<uint64_t> all_write_api_lat;

        for (int i = 0; i < num_workers; ++i) {
            total_server_msgs += workers[i].total_msgs;
            total_server_bytes += workers[i].total_bytes;

            all_recv_api_lat.insert(all_recv_api_lat.end(), workers[i].recv_api_latencies.begin(), workers[i].recv_api_latencies.end());
            all_write_api_lat.insert(all_write_api_lat.end(), workers[i].write_api_latencies.begin(), workers[i].write_api_latencies.end());
        }

        LOG_INFO("=== Server Stats summary ===");
        LOG_INFO("=== Total Handled: msgs=%llu bytes=%llu throughput=%.2f msg/s, %.2f Mbps ===",
                static_cast<unsigned long long>(total_server_msgs),
                static_cast<unsigned long long>(total_server_bytes),
                total_server_msgs / total_el, (double)total_server_bytes * 8 / total_el / 1024 / 1024);

        PrintLatencyStats("Server Latency: Readv API Time", all_recv_api_lat);
        PrintLatencyStats("Server Latency: Writev API Time", all_write_api_lat);

    } else {
        LOG_INFO("=== UBSocket bench client (pp/zc) | threads=%d ip=%s port=%d trans=%s size=%d ===",
                cfg.threads, cfg.ip.c_str(), cfg.port, cfg.trans == AF_SMC ? "ub" : "tcp", cfg.size);

        auto global_start = std::chrono::steady_clock::now();

        std::vector<ClientCtx> ctxs(cfg.threads);
        std::vector<pthread_t> tids(cfg.threads);
        for (int i = 0; i < cfg.threads; ++i) {
            ctxs[i].cfg = &cfg;
            pthread_create(&tids[i], nullptr, ClientThread, &ctxs[i]);
        }

        uint64_t totalMsg = 0, totalByte = 0;
        for (int i = 0; i < cfg.threads; ++i) {
            pthread_join(tids[i], nullptr);
            totalMsg += ctxs[i].msgCount;
            totalByte += ctxs[i].byteCount;
        }

        auto global_end = std::chrono::steady_clock::now();
        double total_el = std::chrono::duration<double>(global_end - global_start).count();
        if (total_el <= 0) total_el = 1e-9;

        std::vector<uint64_t> all_rtt;
        std::vector<uint64_t> all_write_api;
        std::vector<uint64_t> all_recv_api;

        all_rtt.reserve(totalMsg);
        all_write_api.reserve(totalMsg);
        all_recv_api.reserve(totalMsg);

        for (int i = 0; i < cfg.threads; ++i) {
            all_rtt.insert(all_rtt.end(), ctxs[i].rtt_latencies.begin(), ctxs[i].rtt_latencies.end());
            all_write_api.insert(all_write_api.end(), ctxs[i].write_api_latencies.begin(), ctxs[i].write_api_latencies.end());
            all_recv_api.insert(all_recv_api.end(), ctxs[i].recv_api_latencies.begin(), ctxs[i].recv_api_latencies.end());
        }

        LOG_INFO("=== client total: msgs=%llu bytes=%llu time=%.3fs throughput=%.2f msg/s, %.2f Mbps ===",
                static_cast<unsigned long long>(totalMsg),
                static_cast<unsigned long long>(totalByte),
                total_el, totalMsg / total_el, (double)totalByte * 8 / total_el / 1024 / 1024);

        PrintLatencyStats("Client Latency: Total RTT", all_rtt);
        PrintLatencyStats("Client Latency: Readv API Time", all_recv_api);
        PrintLatencyStats("Client Latency: Writev API Time", all_write_api);
    }

    ubsocket_uninit();
    return 0;
}
