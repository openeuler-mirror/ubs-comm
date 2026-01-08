/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-16
 *Note:
 *History: 2025-07-16
*/

#include <sys/eventfd.h>
#include <unistd.h>

#include "brpc_context.h"
#include "rpc_adpt_vlog.h"
#include "file_descriptor.h"
#include "brpc_file_descriptor.h"

EXPOSE_C_DEFINE int socket(int domain, int type, int protocol)
{
    int fd = OsAPiMgr::GetOriginApi()->socket(domain, type, protocol);
    if (!(((domain == AF_INET) || (domain == AF_INET6)) && type == SOCK_STREAM) ||
        fd < 0) {
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
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(socket);
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
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(socket);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->connect(socket, address, address_len);
    }

    return obj->Connect(address, address_len);
}

EXPOSE_C_DEFINE ssize_t readv(int fildes, const struct iovec *iov, int iovcnt)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->readv(fildes, iov, iovcnt);
    }

    return obj->ReadV(iov, iovcnt);
}

EXPOSE_C_DEFINE ssize_t writev(int fildes, const struct iovec *iov, int iovcnt)
{
    Brpc::SocketFd *obj = (Brpc::SocketFd *)Fd<SocketFd>::GetFdObj(fildes);
    if (obj == nullptr) {
        return OsAPiMgr::GetOriginApi()->writev(fildes, iov, iovcnt);
    }

    return obj->WriteV(iov, iovcnt);
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

__attribute__((constructor)) static void rpc_adapter_brpc_init(void)
{
    (void)OsAPiMgr::GetOriginApi();
    (void)Brpc::Context::GetContext();
}