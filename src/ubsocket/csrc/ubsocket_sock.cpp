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
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstdarg>
#include <sys/socket.h>
#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_data_tx.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_helper.h"
#include "include/ubsocket.h"
#include "profiling/ubsocket_prof.h"

using namespace ock::ubs;
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
    int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd < 0) {
        UBS_VLOG_ERR("eventfd() failed, ret: %d, errno: %d, errmsg: %s\n", event_fd, errno, Func::Error2Str(errno));
        LibcApi::close(fd);
        return -1;
    }
    SocketPtr socketPtr;
    Result ret = SocketBase::Create(fd, SocketType::SOCK_TYPE_UMQ, socketPtr);
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("CreateSocketFd() failed, fd: %d, event fd: %d, ret: %d\n", fd, event_fd, ret);
        LibcApi::close(fd);
        LibcApi::close(event_fd);
        return -1;
    }
    socketPtr->event_fd_ = event_fd;
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
    ArraySet<Socket>::GetInstance().OverrideItem(fd, nullptr);
    /* Must call LibcApi::close instead of bare close() to avoid recursion
     * when libubsocket.so is loaded via LD_PRELOAD (close symbol is intercepted). */
    return LibcApi::close(fd);
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

    /* Reuse accept logic. SOCK_NONBLOCK/SOCK_CLOEXEC flags are handled by
     * SocketBase::Accept path via setsockopt/fcntl on the accepted fd. */
    int newFd = UB_API_WRAP(accept)(fd, address, address_len);
    if (newFd < 0) {
        return newFd;
    }
    if (flags & SOCK_CLOEXEC) {
        LibcApi::fcntl(newFd, F_SETFD, FD_CLOEXEC);
    }
    if (flags & SOCK_NONBLOCK) {
        int cur = LibcApi::fcntl(newFd, F_GETFL, 0);
        LibcApi::fcntl(newFd, F_SETFL, cur | O_NONBLOCK);
    }
    return newFd;
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
    /* API 边界点位: 含 fd->Socket 查表, 与内层 CORE_READ 对比可分离拦截层开销 */
    PROF_START(CORE_API_READV);
    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::readv(fd, iov, iovcnt);
    }
    ssize_t ret = sockBase->ReadV(sock, iov, iovcnt);
    PROF_END(CORE_API_READV, ret >= 0);
    return ret;
}

UBS_API ssize_t UB_API_WRAP(writev)(int fd, const struct iovec *iov, int iovcnt)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::writev(fd, iov, iovcnt);
    }
    /* API 边界点位: 含 fd->Socket 查表, 与内层 CORE_WRITE 对比可分离拦截层开销 */
    PROF_START(CORE_API_WRITEV);
    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::writev(fd, iov, iovcnt);
    }
    ssize_t ret = sockBase->WriteV(sock, iov, iovcnt);
    PROF_END(CORE_API_WRITEV, ret >= 0);
    return ret;
}

UBS_API ssize_t UB_API_WRAP(send)(int fd, const void *buf, size_t len, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::send(fd, buf, len, flags);
    }
    /* For connected UB sockets, send is equivalent to write (flags ignored). */
    (void)flags;
    return UB_API_WRAP(write)(fd, buf, len);
}

UBS_API ssize_t UB_API_WRAP(recv)(int fd, void *buf, size_t len, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recv(fd, buf, len, flags);
    }
    /* API 边界点位: ub_bench_epoll 的收包入口, 是 RTT 对比的另一个主锚点 */
    PROF_START(CORE_API_RECV);
    SocketPtr sock = ArraySet<Socket>::GetInstance().GetItem(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::recv(fd, buf, len, flags);
    }
    ssize_t ret = sockBase->Recv(sock, buf, len, flags);
    PROF_END(CORE_API_RECV, ret >= 0);
    return ret;
}

UBS_API ssize_t UB_API_WRAP(read)(int fd, void *buf, size_t nbyte)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::read(fd, buf, nbyte);
    }
    /* Wrap single-buffer read as 1-element readv. */
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = nbyte;
    return UB_API_WRAP(readv)(fd, &iov, 1);
}

UBS_API ssize_t UB_API_WRAP(write)(int fd, const void *buf, size_t nbyte)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::write(fd, buf, nbyte);
    }
    /* Wrap single-buffer write as 1-element writev. */
    struct iovec iov;
    iov.iov_base = const_cast<void *>(buf);
    iov.iov_len = nbyte;
    return UB_API_WRAP(writev)(fd, &iov, 1);
}

UBS_API ssize_t UB_API_WRAP(sendto)(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                                    socklen_t addrlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::sendto(fd, buf, len, flags, dest_addr, addrlen);
    }
    /* For connected UB sockets with NULL dest_addr, behave as send. */
    if (dest_addr == nullptr) {
        return UB_API_WRAP(send)(fd, buf, len, flags);
    }
    /* Unconnected socket sendto not supported in UB mode, fallback to libc. */
    return LibcApi::sendto(fd, buf, len, flags, dest_addr, addrlen);
}

UBS_API ssize_t UB_API_WRAP(recvfrom)(int fd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                                      socklen_t *addrlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recvfrom(fd, buf, len, flags, dest_addr, addrlen);
    }
    /* For connected UB sockets with NULL dest_addr, behave as recv. */
    if (dest_addr == nullptr) {
        return UB_API_WRAP(recv)(fd, buf, len, flags);
    }
    /* Unconnected socket recvfrom not supported in UB mode, fallback to libc. */
    return LibcApi::recvfrom(fd, buf, len, flags, dest_addr, addrlen);
}

UBS_API ssize_t UB_API_WRAP(sendmsg)(int fd, const struct msghdr *msg, int flags)
{
    /* sendmsg not accelerated in UB mode, always fallback to libc. */
    return LibcApi::sendmsg(fd, msg, flags);
}

UBS_API ssize_t UB_API_WRAP(recvmsg)(int fd, struct msghdr *msg, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recvmsg(fd, msg, flags);
    }
    /* recvmsg not accelerated in UB mode, always fallback to libc. */
    return LibcApi::recvmsg(fd, msg, flags);
}

UBS_API ssize_t UB_API_WRAP(sendfile)(int out_fd, int in_fd, off_t *offset, size_t count)
{
    /* sendfile not accelerated in UB mode, always fallback to libc. */
    return LibcApi::sendfile(out_fd, in_fd, offset, count);
}

UBS_API ssize_t UB_API_WRAP(sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count)
{
    /* sendfile not accelerated in UB mode, always fallback to libc. */
    return LibcApi::sendfile64(out_fd, in_fd, offset, count);
}

UBS_API int UB_API_WRAP(fcntl)(int fd, int cmd, ...)
{
    /* fcntl not intercepted in UB mode, always fallback to libc. */
    unsigned long int arg{0};
    va_list va;
    va_start(va, cmd);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    return LibcApi::fcntl(fd, cmd, arg);
}

UBS_API int UB_API_WRAP(fcntl64)(int fd, int cmd, ...)
{
    /* fcntl64 not intercepted in UB mode, always fallback to libc. */
    unsigned long int arg{0};
    va_list va;
    va_start(va, cmd);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    return LibcApi::fcntl64(fd, cmd, arg);
}

UBS_API int UB_API_WRAP(ioctl)(int fd, unsigned long request, ...)
{
    /* ioctl not intercepted in UB mode, always fallback to libc. */
    unsigned long int arg{0};
    va_list va;
    va_start(va, request);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    return LibcApi::ioctl(fd, request, arg);
}

UBS_API int UB_API_WRAP(setsockopt)(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::setsockopt(fd, level, optname, optval, optlen);
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
