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
#include "ubsocket_socket_set.h"

namespace ock {
namespace ubs {
// ======================== 基础方法 ========================
int Acceptor::Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len)
{
#ifdef ENABLED
    int fd = -1;
    auto sock_obj = reinterpret_cast<Socket *>(sock_obj);
    raw_fd_ = sock_obj->raw_socket_;

    // 1. 异步accept模式下，从ready_queue中取fd
    if (GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED && TryPopAsyncReadyFd(fd, address, address_len)) {
        return fd;
    }

    // 2. 同步accept模式下，TCP 建链，返回fd
    struct sockaddr addr_tmp;
    socklen_t len_tmp;
    fd = LibcApi::accept(raw_fd_, &addr_tmp, &len_tmp);
    if (fd >= 0) {
        // 前置判断，如果不是TFO连接，作为普通TCP连接处理
        if (!SocketConnHelper::IsTfoConnection(fd)) {
            return fd;
        }
        int tcpNoDelayRet = SocketConnHelper::SetTcpNoDelay(fd);
        if (tcpNoDelayRet != 0) {
            // Log
        }
    }

    std::string peerIp;
    if (fd >= 0 && address != nullptr) {
        *address = addr_tmp;
        *address_len = len_tmp;
        // 使用提取的接口获取IP地址
        peerIp = SocketConnHelper::ExtractIpFromSockAddr(address);

        Socket *sock_obj = SocketSet::GetInstance().GetSocket(fd);
        if (sock_obj) {
            sock_obj->GetAcceptor()->RawConnInfoV4.peer_ip = peerIp;
            sock_obj->GetAcceptor()->RawConnInfoV4.peer_fd = fd;
            sock_obj->GetAcceptor()->RawConnInfoV4.create_time = std::chrono::system_clock::now();
        }
    }
    if (sock_obj.GetSocketState() == SOCK_STAT_RAW_ESTABLISHED) {
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
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            // Log
            return fd;
        }
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        // RPC_ADPT_VLOG_DEBUG
        return fd;
    }

    // 异步/同步
    // TODO: 异步和同步的分两个函数
    if (GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED) {
        // 待补充异步实现，待完善线程池函数
    } else {
        ProcessUBConnection(fd, peerIp);
        return fd;
    }
#endif
    return 0;
}

void Acceptor::SetAcceptorOps(const AcceptorOpsPtr &acceptor_ops)
{
    acceptor_ops_ = acceptor_ops;
}

// ======================== Accept 主流程辅助函数 ========================
bool Acceptor::TryPopAsyncReadyFd(int &fd, struct sockaddr *address, socklen_t *address_len)
{
#ifdef ENABLED
    Locker sLock(AsyncAcceptInfo.lock);
    if (!AsyncAcceptInfo.ready_queue.empty()) {
        auto tmp = AsyncAcceptInfo.ready_queue.front();
        AsyncAcceptInfo.ready_queue.pop();
        fd = std::get<0>(tmp);
        if (address != nullptr) {
            *address = std::get<1>(tmp);
            *address_len = std::get<2>(tmp);
        }
        UBS_VLOG_DEBUG("found ready fd, return directly, fd %d\n", fd);
        return fd;
    }
#endif
    return true;
}

void Acceptor::ProcessUBConnection(int fd, const std::string &peerIp)
{
#ifdef ENABLED
    bool is_blocking = SocketConnHelper::IsBlocking(fd);
    if (is_blocking) {
        // set non_blocking to apply timeout by chrono(send/recv can be returned immediately)
        SocketConnHelper::SetNonBlocking(fd);
    }

    uint64_t protocol_negotiation = 0;
    ssize_t protocol_negotiation_recv_size = 0;
    int ret = acceptor_ops_->ValidateProtocol(fd, protocol_negotiation, protocol_negotiation_recv_size);

    if (ret > 0 && !GlobalSetting::UBS_AUTO_FALLBACK_TCP) {
        // RPC_ADPT_VLOG_ERR
        LibcApi::close(fd);
        return;
    }
    if (ret > 0) {
        LibcApi::close(fd);
    } else if (ret == 0) {
        auto err = DoAccept(fd, peerIp);
        if (!IsOk(err)) {
            // kRETRYABLE 等错误码需要特殊处理：Degradable(err)
            if (err) {
                // 降级至 TCP，客户端可正确工作，不应清理数据.
            } else {
                // RPC_ADPT_VLOG_WARN
                // Clear messages that already exist on the TCP link to prevent
                // dirty messages from affecting user data transmission
                FlushSocketMsg(fd);
            }
        }
    }

    if (is_blocking) {
        // reset
        SocketConnHelper::SetBlocking(fd);
    }
#endif
}

Result Acceptor::DoAccept(int new_fd, const std::string &peerIp)
{
#ifdef ENABLED
    Result ret = UBS_OK;
    // TODO: event_fd 应该放在哪里
    int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd < 0) {
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        // RPC_ADPT_VLOG_ERR
        return UBS_NEW_SOCKET_FD;
    }
    event_fd_ = event_fd;

    // TODO；使用 Socket 工厂方法统一创建, create 增加三种create
    UmqSocketPtr new_socket_obj;
    ret = Socket::Create(SOCK_TYPE_UMQ, new_socket_obj);
    if (ret != UBS_OK) {
        return ret;
    }

    auto acceptor_ops = std::make_shared<umq::UmqSocketAcceptorOps>();
    Acceptor *acceptor = new Acceptor();
    try {
        new_socket_obj->umq_acceptor_ops_ = acceptor_ops;
        acceptor->SetAcceptorOps(acceptor_ops);
        new_socket_obj->SetAcceptor(acceptor);
        acceptor_ops->SetConnInfo(peerIp, new_fd, 0);
    } catch (std::exception &e) {
        LibcApi::close(event_fd);
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "%s\n", e.what());
        return Error::kUBSOCKET_NEW_SOCKET_FD;
    }
    auto sockCleaner = ubsocket::MakeScopeExit([new_socket_obj]() { delete new_socket_obj; });

    ret = acceptor_ops->Negotiate(new_fd, new_socket_obj);
    if (ret != UBS_OK) {
        return ret;
    }
    ret = acceptor_ops->CreateSocketResources(new_fd, new_socket_obj);
    if (ret != UBS_OK) {
        return ret;
    }

    // Delete existing objects and record new objects in the list.
    sockCleaner.Deactivate();

    Socket *sock_new = SocketSet::GetInstance().OverrideSocket(new_fd, new_socket_obj);

    // TODO: 待 PoollingEpoll 重构后修改这部分

    socket_fd_obj->umq_acceptor_ops_->RawConnInfoV4.create_time = std::chrono::system_clock::now();
    // RPC_ADPT_VLOG_INFO  "UB connection has been successfully established new fd:
    // RawConnInfoV4.type_fd = 0;
#endif
    return UBS_OK;
}

int Acceptor::Listen(int backlog)
{
    return 0;
}

Acceptor::Acceptor() {}

Acceptor::~Acceptor() {}
} // namespace ubs
} // namespace ock
