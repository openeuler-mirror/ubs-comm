// Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
// Description: Provide the utility for umq buffer, iov, etc
// Author:
// Create: 2025-07-16
// Note:
// History: 2025-07-16

#include "brpc_socket_adapter.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include "brpc_context.h"
#include "rpc_adpt_vlog.h"
#include "file_descriptor.h"
#include "brpc_file_descriptor.h"
#include "ubs_mem/mem_file_descriptor.h"
#include "ubs_mem/shm.h"

EXPOSE_C_DEFINE int UB_API_WRAP(socket)(int domain, int type, int protocol)
{
    int fd = -1;
    if (domain == AF_SMC) {
        fd = OsAPiMgr::GetOriginApi()->socket(AF_INET, type, protocol);
    } else {
        fd = OsAPiMgr::GetOriginApi()->socket(domain, type, protocol);
    }

    if (!Brpc::Context::GetUbEnableFlag()) {
        return fd;
    }

    /* The 'socket()' function is only called when constructing the 'Brpc::Context'singleton, so the
     * file descriptor (fd) is directly returned to avoid recursively constructing 'Brpc::Context'. */
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr) {
        return fd;
    }

    if (!context->UseUB(domain, type) || fd < 0) {
        return fd;
    }

    int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd < 0) {
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
            "eventfd() failed, ret: %d, errno: %d, errmsg: %s\n",
            event_fd, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
        OsAPiMgr::GetOriginApi()->close(fd);
        return -1;
    }

    SocketFd *socket_fd_obj = Brpc::Context::GetContext()->CreateSocketFd(fd, event_fd);
    if (socket_fd_obj == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
            "CreateSocketFd() failed, fd: %d, event fd: %d, ret: %p\n",
            fd, event_fd, socket_fd_obj);
        OsAPiMgr::GetOriginApi()->close(fd);
        OsAPiMgr::GetOriginApi()->close(event_fd);
        return -1;
    }

    if (context->GetUsePolling()) {
        Socket *sock = NULL;
        if (PollingEpoll::GetInstance().SocketCreate(&sock, fd, SocketType::SOCKET_TYPE_TCP) != 0) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "SocketCreate failed \n");
        } else {
            PollingEpoll::GetInstance().AddSocket(fd, sock);
        }
    }

    socket_fd_obj->SetFdType(FdType::NATIVE_SOCKET_FD);

    // Delete existing objects and record new objects in the list.
    Fd<SocketFd>::OverrideFdObj(fd, socket_fd_obj);

    return fd;
}

EXPOSE_C_DEFINE int UB_API_WRAP(shutdown)(int fd, int how)
{
    return OsAPiMgr::GetOriginApi()->shutdown(fd, how);
}

EXPOSE_C_DEFINE int UB_API_WRAP(close)(int fd)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->close(fd);
    }
    CleanSocketEpollMapper(fd);
    Fd<SocketFd>::OverrideFdObj(fd, nullptr);
    Fd<EpollFd>::OverrideFdObj(fd, nullptr);

    Brpc::Context *ctx = Brpc::Context::GetContext();
    if (ctx && ctx->GetUsePolling()) {
        PollingEpoll::GetInstance().RemoveSocket(fd);
    }

    return OsAPiMgr::GetOriginApi()->close(fd);
}

EXPOSE_C_DEFINE int UB_API_WRAP(accept)(int socket, struct sockaddr *address, socklen_t *address_len)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->accept(socket, address, address_len);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(socket);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(socket);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->accept(socket, address, address_len);
    }

    return obj->Accept(address, address_len);
}

EXPOSE_C_DEFINE int UB_API_WRAP(accept4)(int socket, struct sockaddr *address, socklen_t *address_len, int flag)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->accept4(socket, address, address_len, flag);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(socket);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(socket);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->accept4(socket, address, address_len, flag);
    }

    return obj->Accept(address, address_len);
}

EXPOSE_C_DEFINE int UB_API_WRAP(listen)(int fd, int backlog)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->listen(fd, backlog);
    }
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->listen(fd, backlog);
    }
    return obj->Listen(backlog);
}

EXPOSE_C_DEFINE int UB_API_WRAP(connect)(int socket, const struct sockaddr *address, socklen_t address_len)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->connect(socket, address, address_len);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(socket);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(socket);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->connect(socket, address, address_len);
    }

    return obj->Connect(address, address_len);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(readv)(int fildes, const struct iovec *iov, int iovcnt)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->readv(fildes, iov, iovcnt);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(fildes);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->readv(fildes, iov, iovcnt);
    }

    return obj->ReadV(iov, iovcnt);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(writev)(int fildes, const struct iovec *iov, int iovcnt)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->writev(fildes, iov, iovcnt);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(fildes);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->writev(fildes, iov, iovcnt);
    }

    return obj->WriteV(iov, iovcnt);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(send)(int sockfd, const void *buf, size_t len, int flags)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->send(sockfd, buf, len, flags);
    }
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->send(sockfd, buf, len, flags);
    }
 
    return obj->Send(buf, len, flags);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(recv)(int sockfd, void *buf, size_t len, int flags)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->recv(sockfd, buf, len, flags);
    }
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->recv(sockfd, buf, len, flags);
    }
 
    return obj->Recv(buf, len, flags);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(read)(int fildes, void *buf, size_t nbyte)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->read(fildes, buf, nbyte);
    }
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->read(fildes, buf, nbyte);
    }

    return obj->Read(buf, nbyte);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(write)(int fildes, const void *buf, size_t nbyte)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->write(fildes, buf, nbyte);
    }
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->write(fildes, buf, nbyte);
    }

    return obj->Write(buf, nbyte);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendto)(int sockfd, const void *buf, size_t len, int flags,
                                            const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }
 
    return obj->SendTo(buf, len, flags, dest_addr, addrlen);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(recvfrom)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                                              socklen_t *addrlen)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->recvfrom(sockfd, buf, len, flags, dest_addr, addrlen);
    }
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->recvfrom(sockfd, buf, len, flags, dest_addr, addrlen);
    }
 
    return obj->RecvFrom(buf, len, flags, dest_addr, addrlen);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendmsg)(int sockfd, const struct msghdr *msg, int flags)
{
    return OsAPiMgr::GetOriginApi()->sendmsg(sockfd, msg, flags);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(recvmsg)(int sockfd, struct msghdr *msg, int flags)
{
    return OsAPiMgr::GetOriginApi()->recvmsg(sockfd, msg, flags);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendfile)(int out_fd, int in_fd, off_t *offset, size_t count)
{
    return OsAPiMgr::GetOriginApi()->sendfile64(out_fd, in_fd, offset, count);
}

EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count)
{
    return OsAPiMgr::GetOriginApi()->sendfile64(out_fd, in_fd, offset, count);
}

static int vfcntl(int fd, int cmd, va_list va)
{
    uintptr_t arg{ 0 };
    arg = va_arg(va, decltype(arg));

    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->fcntl(fd, cmd, arg);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(fd);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->fcntl(fd, cmd, arg);
    }

    return obj->Fcntl(fd, cmd, arg);
}

static int vfcntl64(int fd, int cmd, va_list va)
{
    uintptr_t arg{ 0 };
    arg = va_arg(va, decltype(arg));

    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->fcntl64(fd, cmd, arg);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(fd);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->fcntl64(fd, cmd, arg);
    }

    return obj->Fcntl64(fd, cmd, arg);
}

EXPOSE_C_DEFINE int UB_API_WRAP(fcntl)(int fd, int cmd, ...)
{
    va_list va;

    va_start(va, cmd);
    int ret = vfcntl(fd, cmd, va);
    va_end(va);

    return ret;
}

EXPOSE_C_DEFINE int UB_API_WRAP(fcntl64)(int fd, int cmd, ...)
{
    va_list va;

    va_start(va, cmd);
    int ret = vfcntl64(fd, cmd, va);
    va_end(va);

    return ret;
}

static int vioctl(int fd, unsigned long request, va_list va)
{
    uintptr_t arg{ 0 };
    arg = va_arg(va, decltype(arg));
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->ioctl(fd, request, arg);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(fd);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->ioctl(fd, request, arg);
    }

    return obj->Ioctl(fd, request, arg);
}

EXPOSE_C_DEFINE int UB_API_WRAP(ioctl)(int fd, unsigned long request, ...)
{
    va_list va;

    va_start(va, request);
    int ret = vioctl(fd, request, va);
    va_end(va);

    return ret;
}

EXPOSE_C_DEFINE int UB_API_WRAP(setsockopt)(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->setsockopt(fd, level, optname, optval, optlen);
    }
#ifdef UBS_SHM_BUILD_ENABLED
    Brpc::MemSocketFd *obj = (Brpc::MemSocketFd *)Fd<SocketFd>::GetFdObj(fd);
#else
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
#endif
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->setsockopt(fd, level, optname, optval, optlen);
    }

    return obj->SetSockOpt(fd, level, optname, optval, optlen);
}

EXPOSE_C_DEFINE int UB_API_WRAP(epoll_create)(int size)
{
    int epoll_fd = OsAPiMgr::GetOriginApi()->epoll_create(size);
    if (epoll_fd < 0 && Brpc::Context::GetUbEnableFlag()) {
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
            "epoll_create() failed, size: %d, ret: %d, errno: %d, errmsg: %s\n",
            size, epoll_fd, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
    }
    if (!Brpc::Context::GetUbEnableFlag() || epoll_fd < 0) {
        return epoll_fd;
    }
    
    /* The 'epoll_create()' function is only called when constructing the 'Brpc::Context'singleton, so the
     * file descriptor (fd) is directly returned to avoid recursively constructing 'Brpc::Context'. */
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr || !context->IsUbEpollEnable()) {
        return epoll_fd;
    }

    EpollFd *epoll_fd_obj = context->CreateEpollFd(epoll_fd);
    if (epoll_fd_obj == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
            "CreateEpollFd() failed, epoll fd: %d, ret: %p\n",
            epoll_fd, epoll_fd_obj);
        OsAPiMgr::GetOriginApi()->close(epoll_fd);
        return -1;
    }

    if (context->GetUsePolling() && PollingEpoll::GetInstance().PollingEpollCreate(epoll_fd) != 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "PollingEpollCreate failed \n");
    }

    // Delete existing objects and record new objects in the list.
    Fd<EpollFd>::OverrideFdObj(epoll_fd, epoll_fd_obj);

    return epoll_fd;
}

EXPOSE_C_DEFINE int UB_API_WRAP(epoll_create1)(int flags)
{
    int epoll_fd = OsAPiMgr::GetOriginApi()->epoll_create1(flags);
    if (epoll_fd < 0 && Brpc::Context::GetUbEnableFlag()) {
        char errno_buf[NET_STR_ERROR_BUF_SIZE] = {0};
        RPC_ADPT_VLOG_ERR(ubsocket::NATIVE_SOCKET,
            "epoll_create1() failed, flags: %d, ret: %d, errno: %d, errmsg: %s\n",
            flags, epoll_fd, errno, NetCommon::NN_GetStrError(errno, errno_buf, NET_STR_ERROR_BUF_SIZE));
    }
    if (!Brpc::Context::GetUbEnableFlag() || epoll_fd < 0) {
        return epoll_fd;
    }

    /* The 'epoll_create1()' function is only called when constructing the 'Brpc::Context'singleton, so the
     * file descriptor (fd) is directly returned to avoid recursively constructing 'Brpc::Context'. */
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr || !context->IsUbEpollEnable()) {
        return epoll_fd;
    }

    EpollFd *epoll_fd_obj = context->CreateEpollFd(epoll_fd);
    if (epoll_fd_obj == nullptr) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
            "CreateEpollFd() failed, epoll fd: %d, ret: %p\n",
            epoll_fd, epoll_fd_obj);
        OsAPiMgr::GetOriginApi()->close(epoll_fd);
        return -1;
    }

    // Delete existing objects and record new objects in the list.
    Fd<EpollFd>::OverrideFdObj(epoll_fd, epoll_fd_obj);

    return epoll_fd;
}

EXPOSE_C_DEFINE int UB_API_WRAP(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->epoll_ctl(epfd, op, fd, event);
    }
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr || !context->IsUbEpollEnable()) {
        return OsAPiMgr::GetOriginApi()->epoll_ctl(epfd, op, fd, event);
    }

    EpollFd *obj = Fd<EpollFd>::GetFdObj(epfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->epoll_ctl(epfd, op, fd, event);
    }
    bool use_polling = context == nullptr ? false : context->GetUsePolling();

    return obj->EpollCtl(op, fd, event, use_polling);
}

EXPOSE_C_DEFINE int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->epoll_wait(epfd, events, maxevents, timeout);
    }
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr || !context->IsUbEpollEnable()) {
        return OsAPiMgr::GetOriginApi()->epoll_wait(epfd, events, maxevents, timeout);
    }

    EpollFd *obj = Fd<EpollFd>::GetFdObj(epfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->epoll_wait(epfd, events, maxevents, timeout);
    }

    bool use_polling = context == nullptr ? false : context->GetUsePolling();

    return obj->EpollWait(events, maxevents, timeout, use_polling);
}

EXPOSE_C_DEFINE int UB_API_WRAP(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                const sigset_t *sigmask)
{
    if (!Brpc::Context::GetUbEnableFlag()) {
        return OsAPiMgr::GetOriginApi()->epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr || !context->IsUbEpollEnable()) {
        return OsAPiMgr::GetOriginApi()->epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }

    EpollFd *obj = Fd<EpollFd>::GetFdObj(epfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }

    return obj->EpollWait(events, maxevents, timeout);
}

#ifdef UBSOCKET_ENABLE_INTERCEPT
EXPOSE_C_DEFINE int socket(int domain, int type, int protocol)
{
    return UB_API_WRAP(socket)(domain, type, protocol);
}

EXPOSE_C_DEFINE int shutdown(int fd, int how)
{
    return UB_API_WRAP(shutdown)(fd, how);
}

EXPOSE_C_DEFINE int close(int fd)
{
    return UB_API_WRAP(close)(fd);
}

EXPOSE_C_DEFINE int accept(int socket, struct sockaddr *address, socklen_t *address_len)
{
    return UB_API_WRAP(accept)(socket, address, address_len);
}

EXPOSE_C_DEFINE int accept4(int socket, struct sockaddr *address, socklen_t *address_len, int flags)
{
    return UB_API_WRAP(accept4)(socket, address, address_len, flags);
}

EXPOSE_C_DEFINE int listen(int fd, int backlog)
{
    return UB_API_WRAP(listen)(fd, backlog);
}

EXPOSE_C_DEFINE int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
    return UB_API_WRAP(connect)(socket, address, address_len);
}

EXPOSE_C_DEFINE ssize_t readv(int fildes, const struct iovec *iov, int iovcnt)
{
    return UB_API_WRAP(readv)(fildes, iov, iovcnt);
}

EXPOSE_C_DEFINE ssize_t writev(int fildes, const struct iovec *iov, int iovcnt)
{
    return UB_API_WRAP(writev)(fildes, iov, iovcnt);
}

EXPOSE_C_DEFINE ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    return UB_API_WRAP(send)(sockfd, buf, len, flags);
}

EXPOSE_C_DEFINE ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    return UB_API_WRAP(recv)(sockfd, buf, len, flags);
}

EXPOSE_C_DEFINE ssize_t read(int fildes, void *buf, size_t nbyte)
{
    return UB_API_WRAP(read)(fildes, buf, nbyte);
}

EXPOSE_C_DEFINE ssize_t write(int fildes, const void *buf, size_t nbyte)
{
    return UB_API_WRAP(write)(fildes, buf, nbyte);
}

EXPOSE_C_DEFINE ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                               socklen_t addrlen)
{
    return UB_API_WRAP(sendto)(sockfd, buf, len, flags, dest_addr, addrlen);
}

EXPOSE_C_DEFINE ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                                 socklen_t *addrlen)
{
    return UB_API_WRAP(recvfrom)(sockfd, buf, len, flags, dest_addr, addrlen);
}

EXPOSE_C_DEFINE ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return UB_API_WRAP(sendmsg)(sockfd, msg, flags);
}

EXPOSE_C_DEFINE ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return UB_API_WRAP(recvmsg)(sockfd, msg, flags);
}

EXPOSE_C_DEFINE ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
    return UB_API_WRAP(sendfile)(out_fd, in_fd, offset, count);
}

EXPOSE_C_DEFINE ssize_t sendfile64(int out_fd, int in_fd, off64_t *offset, size_t count)
{
    return UB_API_WRAP(sendfile64)(out_fd, in_fd, offset, count);
}

EXPOSE_C_DEFINE int fcntl(int fd, int cmd, ...)
{
    va_list va;

    va_start(va, cmd);
    int ret = vfcntl(fd, cmd, va);
    va_end(va);

    return ret;
}

EXPOSE_C_DEFINE int fcntl64(int fd, int cmd, ...)
{
    va_list va;

    va_start(va, cmd);
    int ret = vfcntl64(fd, cmd, va);
    va_end(va);

    return ret;
}

EXPOSE_C_DEFINE int ioctl(int fd, unsigned long request, ...)
{
    va_list va;

    va_start(va, request);
    int ret = vioctl(fd, request, va);
    va_end(va);

    return ret;
}

EXPOSE_C_DEFINE int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    return UB_API_WRAP(setsockopt)(fd, level, optname, optval, optlen);
}

EXPOSE_C_DEFINE int epoll_create(int size)
{
    return UB_API_WRAP(epoll_create)(size);
}

EXPOSE_C_DEFINE int epoll_create1(int flags)
{
    return UB_API_WRAP(epoll_create1)(flags);
}

EXPOSE_C_DEFINE int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return UB_API_WRAP(epoll_ctl)(epfd, op, fd, event);
}

EXPOSE_C_DEFINE int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    return UB_API_WRAP(epoll_wait)(epfd, events, maxevents, timeout);
}

EXPOSE_C_DEFINE int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                const sigset_t *sigmask)
{
    return UB_API_WRAP(epoll_pwait)(epfd, events, maxevents, timeout, sigmask);
}
#endif

// Be cautious, global obj constructor may occur after this.
__attribute__((constructor)) static void rpc_adapter_brpc_init(void)
{
    (void)OsAPiMgr::GetOriginApi();
#ifdef UBS_SHM_BUILD_ENABLED
    (void)ShmMgr::GetShmMgr();
    (void)Brpc::Context::GetContext()->InitShm();
#elif !defined(UBSOCKET_TEST_MODE)
    if (getenv("LD_PRELOAD") != nullptr) {
        if (Brpc::Context::GetContext() != nullptr) {
            Brpc::Context::SetUbEnable();
        }
    }
#endif
}

