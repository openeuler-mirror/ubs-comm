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

#ifndef AF_SMC
#define AF_SMC 43
#endif

#define MAX_EVENTS 64

#define LOG_INFO(fmt, ...)  std::fprintf(stdout, "[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)   std::fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

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
    std::string recvApi = "recv"; // 收包 API 选择：recv | readv
    bool useReadv = false;        // recvApi=="readv" 的派生标志，避免收包热路径里反复做字符串比较
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
    char *read_buf;
    char *write_buf;
    size_t read_len;
    size_t write_len;
    bool write_pending;
    size_t msg_size;
    uint64_t msg_count;

    // 单条消息调用的系统调用耗时累加器
    uint64_t accum_recv_api_ns = 0;  // 纯 recv 系统调用总耗时
    uint64_t accum_write_api_ns = 0; // 纯 writev 系统调用总耗时
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
    std::vector<uint64_t> recv_api_latencies;  // 纯 recv API 耗时
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
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
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
                        char *rbuf = (char*)malloc(cfg->size);
                        char *wbuf = (char*)malloc(cfg->size);
                        if (!rbuf || !wbuf) {
                            LOG_ERR("worker thread: malloc failed");
                            free(rbuf); free(wbuf);
                            ::close(cfd);
                            continue;
                        }

                        ClientState cs;
                        cs.fd = cfd;
                        cs.read_buf = rbuf;
                        cs.write_buf = wbuf;
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

                        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, cfd, &cev) < 0) {
                            LOG_ERR("worker thread: epoll_ctl add failed");
                            free(rbuf); free(wbuf);
                            ::close(cfd);
                            continue;
                        }
                        ctx->clients.push_back(cs);
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
                    
                    // --- 测量服务端纯 recv/readv 系统调用耗时 ---
                    auto t_start = std::chrono::steady_clock::now();
                    ssize_t n;
                    if (cfg->useReadv) {
                        struct iovec iov;
                        iov.iov_base = it->read_buf + it->read_len;
                        iov.iov_len = left;
                        n = ::readv(fd, &iov, 1);
                    } else {
                        n = ::recv(fd, it->read_buf + it->read_len, left, 0);
                    }
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

                    it->read_len += n;
                    if (it->read_len == it->msg_size) {
                        std::memcpy(it->write_buf, it->read_buf, it->msg_size);
                        it->read_len = 0;
                        it->write_pending = true;

                        struct epoll_event ev {};
                        ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                        ev.data.fd = fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
                    }
                }
            }

            if (!close_this_client && (evs & EPOLLOUT)) {
                if (it->write_pending) {
                    while (it->write_len < it->msg_size) {
                        struct iovec iov;
                        iov.iov_base = it->write_buf + it->write_len;
                        iov.iov_len = it->msg_size - it->write_len;

                        // --- 测量服务端纯 writev 系统调用耗时 ---
                        auto t_start = std::chrono::steady_clock::now();
                        ssize_t n = ::writev(fd, &iov, 1);
                        auto t_end = std::chrono::steady_clock::now();
                        it->accum_write_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

                        if (n < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            if (errno == EINTR) continue;
                            close_this_client = true;
                            break;
                        }
                        it->write_len += n;
                    }

                    if (it->write_len == it->msg_size) {
                        ctx->recv_api_latencies.push_back(it->accum_recv_api_ns);
                        ctx->write_api_latencies.push_back(it->accum_write_api_ns);
                        ctx->total_msgs++;
                        ctx->total_bytes += it->msg_size;

                        it->write_len = 0;
                        it->write_pending = false;
                        it->accum_recv_api_ns = 0;
                        it->accum_write_api_ns = 0;
                        it->msg_count++;
                    }
                }
            }

            if (close_this_client) {
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                ::close(fd);
                free(it->read_buf);
                free(it->write_buf);
                ctx->clients.erase(it);
            }
        }
    }

    for (auto &c : ctx->clients) {
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, c.fd, nullptr);
        ::close(c.fd);
        free(c.read_buf);
        free(c.write_buf);
    }
    ctx->clients.clear();
    return nullptr;
}

static void *ServerAcceptThread(void *arg) {
    ServerAcceptorCtx *actx = static_cast<ServerAcceptorCtx *>(arg);
    BenchConfig *cfg = actx->cfg;
    auto &workers = *(actx->workers);

    int listen_fd = ::socket(cfg->trans, SOCK_STREAM, 0);
    if (listen_fd < 0) return nullptr;

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg->port));
    if (cfg->ip == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, cfg->ip.c_str(), &addr.sin_addr);
    }

    if (::bind(listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        ::close(listen_fd);
        return nullptr;
    }
    if (::listen(listen_fd, 1024) < 0) {
        ::close(listen_fd);
        return nullptr;
    }

    if (SetNonBlocking(listen_fd) < 0) {
        ::close(listen_fd);
        return nullptr;
    }

    int ep_fd = epoll_create1(0);
    if (ep_fd < 0) {
        ::close(listen_fd);
        return nullptr;
    }

    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        ::close(ep_fd);
        ::close(listen_fd);
        return nullptr;
    }

    LOG_INFO("acceptor thread: listening on %s:%d (trans=%s)",
             cfg->ip.c_str(), cfg->port,
             cfg->trans == AF_SMC ? "ub" : "tcp");

    int rr_idx = 0;
    int num_workers = workers.size();
    struct epoll_event events[1];

    while (!g_stop) {
        int nfds = epoll_wait(ep_fd, events, 1, 100);
        if (nfds <= 0) continue;

        int cfd;
        while ((cfd = ::accept(listen_fd, nullptr, nullptr)) >= 0) {
            if (SetNonBlocking(cfd) < 0) {
                ::close(cfd);
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

    ::close(ep_fd);
    ::close(listen_fd);
    return nullptr;
}

struct ClientCtx {
    BenchConfig *cfg;
    uint64_t msgCount = 0;
    uint64_t byteCount = 0;
    std::vector<uint64_t> rtt_latencies;       // 端到端 RTT 耗时
    std::vector<uint64_t> write_api_latencies; // 纯 writev 系统调用耗时
    std::vector<uint64_t> recv_api_latencies;  // 纯 recv 系统调用耗时
};

static void *ClientThread(void *arg) {
    ClientCtx *ctx = static_cast<ClientCtx *>(arg);
    BenchConfig *cfg = ctx->cfg;

    int fd = ::socket(cfg->trans, SOCK_STREAM, 0);
    if (fd < 0) return nullptr;

    if (SetNonBlocking(fd) < 0) {
        ::close(fd);
        return nullptr;
    }

    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(cfg->port));
    inet_pton(AF_INET, cfg->ip.c_str(), &addr.sin_addr);

    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            ::close(fd);
            return nullptr;
        }
    }

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        ::close(fd);
        return nullptr;
    }

    struct epoll_event ev {};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        ::close(fd); ::close(epoll_fd);
        return nullptr;
    }

    char *send_buf = (char*)malloc(cfg->size);
    char *recv_buf = (char*)malloc(cfg->size);
    if (!send_buf || !recv_buf) {
        free(send_buf); free(recv_buf);
        ::close(fd); ::close(epoll_fd);
        return nullptr;
    }
    memset(send_buf, 'A', cfg->size);

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
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);
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
                    int err = 0;
                    socklen_t len = sizeof(err);
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
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
                    while (send_off < cfg->size) {
                        struct iovec iov;
                        iov.iov_base = send_buf + send_off;
                        iov.iov_len = cfg->size - send_off;
                        
                        // --- 1. 客户端纯 writev 系统调用耗时 ---
                        auto t_start = std::chrono::steady_clock::now();
                        ssize_t n = ::writev(fd, &iov, 1);
                        auto t_end = std::chrono::steady_clock::now();
                        accum_write_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

                        if (n < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            if (errno == EINTR) continue;
                            goto client_end;
                        }
                        send_off += n;
                    }
                    if (send_off == cfg->size) {
                        send_pending = false;
                        wait_response = true;
                        send_off = 0;
                        recv_off = 0;
                    }
                }
            }

            if (evs & EPOLLIN) {
                if (!wait_response) continue;
                while (recv_off < cfg->size) {
                    // --- 2. 客户端纯 recv/readv 系统调用耗时 ---
                    auto t_start = std::chrono::steady_clock::now();
                    ssize_t n;
                    if (cfg->useReadv) {
                        struct iovec iov;
                        iov.iov_base = recv_buf + recv_off;
                        iov.iov_len = cfg->size - recv_off;
                        n = ::readv(fd, &iov, 1);
                    } else {
                        n = ::recv(fd, recv_buf + recv_off, cfg->size - recv_off, 0);
                    }
                    auto t_end = std::chrono::steady_clock::now();
                    accum_recv_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t_end - t_start).count();

                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        goto client_end;
                    }
                    if (n == 0) goto client_end;
                    recv_off += n;

                    if (recv_off == cfg->size) {
                        auto recv_end = std::chrono::steady_clock::now();

                        // 仅记录 RTT、writev API 和 recv API
                        uint64_t rtt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(recv_end - send_start).count();

                        local_rtt_latencies.push_back(rtt_ns);
                        local_write_api_latencies.push_back(accum_write_api_ns);
                        local_recv_api_latencies.push_back(accum_recv_api_ns);

                        accum_write_api_ns = 0;
                        accum_recv_api_ns = 0;

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

                        while (send_off < cfg->size) {
                            struct iovec iov;
                            iov.iov_base = send_buf + send_off;
                            iov.iov_len = cfg->size - send_off;
                            
                            auto t0 = std::chrono::steady_clock::now();
                            ssize_t n = writev(fd, &iov, 1);
                            auto t1 = std::chrono::steady_clock::now();
                            accum_write_api_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

                            if (n < 0) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                                if (errno == EINTR) continue;
                                goto client_end;
                            }
                            send_off += n;
                        }
                        if (send_off == cfg->size) {
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

    ::close(epoll_fd);
    ::close(fd);
    free(send_buf);
    free(recv_buf);
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
        std::fprintf(stderr, "Usage instructions missing...\n");
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
        } else if (!(v = GetOpt(argc, argv, i, "--recv-api")).empty()) {
            if (v == "readv") {
                cfg.recvApi = "readv";
                cfg.useReadv = true;
            } else if (v == "recv") {
                cfg.recvApi = "recv";
                cfg.useReadv = false;
            } else {
                LOG_ERR("unknown --recv-api '%s' (expect recv|readv), fallback to recv", v.c_str());
            }
        }
    }

    if (cfg.threads < 1) cfg.threads = 1;
    if (cfg.role == "pp" && cfg.ip == "0.0.0.0") cfg.ip = "127.0.0.1";

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    // 收包 API 名称用于统计标题，避免选了 readv 却仍打印 "Recv API"
    const std::string recvApiLabel = cfg.useReadv ? "Readv API" : "Recv API";

    if (cfg.role == "sr") {
        LOG_INFO("=== UBSocket bench server (sr) | threads=%d port=%d trans=%s size=%d recv-api=%s ===",
                cfg.threads, cfg.port, cfg.trans == AF_SMC ? "ub" : "tcp", cfg.size, cfg.recvApi.c_str());

        int num_workers = cfg.threads;
        std::vector<ServerWorkerCtx> workers(num_workers);

        for (int i = 0; i < num_workers; ++i) {
            workers[i].cfg = &cfg;
            workers[i].epoll_fd = epoll_create1(0);
            workers[i].notify_fd = eventfd(0, EFD_NONBLOCK);

            struct epoll_event ev {};
            ev.events = EPOLLIN;
            ev.data.fd = workers[i].notify_fd;
            epoll_ctl(workers[i].epoll_fd, EPOLL_CTL_ADD, workers[i].notify_fd, &ev);

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
            ::close(workers[i].epoll_fd);
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

        PrintLatencyStats(("Server Latency: " + recvApiLabel + " Time").c_str(), all_recv_api_lat);
        PrintLatencyStats("Server Latency: Writev API Time", all_write_api_lat);

    } else {
        LOG_INFO("=== UBSocket bench client (pp) | threads=%d ip=%s port=%d trans=%s size=%d recv-api=%s ===",
                cfg.threads, cfg.ip.c_str(), cfg.port, cfg.trans == AF_SMC ? "ub" : "tcp", cfg.size,
                cfg.recvApi.c_str());
        
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
        PrintLatencyStats(("Client Latency: " + recvApiLabel + " Time").c_str(), all_recv_api);
        PrintLatencyStats("Client Latency: Writev API Time", all_write_api);
    }
    return 0;
}