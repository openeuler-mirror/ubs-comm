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
#include "profiling/statistics/statistics_statsmgr.h"
#include "common/ubsocket_thread_pool.h"
#include "ubsocket_core_types.h"
#include "ubsocket_event_epoll.h"
#include "ubsocket_socket_set.h"
#include "ubsocket_wakeup_event.h"
#include "umq/umq_socket.h"

namespace ock {
namespace ubs {
// ======================== 基础方法 ========================
int Acceptor::Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
    int fd = -1;
    // 1. 异步accept模式下，从ready_queue中取fd
    if (GlobalSetting::AsyncAcceptorEnabled() && TryPopAsyncReadyFd(fd, address, address_len)) {
        return fd;
    }
    // 2. 同步accept模式下，TCP 建链，返回fd
    struct sockaddr addr_tmp;
    socklen_t len_tmp = sizeof(addr_tmp); // 初始化 addrlen
    fd = LibcApi::accept(raw_fd_, &addr_tmp, &len_tmp);
    if (fd >= 0) {
        // 前置判断，如果不是TFO连接，作为普通TCP连接处理
        if (!SocketConnHelper::IsUbsConnection(fd)) {
            return fd;
        }
        int tcpNoDelayRet = SocketConnHelper::SetTcpNoDelay(fd);
        if (tcpNoDelayRet != 0) {
            UBS_VLOG_WARN("Set TCP_NODELAY failed, fd %d, ret %d, errno %d\n", fd, tcpNoDelayRet, errno);
        }
    }
    std::string peerIp;
    if (fd >= 0 && address != nullptr) {
        *address = addr_tmp;
        *address_len = len_tmp;
        // 使用提取的接口获取IP地址
        peerIp = SocketConnHelper::ExtractIpFromSockAddr(address);
        SocketPtr sock_obj = SocketSet::Instance().GetSocket(fd);
        if (sock_obj != nullptr) {
            auto sockBase = RefConvert<Socket, SocketBase>(sock_obj);
            sockBase->acceptor_->acceptor_ops_->conn_info.peer_ip = peerIp;
            sockBase->acceptor_->acceptor_ops_->conn_info.peer_fd = fd;
            sockBase->acceptor_->acceptor_ops_->conn_info.create_time = std::chrono::system_clock::now();
        }
    }
    if (sock->State() == SOCK_STAT_RAW_ESTABLISHED) {
        return fd;
    }
    if (fd < 0) {
        /*
         * 1. 若全连接队列不为空：
         * a. 正常情况下，返回非负整数的fd，tcp连接已完成，则执行DoAccept，且需要等待ub连接完成再返回，
         * b. 异常情况下，比如内存不足、文件描述符达到系统上限、客户端异常中止连接等，保持原错误码直接返回上层，由上层应用决定后续动作
         * 2. 若全连接队列为空：
         * a. fd为非阻塞，则返回-1，errno为EAGAIN/EWOULDBLOCK，保持原错误码直接返回上层
         * b. fd为阻塞，则等待直到有连接完成或者触发异常，比如被信号中断，返回-1，errno为EINTR，保持原错误码直接返回上层
         */
        if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) { // nonblocking
            UBS_VLOG_ERR("accept() failed, Peer IP:%s, fd: %d, ret: %d, errno: %d, errmsg: %s\n", GetPeerIp().c_str(),
                         sock->raw_socket_, fd, errno, Func::Error2Str(errno));
            return fd;
        }
        UBS_VLOG_DEBUG("tcp accept need try again, fd: %d, %d, %s\n", sock->raw_socket_, errno, Func::Error2Str(errno));
        return fd;
    }
    // 异步/同步
    if (GlobalSetting::AsyncAcceptorEnabled()) {
        // 懒初始化：启动 ExecutorService + 初始化 wakeup_event_
        InitWakeupEvent();
        UBS_VLOG_INFO("async accept execute. fd:%d", fd);
        auto exec_ret = ExecutorService::GetExecutorService()->Execute([this, fd, addr_tmp, len_tmp]() {
            UBS_VLOG_INFO("async accept start. fd:%d\n", fd);
            std::string ip = SocketConnHelper::ExtractIpFromSockAddr(&addr_tmp);
            ProcessUBConnection(fd, ip);
            {
                Locker sLock(ubSocket_async_accept_info.lock);
                ubSocket_async_accept_info.ready_queue.push(std::make_tuple(fd, addr_tmp, len_tmp));
            }
            // 直接调用 WakeUpReadyEventFd() 写 eventfd，触发 epoll_wait 返回
            wakeup_event_.WakeUpReadyEventFd(raw_fd_);
            ubSocket_async_accept_info.asyncTaskNum.fetch_sub(1U);
            UBS_VLOG_INFO("async accept success. fd:%d\n", fd);
        });
        UBS_VLOG_INFO("Execute end. exec_ret:%d\n", exec_ret);
        if (exec_ret == true) {
            ubSocket_async_accept_info.asyncTaskNum.fetch_add(1U);
        } else {
            UBS_VLOG_DEBUG("submit async accept task failed, use sync accept. fd:%d\n", fd);
            ProcessUBConnection(fd, peerIp);
            return fd;
        }

        errno = EAGAIN;
        return -1;
    } else {
        if (peerIp.length() == 0) {
            peerIp = SocketConnHelper::ExtractIpFromSockAddr(&addr_tmp);
        }
        ProcessUBConnection(fd, peerIp);
        return fd;
    }
}

void Acceptor::SetAcceptorOps(const AcceptorOpsPtr &acceptor_ops)
{
    acceptor_ops_ = acceptor_ops;
}

// ======================== Accept 主流程辅助函数 ========================
bool Acceptor::TryPopAsyncReadyFd(int &fd, struct sockaddr *address, socklen_t *address_len)
{
    Locker sLock(ubSocket_async_accept_info.lock);
    if (!ubSocket_async_accept_info.ready_queue.empty()) {
        auto tmp = ubSocket_async_accept_info.ready_queue.front();
        ubSocket_async_accept_info.ready_queue.pop();
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

void Acceptor::ProcessUBConnection(int fd, const std::string &peerIp)
{
    bool is_blocking = SocketConnHelper::IsBlocking(fd);
    if (is_blocking) {
        // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
        SocketConnHelper::SetNonBlocking(fd);
    }
    uint64_t protocol_negotiation = 0;
    ssize_t protocol_negotiation_recv_size = 0;
    int ret = acceptor_ops_->ValidateProtocol(fd, protocol_negotiation, protocol_negotiation_recv_size);
    if (ret > 0 && !GlobalSetting::UBS_AUTO_FALLBACK_TCP) {
        UBS_VLOG_ERR("Failed to accept as protocol dismatch,Peer IP:%s\n", GetPeerIp().c_str());
        LibcApi::close(fd);
        return;
    }
    if (ret > 0) {
        LibcApi::close(fd);
    } else if (ret == 0) {
        auto err = DoAccept(fd, peerIp);
        if (!IsOk(err)) {
            // kRETRYABLE 等错误码需要特殊处理：Degradable(err)
            if (IsDegradable(err)) {
                // 降级至 TCP，客户端可正确工作，不应清理数据.
                UBS_VLOG_INFO("ubsocket is degraded to TCP.\n");
            } else {
                UBS_VLOG_WARN("Fatal error occurred,Peer IP:%s, fd: %d fallback to TCP/IP\n", peerIp.data(), fd);
                // Clear messages that already exist on the TCP link to prevent
                // dirty messages from affecting user data transmission
                SocketConnHelper::FlushSocketMsg(fd);
            }
        }
    }

    if (is_blocking) {
        // reset
        SocketConnHelper::SetBlocking(fd);
    }
}

Result Acceptor::DoAccept(int new_fd, const std::string &peerIp)
{
    PROF_START(CORE_ACCEPT);
    Result ret = UBS_OK;
    int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd < 0) {
        UBS_VLOG_ERR("eventfd() failed, ret: %d, errno: %d, errmsg: %s\n", event_fd, errno, Func::Error2Str(errno));
        PROF_END(CORE_ACCEPT, false);
        return UBS_NEW_SOCKET_FD;
    }

    // TODO；使用 Socket 工厂方法统一创建, create 增加三种create fd透传
    SocketPtr new_socket_obj;
    ret = SocketBase::Create(new_fd, SocketType::SOCK_TYPE_UMQ, new_socket_obj);
    if (ret != UBS_OK) {
        PROF_END(CORE_ACCEPT, false);
        return ret;
    }
    new_socket_obj->event_fd_ = event_fd;
    auto newSocket = RefConvert<Socket, SocketBase>(new_socket_obj);
    newSocket->acceptor_->acceptor_ops_->event_fd = event_fd;
    newSocket->acceptor_->acceptor_ops_->conn_info.peer_ip = peerIp;
    newSocket->acceptor_->acceptor_ops_->conn_info.peer_fd = new_fd;
    newSocket->acceptor_->acceptor_ops_->conn_info.type_fd = 0;

    ret = newSocket->acceptor_->acceptor_ops_->Negotiate(new_socket_obj);
    if (ret != UBS_OK) {
        PROF_END(CORE_ACCEPT, false);
        return ret;
    }
    ret = newSocket->acceptor_->acceptor_ops_->CreateSocketResources(new_socket_obj);
    if (ret != UBS_OK) {
        PROF_END(CORE_ACCEPT, false);
        return ret;
    }

    SocketSet::Instance().OverrideSocket(new_fd, new_socket_obj);

    newSocket->acceptor_->acceptor_ops_->conn_info.create_time = std::chrono::system_clock::now();

    if (GlobalSetting::UBS_TRACE_ENABLED) {
        umq::UmqSocketPtr sockptr = RefConvert<SocketBase, umq::UmqSocket>(newSocket);
        sockptr->stats_mgr_.UpdateTraceStats(Statistics::StatsMgr::CONN_COUNT, 1);
    }
    //TODO: 优化建链成功的打印日志
    UBS_VLOG_INFO("UB connection has been successfully established new fd: %d\n", new_fd);
    PROF_END(CORE_ACCEPT, true);

    return UBS_OK;
}

// ======================== 异步 Accept 唤醒初始化 ========================
void Acceptor::InitWakeupEvent()
{
    static std::once_flag initFlag;
    std::call_once(initFlag, [this]() {
        // 1. 启动 ExecutorService 线程池
        auto exec_service = ExecutorService::GetExecutorService();
        if (!exec_service->Start()) {
            UBS_VLOG_ERR("Failed to start ExecutorService for async accept");
        }

        // 2. 初始化 wakeup_event_：获取 epoll_fd，注册 eventfd 进 epoll
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

        wakeup_event_.Initialize(epoll_fd);
        wakeup_event_.SetListenFd(raw_fd_);
        UBS_VLOG_INFO("async accept: wakeup_event_ init, epoll_fd=%d, listen_fd=%d\n", epoll_fd, raw_fd_);

        auto *aep = (AsyncEventPoll *)ArraySet<EventPoll>::GetInstance().GetItem(epoll_fd);
        if (aep != nullptr) {
            aep->SetWakeupCallback(wakeup_event_.GetReadyEvent(),
                                   [this](struct epoll_event *ev, int me, std::unordered_map<int, EpollEvent *> &sd) {
                                       return wakeup_event_.ProcessReadyEvents(ev, me, sd);
                                   });
            UBS_VLOG_INFO("async accept: SetWakeupCallback() called for epoll_fd: %d\n", epoll_fd);
        } else {
            UBS_VLOG_ERR("async accept: failed to get AsyncEventPoll for epoll_fd: %d\n", epoll_fd);
        }
    });
}

int Acceptor::Listen(int backlog)
{
    return 0;
}

Acceptor::~Acceptor()
{
    if (GlobalSetting::UBS_TRACE_ENABLED) {
        Statistics::StatsMgr::SubMConnCount();
    }
}
} // namespace ubs
} // namespace ock
