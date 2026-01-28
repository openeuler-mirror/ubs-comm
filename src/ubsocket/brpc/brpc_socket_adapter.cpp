// Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
// Description: Provide the utility for umq buffer, iov, etc
// Author:
// Create: 2025-07-16
// Note:
// History: 2025-07-16

#include <sys/eventfd.h>
#include <unistd.h>

#include "brpc_context.h"
#include "rpc_adpt_vlog.h"
#include "file_descriptor.h"
#include "brpc_file_descriptor.h"
#include "ubs_mem/mem_file_descriptor.h"
#include "ubs_mem/shm.h"

static bool ForceUseUB()
{
    static bool enable = []() {
        const char *env = std::getenv("RPC_ADPT_UB_FORCE");
        if (env == nullptr) {
            return false;
        }
        std::string envStr(env);
        if (envStr == "1") {
            RPC_ADPT_VLOG_INFO("RPC_ADPT_UB_FORCE is set force use ub acceleration");
            return true;
        }
        return false;
    }();
    return enable;
}

static bool UseUB(int domain, int type)
{
    bool isTCP = ((domain == AF_INET) || (domain == AF_INET6)) && (type == SOCK_STREAM);
    if ((domain == AF_SMC) || (ForceUseUB() && isTCP)) {
        return true;
    }
    return false;
}

EXPOSE_C_DEFINE int socket(int domain, int type, int protocol)
{
    int fd = -1;
    if (domain == AF_SMC) {
        fd = OsAPiMgr::GetOriginApi()->socket(AF_INET, type, protocol);
    } else {
        fd = OsAPiMgr::GetOriginApi()->socket(domain, type, protocol);
    }
    if (!UseUB(domain, type) || fd < 0) {
        return fd;
    }

    /* The 'socket()' function is only called when constructing the 'Brpc::Context'singleton, so the
     * file descriptor (fd) is directly returned to avoid recursively constructing 'Brpc::Context'. */
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr) {
        return fd;
    }

    int event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd < 0) {
        OsAPiMgr::GetOriginApi()->close(fd);
        return -1;
    }

    SocketFd *socket_fd_obj = Brpc::Context::GetContext()->CreateSocketFd(fd, event_fd);
    if (socket_fd_obj == nullptr) {
        OsAPiMgr::GetOriginApi()->close(fd);
        OsAPiMgr::GetOriginApi()->close(event_fd);
        return -1;
    }

    if (context->GetUsePolling()) {
        Socket *sock = NULL;
        if (PollingEpoll::GetInstance().SocketCreate(&sock, fd, SocketType::SOCKET_TYPE_TCP) != 0) {
            RPC_ADPT_VLOG_ERR("SocketCreate failed \n");
        } else {
            PollingEpoll::GetInstance().AddSocket(fd, sock);
        }
    }

    //Delete existing objects and record new objects in the list.
    Fd<SocketFd>::OverrideFdObj(fd, socket_fd_obj);

    return fd;
}

EXPOSE_C_DEFINE int shutdown(int fd, int how)
{
    return OsAPiMgr::GetOriginApi()->shutdown(fd, how);
}


EXPOSE_C_DEFINE int close(int fd)
{
    Fd<SocketFd>::OverrideFdObj(fd, nullptr);
    Fd<EpollFd>::OverrideFdObj(fd, nullptr);

    return OsAPiMgr::GetOriginApi()->close(fd);
}

EXPOSE_C_DEFINE int accept(int socket, struct sockaddr *address, socklen_t *address_len)
{
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

EXPOSE_C_DEFINE int accept4(int socket, struct sockaddr *address, socklen_t *address_len, int flag)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(socket);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->accept4(socket, address, address_len, flag);
    }

    return obj->Accept(address, address_len);
}

EXPOSE_C_DEFINE int connect(int socket, const struct sockaddr *address, socklen_t address_len)
{
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

EXPOSE_C_DEFINE ssize_t readv(int fildes, const struct iovec *iov, int iovcnt)
{
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

EXPOSE_C_DEFINE ssize_t writev(int fildes, const struct iovec *iov, int iovcnt)
{
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

EXPOSE_C_DEFINE ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->send(sockfd, buf, len, flags);
    }
 
    return obj->Send(buf, len, flags);
}

EXPOSE_C_DEFINE ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->recv(sockfd, buf, len, flags);
    }
 
    return obj->Recv(buf, len, flags);
}

EXPOSE_C_DEFINE ssize_t read(int fildes, void *buf, size_t nbyte)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->read(fildes, buf, nbyte);
    }

    return obj->Read(buf, nbyte);
}

EXPOSE_C_DEFINE ssize_t write(int fildes, const void *buf, size_t nbyte)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->write(fildes, buf, nbyte);
    }

    return obj->Write(buf, nbyte);
}

EXPOSE_C_DEFINE ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                               socklen_t addrlen)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    }
 
    return obj->SendTo(buf, len, flags, dest_addr, addrlen);
}

EXPOSE_C_DEFINE ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                               socklen_t *addrlen)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(sockfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->recvfrom(sockfd, buf, len, flags, dest_addr, addrlen);
    }
 
    return obj->RecvFrom(buf, len, flags, dest_addr, addrlen);
}

EXPOSE_C_DEFINE ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    return OsAPiMgr::GetOriginApi()->sendmsg(sockfd, msg, flags);
}

EXPOSE_C_DEFINE ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    return OsAPiMgr::GetOriginApi()->recvmsg(sockfd, msg, flags);
}

EXPOSE_C_DEFINE ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
    return OsAPiMgr::GetOriginApi()->sendfile64(out_fd, in_fd, offset, count);
}

EXPOSE_C_DEFINE ssize_t sendfile64(int out_fd, int in_fd, off64_t *offset, size_t count)
{
    return OsAPiMgr::GetOriginApi()->sendfile64(out_fd, in_fd, offset, count);
}

EXPOSE_C_DEFINE int fcntl(int fd, int cmd, ...)
{
    unsigned long int arg{ 0 };
    va_list va;
    va_start(va, cmd);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->fcntl(fd, cmd, arg);
    }

    return obj->Fcntl(fd, cmd, arg);
}

EXPOSE_C_DEFINE int fcntl64(int fd, int cmd, ...)
{
    unsigned long int arg{ 0 };
    va_list va;
    va_start(va, cmd);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->fcntl64(fd, cmd, arg);
    }

    return obj->Fcntl64(fd, cmd, arg);
}

EXPOSE_C_DEFINE int ioctl(int fd, unsigned long request, ...)
{
    unsigned long int arg{ 0 };
    va_list va;
    va_start(va, request);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->ioctl(fd, request, arg);
    }

    return obj->Ioctl(fd, request, arg);
}

EXPOSE_C_DEFINE int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->setsockopt(fd, level, optname, optval, optlen);
    }

    return obj->SetSockOpt(fd, level, optname, optval, optlen);
}

EXPOSE_C_DEFINE int epoll_create(int size)
{
    int epoll_fd = OsAPiMgr::GetOriginApi()->epoll_create(size);
    if (epoll_fd < 0) {
        return epoll_fd;
    }
    
    /* The 'epoll_create()' function is only called when constructing the 'Brpc::Context'singleton, so the
     * file descriptor (fd) is directly returned to avoid recursively constructing 'Brpc::Context'. */
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr) {
        return epoll_fd;
    }

    EpollFd *epoll_fd_obj = context->CreateEpollFd(epoll_fd);
    if (epoll_fd_obj == nullptr) {
        OsAPiMgr::GetOriginApi()->close(epoll_fd);
        return -1;
    }

    if (context->GetUsePolling() && PollingEpoll::GetInstance().PollingEpollCreate(epoll_fd) != 0) {
        RPC_ADPT_VLOG_ERR("PollingEpollCreate failed \n");
    }

    // Delete existing objects and record new objects in the list.
    Fd<EpollFd>::OverrideFdObj(epoll_fd, epoll_fd_obj);

    return epoll_fd;
}

EXPOSE_C_DEFINE int epoll_create1(int flags)
{
    int epoll_fd = OsAPiMgr::GetOriginApi()->epoll_create1(flags);
    if (epoll_fd < 0) {
        return epoll_fd;
    }

    /* The 'epoll_create1()' function is only called when constructing the 'Brpc::Context'singleton, so the
     * file descriptor (fd) is directly returned to avoid recursively constructing 'Brpc::Context'. */
    Brpc::Context *context = Brpc::Context::GetContext();
    if (context == nullptr) {
        return epoll_fd;
    }

    EpollFd *epoll_fd_obj = context->CreateEpollFd(epoll_fd);
    if (epoll_fd_obj == nullptr) {
        OsAPiMgr::GetOriginApi()->close(epoll_fd);
        return -1;
    }

    // Delete existing objects and record new objects in the list.
    Fd<EpollFd>::OverrideFdObj(epoll_fd, epoll_fd_obj);

    return epoll_fd;
}

EXPOSE_C_DEFINE int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    EpollFd *obj = Fd<EpollFd>::GetFdObj(epfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->epoll_ctl(epfd, op, fd, event);
    }

    Brpc::Context *context = Brpc::Context::GetContext();
    bool use_polling = context == nullptr ? false : context->GetUsePolling();

    return obj->EpollCtl(op, fd, event, use_polling);
}

EXPOSE_C_DEFINE int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
    EpollFd *obj = Fd<EpollFd>::GetFdObj(epfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->epoll_wait(epfd, events, maxevents, timeout);
    }

    Brpc::Context *context = Brpc::Context::GetContext();
    bool use_polling = context == nullptr ? false : context->GetUsePolling();

    return obj->EpollWait(events, maxevents, timeout, use_polling);
}

EXPOSE_C_DEFINE int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                const sigset_t *sigmask)
{
    EpollFd *obj = Fd<EpollFd>::GetFdObj(epfd);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->epoll_pwait(epfd, events, maxevents, timeout, sigmask);
    }

    return obj->EpollWait(events, maxevents, timeout);
}

// Be cautious, global obj constructor may occur after this.
__attribute__((constructor)) static void rpc_adapter_brpc_init(void)
{
    (void)OsAPiMgr::GetOriginApi();
#ifdef UBS_SHM_BUILD_ENABLED
    (void)ShmMgr::GetShmMgr();
    (void)Brpc::Context::GetContext()->InitShm();
#elif !defined(UBSOCKET_TEST_MODE)
    if (getenv("LD_PRELOAD") != nullptr) {
        (void)Brpc::Context::GetContext();
    }

#endif
}

