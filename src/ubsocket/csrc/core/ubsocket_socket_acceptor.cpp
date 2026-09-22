/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "ubsocket_socket_acceptor.h"
#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_thread_pool.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "ubsocket_core_types.h"
#include "ubsocket_event_epoll.h"
#include "ubsocket_socket.h"
#include "ubsocket_wakeup_event.h"

namespace ock {
namespace ubs {

Acceptor::AsyncAcceptInfo::~AsyncAcceptInfo()
{
    if (lock != nullptr) {
        {
            Locker sLock(lock);
            while (!ready_queue.empty()) {
                /* accepted (and handshaken) but never handed to brpc: close, or the fd leaks */
                (void)LibcApi::close(std::get<0>(ready_queue.front()));
                ready_queue.pop();
            }
            asyncTaskNum.store(0);
        }
        LockRegistry::LOCK_OPS.destroy(lock);
        lock = nullptr;
    }
}

int Acceptor::Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
    // 根据全局配置分发到 同步 / 异步 逻辑
    if (GlobalSetting::AsyncAcceptorEnabled()) {
        return AcceptAsync(sock, address, address_len);
    } else {
        return AcceptSync(sock, address, address_len);
    }
}

int Acceptor::AcceptSync(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
    struct sockaddr addr_tmp;
    socklen_t len_tmp = sizeof(addr_tmp);
    int fd = LibcApi::accept(raw_fd_, &addr_tmp, &len_tmp);
    if (fd < 0) {
        HandleAcceptError(sock, fd);
        return fd;
    }

    if (address != nullptr && address_len != nullptr) {
        *address = addr_tmp;
        *address_len = len_tmp;
    }
    ProcessAcceptedFd(fd, &addr_tmp);

    // 如果不是 UBS 连接（TFO等），作为普通 TCP 直接返回
    if (!SocketConnHelper::IsUbsConnection(fd) || sock->State() == SOCK_STAT_RAW_ESTABLISHED) {
        return fd;
    }

    std::string peerIp = SocketConnHelper::ExtractIpFromSockAddr(&addr_tmp);
    int peerPort = SocketConnHelper::ExtractPortFromSockAddr(&addr_tmp);
    if (!ProcessUBConnection(fd, peerIp)) {
        /* fd 已在协商失败时关闭：不得把已关闭（可能已被复用）的 fd 号交给上层。
         * 以 EAGAIN 让上层 accept 循环视作本轮无新连接，下一次可读事件再来。 */
        errno = EAGAIN;
        return -1;
    }
    TracePeerIp(fd, peerIp, peerPort);
    return fd;
}

int Acceptor::AcceptAsync(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
    AsyncAcceptInfo *asyncCtx = EnsureAsyncAcceptCtx();
    if (UNLIKELY(asyncCtx == nullptr)) {
        UBS_VLOG_ERR("async accept: failed to create async accept context, fd=%d\n", raw_fd_);
        errno = ENOMEM;
        return -1;
    }

    // 懒初始化：启动 ExecutorService + 初始化 wakeup_event
    InitWakeupEvent();

    struct sockaddr addr_tmp;
    socklen_t len_tmp = sizeof(addr_tmp);
    uint32_t accepted_count = 0;
    // 无论 ready_queue 有没有数据，只要来了 Accept 调用，就先去内核把全新的 TCP 连接全部 accept 出来抛给线程池！
    while (true) {
        len_tmp = sizeof(addr_tmp);
        int fd = LibcApi::accept(raw_fd_, &addr_tmp, &len_tmp);

        if (fd < 0) {
            // EAGAIN / EWOULDBLOCK 表示内核全连接队列已空，正常退出循环
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            // 其他异常错误（如 EMFILE/ECONNABORTED 等）：
            // 如果连第一个 fd 都没 accept 成功，直接返回错误 fd 供上层处理；否则退出循环处理已提取的连接
            if (accepted_count == 0) {
                HandleAcceptError(sock, fd);
                return fd;
            }
            break;
        }

        accepted_count++;
        ProcessAcceptedFd(fd, &addr_tmp);

        // 非 UBS 特殊连接，直接入队准备返回，无需异步协商
        if (!SocketConnHelper::IsUbsConnection(fd)) {
            asyncCtx->ready_queue.push(std::make_tuple(fd, addr_tmp, len_tmp));
            continue;
        }

        std::string peerIp = SocketConnHelper::ExtractIpFromSockAddr(&addr_tmp);
        int peerPort = SocketConnHelper::ExtractPortFromSockAddr(&addr_tmp);

        UBS_VLOG_DEBUG("async accept execute. fd:%d", fd);
        asyncCtx->asyncTaskNum.fetch_add(1U);
        bool exec_ret =
            /* sock (a Ref) keeps the listening Socket — and with it this Acceptor and asyncCtx — alive
             * until the task finishes; raw captures let the acceptor die under a running task (issue #50). */
            ExecutorService::GetExecutorService()->Execute([this, asyncCtx, sock, fd, addr_tmp, len_tmp, peerIp, peerPort]() {
            UBS_VLOG_DEBUG("async accept start. fd:%d\n", fd);
            if (!ProcessUBConnection(fd, peerIp)) {
                /* fd 已关闭：不入 ready 队列（否则上层会拿到一个已关闭/可能被复用的 fd 号）。
                 * 仍需回收任务计数；无需唤醒——没有可交付的连接。 */
                asyncCtx->asyncTaskNum.fetch_sub(1U);
                UBS_VLOG_DEBUG("async accept: handshake failed, fd closed, not delivered. fd:%d\n", fd);
                return;
            }
            TracePeerIp(fd, peerIp, peerPort);
            {
                Locker sLock(asyncCtx->lock);
                asyncCtx->ready_queue.push(std::make_tuple(fd, addr_tmp, len_tmp));
            }

            // 触发 eventfd 唤醒主 epoll 线程读取 ready_queue
            asyncCtx->wakeup_event.WakeUpReadyEventFd(raw_fd_);
            asyncCtx->asyncTaskNum.fetch_sub(1U);
            UBS_VLOG_DEBUG("async accept success. fd:%d\n", fd);
        });

        if (!exec_ret) {
            asyncCtx->asyncTaskNum.fetch_sub(1U);
            // 线程池满/提交失败，退化为同步处理，防止连接饥饿
            UBS_VLOG_DEBUG("submit async accept task failed, fallback sync. fd:%d\n", fd);
            if (!ProcessUBConnection(fd, peerIp)) {
                /* fd 已关闭：继续抽干内核队列，本条不交付 */
                continue;
            }
            return fd;
        }
    }

    // 抽干内核队列后，统一从 ready_queue 中尝试弹出 1 个 FD 返回给上层
    int ready_fd = -1;
    if (TryPopAsyncReadyFd(ready_fd, address, address_len)) {
        UBS_VLOG_DEBUG("[Debug]: Accept Async fd count: %u, currently pop fd: %d\n", accepted_count, ready_fd);
        return ready_fd;
    }
    UBS_VLOG_DEBUG("[Debug]: Accept Async fd count: %u, return EAGAIN\n", accepted_count);
    // 如果内核被抽干了，且当前 ready_queue 也是空的（说明所有连接都在线程池异步处理中），
    // 设置 EAGAIN 返回 -1，告知上层等待下一次 eventfd 唤醒
    errno = EAGAIN;
    return -1;
}

void Acceptor::ProcessAcceptedFd(int fd, struct sockaddr *addr_tmp)
{
    if (SocketConnHelper::IsUbsConnection(fd)) {
        int tcpNoDelayRet = SocketConnHelper::SetTcpNoDelay(fd);
        if (tcpNoDelayRet != 0) {
            UBS_VLOG_WARN("Set TCP_NODELAY failed, fd %d, ret %d, errno %d\n", fd, tcpNoDelayRet, errno);
        }
    }

    SocketPtr sock_obj = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (sock_obj != nullptr) {
        auto sockBase = RefConvert<Socket, SocketBase>(sock_obj);
        SocketBase::HandshakeCtx *listen_hs = sockBase->Hs();
        AcceptorOps *listenOps = (listen_hs != nullptr) ? listen_hs->acceptor.acceptor_ops_.Get() : nullptr;
        /* 已建链 socket 的握手上下文/协商 ops 会在建链完成后释放（fd 复用可能命中旧表项），判空 */
        if (listenOps != nullptr) {
            SocketConnHelper::ExtractIpFromSockAddr(addr_tmp, listenOps->nego_peer_ip_,
                                                    sizeof(listenOps->nego_peer_ip_));
        }
    }
}

void Acceptor::TracePeerIp(int fd, const std::string &peerIp, int peerPort)
{
    if (GlobalSetting::UBS_SPLIT_TRACE_ENABLED) {
        SocketPtr sock_obj = ArraySet<Socket>::GetInstance().GetItem(fd);
        if (sock_obj != nullptr) {
            struct sockaddr_storage local_addr;
            socklen_t local_addr_len = sizeof(local_addr);
            if (getsockname(fd, (struct sockaddr *)&local_addr, &local_addr_len) == 0) {
                std::string local_ip = SocketConnHelper::ExtractIpFromSockAddr((struct sockaddr *)&local_addr);
                int local_port = SocketConnHelper::ExtractPortFromSockAddr((struct sockaddr *)&local_addr);

                UBS_VLOG_INFO("tcp accept local ip %s port %d, peer ip %s port %d, fd: %d\n", local_ip.c_str(),
                              local_port, peerIp.c_str(), peerPort, fd);
            }
        }
    }
}

void Acceptor::HandleAcceptError(const SocketPtr &sock, int fd)
{
    /*
     * 1. 若全连接队列不为空：
     * a. 正常情况下，返回非负整数的fd，tcp连接已完成，则执行DoAccept，且需要等待ub连接完成再返回，
     * b. 异常情况下，比如内存不足、文件描述符达到系统上限、客户端异常中止连接等，保持原错误码直接返回上层，由上层应用决定后续动作
     * 2. 若全连接队列为空：
     * a. fd为非阻塞，则返回-1，errno为EAGAIN/EWOULDBLOCK，保持原错误码直接返回上层
     * b. fd为阻塞，则等待直到有连接完成或者触发异常，比如被信号中断，返回-1，errno为EINTR，保持原错误码直接返回上层
     */
    if ((errno == EMFILE) || (errno == ENFILE)) {
        /* fd 耗尽是持久性硬错误：只要没有 fd 释放，连接压力越大 accept 失败越频繁，
         * 不作抑制会在高负载下持续刷屏。固定时间窗内仅打印一次，并附带被抑制的错误次数。
         * 免锁：竞态下后者只需累加计数即可，不阻塞 accept 路径。 */
        constexpr uint64_t kRatelimitNs = 1000000000ULL; // 1s
        static std::atomic<uint64_t> lastLogNs(0);
        static std::atomic<uint64_t> suppressed{0};
        const uint64_t nowNs = Func::CurrentTimeNs();
        uint64_t prev = lastLogNs.load(std::memory_order_relaxed);
        if ((nowNs - prev) >= kRatelimitNs) { // monotonic 时钟，无回绕
            if (lastLogNs.compare_exchange_strong(prev, nowNs, std::memory_order_relaxed)) {
                const uint64_t dropped = suppressed.exchange(0, std::memory_order_relaxed);
                UBS_VLOG_ERR("accept() failed: fd exhausted (ret:%d, errno:%d, errmsg:%s), %llu errors suppressed "
                             "in window, Peer IP:%s, fd:%d\n", fd, errno, Func::Error2Str(errno),
                             (unsigned long long)dropped, GetPeerIp().c_str(), sock->raw_socket_);
                return;
            }
        }
        suppressed.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        UBS_VLOG_ERR("accept() failed, Peer IP:%s, fd: %d, ret: %d, errno: %d, errmsg: %s\n", GetPeerIp().c_str(),
                     sock->raw_socket_, fd, errno, Func::Error2Str(errno));
    } else {
        UBS_VLOG_DEBUG("tcp accept need try again, fd: %d, %d, %s\n", sock->raw_socket_, errno, Func::Error2Str(errno));
    }
}

void Acceptor::SetAcceptorOps(const AcceptorOpsPtr &acceptor_ops)
{
    acceptor_ops_ = acceptor_ops;
}

// ======================== Accept 主流程辅助函数 ========================
bool Acceptor::TryPopAsyncReadyFd(int &fd, struct sockaddr *address, socklen_t *address_len)
{
    if (async_accept_ == nullptr) {
        /* 尚未进入过异步 accept 路径，必然无就绪 fd；不为查询而创建上下文 */
        return false;
    }
    Locker sLock(async_accept_->lock);
    if (!async_accept_->ready_queue.empty()) {
        auto tmp = async_accept_->ready_queue.front();
        async_accept_->ready_queue.pop();
        fd = std::get<0>(tmp);
        if (address != nullptr) {
            *address = std::get<1>(tmp);
            *address_len = std::get<2>(tmp);
        }
        UBS_VLOG_DEBUG("found ready fd, return directly, fd %d\n", fd);
        return true;
    }
    return false;
}

bool Acceptor::ProcessUBConnection(int fd, const std::string &peerIp)
{
    bool is_blocking = SocketConnHelper::IsBlocking(fd);
    if (is_blocking) {
        // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
        SocketConnHelper::SetNonBlocking(fd);
    }
    uint64_t protocol_negotiation = 0;
    ssize_t protocol_negotiation_recv_size = 0;
    int ret = acceptor_ops_->ValidateProtocol(fd, protocol_negotiation, protocol_negotiation_recv_size);
    if (ret > 0 || ret == -1) {
        UBS_VLOG_WARN("Protocol dismatch,Peer IP:%s, fd: %d\n", peerIp.c_str(), fd);
        LibcApi::close(fd);
        return false;
    }
    if (ret == 0) {
        auto err = DoAccept(fd, peerIp);
        if (!IsOk(err)) {
            // kRETRYABLE 等错误码需要特殊处理：Degradable(err)
            if (IsDegradable(err)) {
                // 降级至 TCP，客户端可正确工作，不应清理数据.
                UBS_VLOG_INFO("ubsocket is degraded to TCP, fd: %d\n", fd);
            } else {
                /* 致命失败（典型：客户端在握手中途超时离开 —— 服务端在最终 ack 交换处读到 EOF，
                 * 返回 UBS_TCP_EXCHANGE）。原实现在此“回退到 TCP/IP”后把 fd 原样交给上层，
                 * 而对端早已不在：brpc 会把这个 fd 注册为一条正常连接，可对端的 FIN 早在注册前
                 * 就到达并耗尽了唯一一次 EPOLLIN 边沿（ET），此后再无任何路径会碰这条链路——
                 * 服务端出现“对端已消失却永远处于 established”的连接。
                 * 无对端可回退，唯一正确的处理是关闭 fd 并告知调用方不要上交。
                 * DoAccept 内建的 Socket/UMQ 资源已随其局部 SocketPtr 析构释放，此处只需收 fd。 */
                UBS_VLOG_WARN("Fatal handshake error, peer IP:%s, fd: %d, err: %d — closing (peer gone or "
                              "negotiation aborted; nothing to fall back to)\n", peerIp.data(), fd, err);
                if (is_blocking) {
                    SocketConnHelper::SetBlocking(fd);
                }
                LibcApi::close(fd);
                return false;
            }
        }
    }

    if (is_blocking) {
        // reset
        SocketConnHelper::SetBlocking(fd);
    }
    return true;
}

Result Acceptor::DoAccept(int new_fd, const std::string &peerIp)
{
    PROF_START(CORE_ACCEPT);
    Result ret = UBS_OK;

    /* fd 超出 ArraySet 容量时 OverrideItem 会静默返回空引用，socket "建成功"却按 fd 查不到，
     * 直到首次 ubs_poll 才以 EPIPE 报出（issue #44：nofile 抬到 1M 后 fd 65556）。
     * 在这里显式拒绝：ERROR 日志 + errno=EMFILE + 致命错误码，调用方关 fd、不上交。 */
    const uint32_t fd_capacity = ArraySet<Socket>::GetInstance().Capacity();
    if (UNLIKELY(static_cast<uint32_t>(new_fd) >= fd_capacity)) {
        UBS_VLOG_ERR("Accept fd %d exceeds socket table capacity %u (RLIMIT_NOFILE / FD_CAPACITY_HARD_LIMIT), "
                     "rejecting UB connection from %s\n", new_fd, fd_capacity, peerIp.c_str());
        errno = EMFILE;
        PROF_END(CORE_ACCEPT, false);
        return UBS_ERROR;
    }

    // TODO；使用 Socket 工厂方法统一创建, create 增加三种create fd透传
    SocketPtr new_socket_obj;
    ret = SocketBase::Create(new_fd, SocketType::SOCK_TYPE_UMQ, new_socket_obj);
    if (ret != UBS_OK) {
        PROF_END(CORE_ACCEPT, false);
        return ret;
    }

    auto newSocket = RefConvert<Socket, SocketBase>(new_socket_obj);
    if (RawConnInfoV4 *ci = newSocket->MutableConnInfo()) {
        ci->SetPeerIp(peerIp.c_str());
        ci->peer_fd = new_fd;
        ci->type_fd = 0;
    }

    /* 注册必须先于协商：方案B 下应答（含 server_bind_ret）在 Negotiate 内部就已
     * 发出，客户端 3 消息握手完成后立刻发首包——共享 JFR runner 按 fd 经 ArraySet
     * 解析归属，若此时还没 OverrideItem，首包 CQE 被按"无主"吞掉（消费不投递），
     * 客户端首个 RPC 静默丢失直至超时。经典/A' 路径客户端必须等到服务端进入
     * CreateSocketResources（注册之后）才能完成建链，天然无此窗口；把注册提到
     * 协商之前对它们无行为变化（协商期间无 UMQ、无 epoll 挂接，条目惰性）。 */
    ArraySet<Socket>::GetInstance().OverrideItem(new_fd, new_socket_obj.Get());
    ret = newSocket->Hs()->acceptor.acceptor_ops_->Negotiate(new_socket_obj);
    if (ret != UBS_OK) {
        /* 方案A'/B 在应答前就建了 umq：协商失败当场还掉它的 id（issue #49） */
        newSocket->DiscardUnboundUmq();
        ArraySet<Socket>::GetInstance().OverrideItem(new_fd, nullptr);
        PROF_END(CORE_ACCEPT, false);
        return ret;
    }
    ret = newSocket->Hs()->acceptor.acceptor_ops_->CreateSocketResources(new_socket_obj);
    if (ret != UBS_OK) {
        /* 先清数据面槽位，再从 ArraySet 摘除：OverrideItem 会把最后一个引用交给
         * 延迟释放队列，~UmqSocket 要等 reaper 才跑，而 fd 在那之前就会被关闭并
         * 可能被内核复用——不在这里清槽，新 socket 就会撞见"仍归旧 socket"的条目。 */
        newSocket->ReleaseDataPlane();
        newSocket->DiscardUnboundUmq();
        ArraySet<Socket>::GetInstance().OverrideItem(new_fd, nullptr);
        PROF_END(CORE_ACCEPT, false);
        return ret;
    }

    new_socket_obj->create_type_ = SOCK_CREATE_TYPE_ACCEPT;
    if (RawConnInfoV4 *ci = newSocket->MutableConnInfo()) {
        ci->SetCreateTimeNow();
    }
    /* 建链完成：身份信息已在 conn_info_ 上（不再回写 ops 的副本），
     * 钩子内收拢 EID 快照并释放协商 ops */
    newSocket->OnEstablished();

    if (GlobalSetting::UBS_MONITOR_ENABLE) {
        if (auto *mgr = newSocket->GetStatsMgr()) {
            mgr->UpdateTraceStats(Statistics::StatsMgr::CONN_COUNT, 1);
        }
    }
    //TODO: 优化建链成功的打印日志
    UBS_VLOG_DEBUG("UB connection has been successfully established new fd: %d\n", new_fd);
    PROF_END(CORE_ACCEPT, true);

    return UBS_OK;
}

// ======================== 异步 Accept 唤醒初始化 ========================
void Acceptor::InitWakeupEvent()
{
    /* Per listening socket. This used to be a process-wide once that initialised the caller's own
     * wakeup_event, so only the first listener ever got an eventfd and every later port's completed
     * handshakes waited in ready_queue for the next SYN (issue #50). Retried at the next Accept()
     * on failure. bthread 上禁用 std::call_once，原因见 ubsocket_leaky_singleton.h::Instance 的注释。 */
    AsyncAcceptInfo *asyncCtx = async_accept_.get();
    if (asyncCtx == nullptr || asyncCtx->wakeup_ready.load(std::memory_order_acquire)) {
        return;
    }
    Locker sLock(asyncCtx->lock);
    if (asyncCtx->wakeup_ready.load(std::memory_order_relaxed)) {
        return;
    }
    // 线程池已前移到ubsocket.cpp 初始化阶段；这里只做 wakeup_event：获取 epoll_fd，注册 eventfd 进 epoll
    EpollMapper *mapper = GetSocketEpollMapper(raw_fd_);
    if (mapper == nullptr) {
        UBS_VLOG_ERR("async accept: mapper==nullptr, fd=%d\n", raw_fd_);
        return;
    }
    int epoll_fd = mapper->QueryFirst();
    if (epoll_fd < 0) {
        UBS_VLOG_ERR("async accept: epoll_fd<0, fd=%d\n", raw_fd_);
        return;
    }
    if (asyncCtx->wakeup_event.Initialize(epoll_fd) != 0) {
        UBS_VLOG_ERR("async accept: wakeup_event init failed, epoll_fd=%d, listen_fd=%d\n", epoll_fd, raw_fd_);
        return;
    }
    asyncCtx->wakeup_event.SetListenFd(raw_fd_);
    EventPollPtr aepRef = ArraySet<EventPoll>::GetInstance().GetItem(epoll_fd);
    auto *aep = (AsyncEventPoll *)aepRef.Get();
    if (aep == nullptr) {
        UBS_VLOG_ERR("async accept: failed to get AsyncEventPoll for epoll_fd: %d\n", epoll_fd);
        return; /* Initialize() is idempotent; registration is retried next time */
    }
    aep->SetWakeupCallback(asyncCtx->wakeup_event.GetReadyEvent(),
                           [asyncCtx](struct epoll_event *ev, int me, std::unordered_map<int, EpollEvent *> &sd) {
                               return asyncCtx->wakeup_event.ProcessReadyEvents(ev, me, sd);
                           });
    asyncCtx->wakeup_ready.store(true, std::memory_order_release);
    UBS_VLOG_DEBUG("async accept: wakeup registered, epoll_fd=%d, listen_fd=%d\n", epoll_fd, raw_fd_);
}

int Acceptor::Listen(int backlog)
{
    return 0;
}

Acceptor::~Acceptor()
{
    /* CONN_COUNT 的递减已统一收敛到 SocketBase 析构，此处不再单独递减。 */
    /* async_accept_ 上下文（含锁、队列排空与 wakeup 事件）由 unique_ptr 析构统一释放 */
}
} // namespace ubs
} // namespace ock