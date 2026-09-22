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
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_data_tx.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "include/ubsocket.h"

using namespace ock::ubs;

// ===== Public C API for UB degradation support =====

UBS_API int ubsocket_is_ub_transport(int fd)
{
    if (fd < 0 || GlobalSetting::UBS_NATIVE_TCP_MODE || !GlobalSetting::UBS_INITED) {
        return -1;
    }
    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (sock == nullptr) {
        return 0; // not in ArraySet → TCP (degraded or plain TCP)
    }
    return 1; // in ArraySet → UB transport active
}

UBS_API int ubsocket_set_degrade_enable(int enable)
{
    // Set directly even if UBS_INITED is false, so that brpc can set
    // the flag before ubsocket_init() runs (GlobalInitialize may be
    // deferred). LoadEnv will not override this if the env var is
    // not set.
    GlobalSetting::UBS_ENABLE_DEGRADE = (enable != 0);
    return 0;
}

UBS_API int UB_API_WRAP(socket)(int domain, int type, int protocol)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::socket(domain, type, protocol);
    }
    int fd;
    if (domain == AF_SMC) {
        fd = LibcApi::socket(AF_INET, type, protocol);
    } else {
        return LibcApi::socket(domain, type, protocol);
    }
    if (fd < 0) {
        return fd;
    }
    /* 同 Acceptor::DoAccept：fd 超出 ArraySet 容量时登记会静默失败（issue #44），这里按 EMFILE 明确拒绝 */
    const uint32_t fd_capacity = ArraySet<Socket>::GetInstance().Capacity();
    if (UNLIKELY(static_cast<uint32_t>(fd) >= fd_capacity)) {
        UBS_VLOG_ERR("socket() fd %d exceeds socket table capacity %u (RLIMIT_NOFILE / FD_CAPACITY_HARD_LIMIT)\n", fd,
                     fd_capacity);
        LibcApi::close(fd);
        errno = EMFILE;
        return -1;
    }
    SocketPtr socketPtr;
    Result ret = SocketBase::Create(fd, SocketType::SOCK_TYPE_UMQ, socketPtr);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("CreateSocketFd() failed, fd: %d, ret: %d\n", fd, ret);
        LibcApi::close(fd);
        return -1;
    }
    ArraySet<Socket>::GetInstance().OverrideItem(fd, socketPtr.Get());
    return fd;
}

UBS_API int UB_API_WRAP(shutdown)(int fd, int how)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::shutdown(fd, how);
    }

    return LibcApi::shutdown(fd, how);
}

UBS_API int UB_API_WRAP(close)(int fd)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::close(fd);
    }
    bool need_shutdown = false;
    {
        SocketPtr close_sock = ArraySet<Socket>::GetInstance().GetItem(fd);
        // 跳过 listen fd：shutdown 后 accept() 持续返回 EINVAL，brpc accept 循环会忙循环；且 listen fd 无 writev。
        need_shutdown = (close_sock != nullptr && close_sock->create_type_ != SOCK_CREATE_TYPE_LISTEN);
    }
    if (need_shutdown) {
        // 关闭窗口：OverrideItem 移除 ArraySet 后，socket 析构会执行 UMQ 拆除(毫秒级)，期间 ArraySet
        // 已空但 fd 仍开，并发 writev 会回退到原生 TCP 把真实数据发到对端(对端 UMQ 模式读不到，导致
        // 残留数据堵住 FIN 检测、socket 泄漏)。先 shutdown(不释放 fd 号、不改变拆除顺序)使该窗口内
        // writev 拿到 EPIPE，并立即向对端发 FIN。shutdown 仅置状态，fd 在 UMQ 拆除期间仍有效。
        LibcApi::shutdown(fd, SHUT_RDWR);
    }
    /* OverrideItem hands back the ArraySet's ref — for a socket brpc has already
     * deregistered (RemoveConsumer precedes close) that is the LAST ref, and
     * dropping it here would run ~UmqSocket's UB teardown (unbind + up to
     * UMQ_DESTROY_FLUSH_TIMEOUT_MS of CQE drain + destroy) inline on this —
     * typically a brpc worker — thread. Hand it to the reaper instead; the
     * fd itself is closed right away. If the reaper is not running, the ref
     * drops at scope exit → inline teardown exactly as before. */
    SocketPtr last_ref = ArraySet<Socket>::GetInstance().OverrideItem(fd, nullptr);
    if (last_ref != nullptr && last_ref->create_type_ != SOCK_CREATE_TYPE_LISTEN) {
        /* The fd goes with the socket: the reaper closes it only after the
         * UMQ is destroyed, so the kernel cannot recycle this number for a
         * new link while the old UMQ (umq_ctx == fd) is still alive and its
         * completions could be misrouted to the newcomer. shutdown() above
         * already sent FIN and blocks any further I/O on the number. */
        if (TxCqePoller::Instance().RetireSocket(fd, std::move(last_ref))) {
            return 0; /* parked: reaper owns fd + socket from here */
        }
        /* reaper not running: last_ref (if still held) drops at scope exit → inline teardown */
    }
    return close(fd);
}

UBS_API int UB_API_WRAP(accept)(int fd, struct sockaddr *address, socklen_t *address_len)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::accept(fd, address, address_len);
    }

    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::accept(fd, address, address_len);
    }

    return sockBase->Accept(sock, address, address_len);
}

UBS_API int UB_API_WRAP(accept4)(int fd, struct sockaddr *address, socklen_t *address_len, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::accept4(fd, address, address_len, flags);
    }

    return 0;
}

UBS_API int UB_API_WRAP(bind)(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    UBS_VLOG_INFO("Binding to IP: %s, Port: %d\n", SocketConnHelper::ExtractIpFromSockAddr(addr).c_str(),
                  SocketConnHelper::ExtractPortFromSockAddr(addr));
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::bind(fd, addr, addrlen);
    }

    return LibcApi::bind(fd, addr, addrlen);
}

UBS_API int UB_API_WRAP(listen)(int fd, int backlog)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::listen(fd, backlog);
    }

    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (sock == nullptr) {
        return LibcApi::listen(fd, backlog);
    }
    sock->create_type_ = SOCK_CREATE_TYPE_LISTEN;
    UBHandshakeMode ubHandshakeMode = GlobalSetting::UBS_HAND_SHAKE_MODE;
    if (ubHandshakeMode == UBHandshakeMode::UB_SOCK_OPT) {
        UBS_VLOG_INFO("Enable ub handshake option\n");
        int opt = 1;
        if (LibcApi::setsockopt(fd, IPPROTO_TCP, TCP_UB_SOCKET_HANDSHAKE, &opt, sizeof(opt)) == 0) {
            return LibcApi::listen(fd, backlog);
        }
        UBS_VLOG_WARN("Unable to enable ub handshake option. Handshake mode fallback to TFO.\n");
        GlobalSetting::UBS_HAND_SHAKE_MODE = UBHandshakeMode::TFO;
        ubHandshakeMode = UBHandshakeMode::TFO;
    }
    if (ubHandshakeMode == UBHandshakeMode::TFO) {
        UBS_VLOG_INFO("Enable Server TFO, with QLen %d\n", backlog);
        // enable tfo
        if (LibcApi::setsockopt(fd, SOL_TCP, TCP_FASTOPEN, &backlog, sizeof(backlog)) < 0) {
            UBS_VLOG_WARN("Unable to enable server TFO.\n");
        }
    }
    return LibcApi::listen(fd, backlog);
}

UBS_API int UB_API_WRAP(connect)(int fd, const struct sockaddr *address, socklen_t address_len)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::connect(fd, address, address_len);
    }
    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    if (sock == nullptr) {
        return LibcApi::connect(fd, address, address_len);
    }
    sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    return sockBase->Connect(sock, address, address_len);
}

UBS_API ssize_t UB_API_WRAP(readv)(int fd, const struct iovec *iov, int iovcnt)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::readv(fd, iov, iovcnt);
    }
    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::readv(fd, iov, iovcnt);
    }
    return sockBase->ReadV(sock, iov, iovcnt);
}

UBS_API ssize_t UB_API_WRAP(writev)(int fd, const struct iovec *iov, int iovcnt)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::writev(fd, iov, iovcnt);
    }
    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::writev(fd, iov, iovcnt);
    }
    return sockBase->WriteV(sock, iov, iovcnt);
}

UBS_API ssize_t UB_API_WRAP(send)(int fd, const void *buf, size_t len, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::send(fd, buf, len, flags);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(recv)(int fd, void *buf, size_t len, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recv(fd, buf, len, flags);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(read)(int fd, void *buf, size_t nbyte)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::read(fd, buf, nbyte);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(write)(int fd, const void *buf, size_t nbyte)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::write(fd, buf, nbyte);
    }
    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendto)(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                                    socklen_t addrlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::sendto(fd, buf, len, flags, dest_addr, addrlen);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(recvfrom)(int fd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                                      socklen_t *addrlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recvfrom(fd, buf, len, flags, dest_addr, addrlen);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendmsg)(int fd, const struct msghdr *msg, int flags)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(recvmsg)(int fd, struct msghdr *msg, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recvmsg(fd, msg, flags);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendfile)(int out_fd, int in_fd, off_t *offset, size_t count)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::sendfile64(out_fd, in_fd, offset, count);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::sendfile64(out_fd, in_fd, offset, count);
    }

    return 0;
}

UBS_API int UB_API_WRAP(fcntl)(int fd, int cmd, ...)
{
    return 0;
}

UBS_API int UB_API_WRAP(fcntl64)(int fd, int cmd, ...)
{
    return 0;
}

UBS_API int UB_API_WRAP(ioctl)(int fd, unsigned long request, ...)
{
    return 0;
}

UBS_API int UB_API_WRAP(setsockopt)(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::setsockopt(fd, level, optname, optval, optlen);
    }

    /* design §4.2: intercept SOL_UB level options (e.g. UBS_OPT_RPC_TIMEOUT_MS)
     * and route them to the SocketBase::SetSockOpt handler. Lower levels fall
     * through to the libc setsockopt. */
    if (level >= static_cast<int>(UbsocketLevel::SOL_UB)) {
        SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
        auto sockBase = RefConvert<Socket, SocketBase>(sock);
        if (sockBase == nullptr) {
            errno = ENOTSOCK;
            return -1;
        }
        return sockBase->SetSockOpt(fd, level, optname, optval, optlen);
    }

    return LibcApi::setsockopt(fd, level, optname, optval, optlen);
}

UBS_API int UB_API_WRAP(getsockopt)(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::getsockopt(fd, level, optname, optval, optlen);
    }

    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::getsockopt(fd, level, optname, optval, optlen);
    }

    if (level < static_cast<int>(UbsocketLevel::SOL_UB)) {
        return LibcApi::getsockopt(fd, level, optname, optval, optlen);
    }

    return sockBase->GetSockOpt(fd, level, optname, optval, optlen);
}
