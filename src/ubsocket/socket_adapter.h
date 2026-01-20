/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-16
 *Note:
 *History: 2025-07-16
*/

#ifndef SOCKET_ADAPTER_H
#define SOCKET_ADAPTER_H

#include <poll.h>
#include <signal.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdexcept>
#include "rpc_adpt_vlog.h"

#define EXPOSE_C_DEFINE extern "C" __attribute__((visibility("default")))

typedef int (*creat_api) (const char *pathname, mode_t mode);
typedef int (*open_api) (const char *file, int oflag, ...);
typedef int (*dup_api) (int fildes);
typedef int (*dup2_api) (int fildes, int fildes2);
typedef int (*pipe_api) (int filedes[2]);
typedef int (*socket_api) (int domain, int type, int protocol);
typedef int (*socketpair_api) (int domain, int type, int protocol, int sv[2]);
typedef int (*close_api) (int fd);
typedef int (*shutdown_api) (int fd, int how);
typedef int (*accept_api) (int socket, struct sockaddr *address, socklen_t *address_len);
typedef int (*accept4_api) (int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
typedef int (*bind_api) (int fd, const struct sockaddr *addr, socklen_t addrlen);
typedef int (*connect_api) (int socket, const struct sockaddr *address, socklen_t address_len);
typedef int (*listen_api) (int fd, int backlog);
typedef int (*setsockopt_api) (int fd, int level, int optname, const void *optval, socklen_t optlen);
typedef int (*getsockopt_api) (int fd, int level, int optname, void *optval, socklen_t *optlen);
typedef int (*fcntl_api) (int fd, int cmd, ...);
typedef int (*fcntl64_api) (int fd, int cmd, ...);
typedef int (*ioctl_api) (int fd, unsigned long int request, ...);
typedef int (*getsockname_api) (int fd, struct sockaddr *name, socklen_t *namelen);
typedef int (*getpeername_api) (int fd, struct sockaddr *name, socklen_t *namelen);
typedef ssize_t (*read_api) (int fd, void *buf, size_t nbytes);
typedef ssize_t (*readv_api) (int fildes, const struct iovec *iov, int iovcnt);
typedef ssize_t (*recv_api) (int sockfd, void *buf, size_t size, int flags);
typedef ssize_t (*recvmsg_api) (int fd, struct msghdr *message, int flags);
typedef int (*recvmmsg_api) (int fd, struct mmsghdr *mmsghdr, unsigned int vlen, int flags, struct timespec *timeout);
typedef ssize_t (*recvfrom_api) (int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *fromlen);
typedef ssize_t (*write_api) (int fd, const void *buf, size_t n);
typedef ssize_t (*writev_api) (int fildes, const struct iovec *iov, int iovcnt);
typedef ssize_t (*send_api) (int sockfd, const void *buf, size_t size,int flags);
typedef ssize_t (*sendmsg_api) (int fd, const struct msghdr *message,int flags);
typedef int (*sendmmsg_api) (int fd, struct mmsghdr *mmsghdr, unsigned int vlen, int flags);
typedef ssize_t (*sendto_api) (int fd, const void *buf, size_t n, int flags, const struct sockaddr *to,
     socklen_t tolen);
typedef ssize_t (*sendfile_api) (int out_fd, int in_fd, off_t *offset, size_t count);
typedef ssize_t (*sendfile64_api) (int out_fd, int in_fd, off64_t *offset, size_t count);
typedef int (*select_api) (int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
typedef int (*pselect_api) (int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, 
    const struct timespec *timeout, const sigset_t *sigmask);
typedef int (*poll_api) (struct pollfd *fds, nfds_t nfds, int timeout);   
typedef int (*ppoll_api) (struct pollfd *fds, nfds_t nfds, const struct timespec *timeout, const sigset_t *sigmask);
typedef int (*epoll_create_api) (int size);
typedef int (*epoll_create1_api) (int flags);
typedef int (*epoll_ctl_api) (int epfd, int op, int fd, struct epoll_event *event);
typedef int (*epoll_wait_api) (int epfd, struct epoll_event *events, int maxevents, int timeout);
typedef int (*epoll_pwait_api) (int epfd, struct epoll_event *events, 
    int maxevents, int timeout, const sigset_t *sigmask);
typedef int (*clone_api) (int (*fn)(void *), void *child_stack, int flags, void *arg);
typedef pid_t (*fork_api) (void);
typedef pid_t (*vfork_api) (void);
typedef int (*daemon_api) (int nochdir, int noclose);
typedef int (*sigaction_api) (int signum, const struct sigaction *act, struct sigaction *oldact);
typedef sighandler_t (*signal_api) (int signum, sighandler_t handler);

#define ORIGIN_API_DEFINE(__api_name) __api_name##_api __api_name##_ptr = nullptr
#define ORIGIN_API_DEFINE_INITIALIZER(__api_name) RecordApi(handle, #__api_name, this->__api_name##_ptr)

/** 
 * Record the address where the symbol is loaded into memory
 * The caller is responsible for ensuring the validity of the input parameters; no validation is performed here.
 * @param[in] handle: handle for dynamic loeaded shared object;
 * @param[in] symbol_name: symbol name;
 * @param[out] symbol: variable to record the address;
 * Return: Void
 */
template <typename ApiType>
void RecordApi(void *handle, const char *symbol_name, ApiType &symbol)
{
    (void)dlerror();
    symbol = reinterpret_cast<ApiType>(dlsym(handle, symbol_name));
    char *dlerror_str = dlerror();
    if(!symbol || dlerror_str){
        if(strcmp(symbol_name, "fcntl64")!=0){
            RPC_ADPT_VLOG_ERR("Failed when looking for '%s', error message: %s\n",
                symbol_name, (!dlerror_str ? "": dlerror_str));
        } else {
            // fcntl64 is not used in most cases, thus, print debug log 
            RPC_ADPT_VLOG_DEBUG("Failed when looking for '%s', error message: %s\n",
                 symbol_name, (!dlerror_str ? "": dlerror_str));
        }
    } else {
         RPC_ADPT_VLOG_DEBUG("Found for '%s()'\n", symbol_name);
    }
}

class OsAPiMgr {
    public:
    static OsAPiMgr *GetOriginApi()
    {
        static OsAPiMgr mgr;
        return &mgr;
    }

    int open(const char *file, int oflag, ...)
    {
        unsigned long int arg{ 0 };
        va_list va;
        va_start(va, oflag);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return open_ptr(file, oflag, arg);
    }
    int socket(int domain, int type, int protocol)
    {
        return socket_ptr(domain, type, protocol);
    }
    int close(int fd)
    {
        return close_ptr(fd);
    }
    int shutdown(int fd, int how)
    {
        return shutdown_ptr(fd, how);
    }
    int accept(int socket, struct sockaddr *address, socklen_t *address_len)
    {
        return accept_ptr(socket, address, address_len);
    }
    int accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
    {
        return accept4_ptr(fd, addr, addrlen, flags);
    }
    int bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
    {
        return bind_ptr(fd, addr, addrlen);
    }
    int connect(int socket, const struct sockaddr *address, socklen_t address_len)
    {
        return connect_ptr(socket, address, address_len);
    }
    int listen(int fd, int backlog)
    {
        return listen_ptr(fd, backlog);
    }
    int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
    {
        return setsockopt_ptr(fd, level, optname, optval, optlen);
    }
    int fcntl(int fd, int cmd, ...)
    {
        unsigned long int arg{ 0 };
        va_list va;
        va_start(va, cmd);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return fcntl_ptr(fd, cmd, arg);
    }
    int fcntl64(int fd, int cmd, ...)
    {
        unsigned long int arg{ 0 };
        va_list va;
        va_start(va, cmd);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return fcntl64_ptr(fd, cmd, arg);
    }
    int ioctl(int fd, unsigned long int request, ...)
    {
        unsigned long int arg{ 0 };
        va_list va;
        va_start(va, request);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return ioctl_ptr(fd, request, arg);
    }
    ssize_t read(int fd, void *buf, size_t nbytes)
    {
        return read_ptr(fd, buf, nbytes);
    }
    ssize_t readv(int fildes, const struct iovec *iov, int iovcnt)
    {
        return readv_ptr(fildes, iov, iovcnt);
    }
    ssize_t recv(int sockfd, void *buf, size_t size, int flags)
    {
        return recv_ptr(sockfd, buf, size, flags);
    }
    ssize_t recvmsg(int fd, struct msghdr *message, int flags)
    {
        return recvmsg_ptr(fd, message, flags);
    }
    ssize_t recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *fromlen)
    {
        return recvfrom_ptr(fd, buf, n, flags, from, fromlen);
    }
    ssize_t write(int fd, const void *buf, size_t n)
    {
        return write_ptr(fd, buf, n);
    }
    ssize_t writev(int fildes, const struct iovec *iov, int iovcnt)
    {
        return writev_ptr(fildes, iov, iovcnt);
    }
    ssize_t send(int sockfd, const void *buf, size_t size, int flags)
    {
        return send_ptr(sockfd, buf, size, flags);
    }
    ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
    {
        return sendmsg_ptr(fd, message, flags);
    }
    ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to,
                   socklen_t tolen)
    {
        return sendto_ptr(fd, buf, n, flags, to, tolen);
    }
    ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
    {
        return sendfile_ptr(out_fd, in_fd, offset, count);
    }
    ssize_t sendfile64(int out_fd, int in_fd, off64_t *offset, size_t count)
    {
        return sendfile64_ptr(out_fd, in_fd, offset, count);
    }
    int epoll_create(int size)
    {
        return epoll_create_ptr(size);
    }
    int epoll_create1(int flags)
    {
        return epoll_create1_ptr(flags);
    }
    int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
    {
        return epoll_ctl_ptr(epfd, op, fd, event);
    }
    int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
    {
        return epoll_wait_ptr(epfd, events, maxevents, timeout);
    }
    int epoll_pwait(int epfd, struct epoll_event *events,
                    int maxevents, int timeout, const sigset_t *sigmask)
    {
        return epoll_pwait_ptr(epfd, events, maxevents, timeout, sigmask);
    }

    ORIGIN_API_DEFINE(creat);
    ORIGIN_API_DEFINE(open);
    ORIGIN_API_DEFINE(dup);
    ORIGIN_API_DEFINE(dup2);
    ORIGIN_API_DEFINE(pipe);
    ORIGIN_API_DEFINE(socket);
    ORIGIN_API_DEFINE(socketpair);
    ORIGIN_API_DEFINE(close);
    ORIGIN_API_DEFINE(shutdown);
    ORIGIN_API_DEFINE(accept);
    ORIGIN_API_DEFINE(accept4);
    ORIGIN_API_DEFINE(bind);
    ORIGIN_API_DEFINE(connect);
    ORIGIN_API_DEFINE(listen);
    ORIGIN_API_DEFINE(setsockopt);
    ORIGIN_API_DEFINE(getsockopt);
    ORIGIN_API_DEFINE(fcntl);
    ORIGIN_API_DEFINE(fcntl64);
    ORIGIN_API_DEFINE(ioctl);
    ORIGIN_API_DEFINE(getsockname);
    ORIGIN_API_DEFINE(getpeername);
    ORIGIN_API_DEFINE(read);
    ORIGIN_API_DEFINE(readv);
    ORIGIN_API_DEFINE(recv);
    ORIGIN_API_DEFINE(recvmsg);
    ORIGIN_API_DEFINE(recvmmsg);
    ORIGIN_API_DEFINE(recvfrom);
    ORIGIN_API_DEFINE(write);
    ORIGIN_API_DEFINE(writev);
    ORIGIN_API_DEFINE(send);
    ORIGIN_API_DEFINE(sendmsg);
    ORIGIN_API_DEFINE(sendmmsg);
    ORIGIN_API_DEFINE(sendto);
    ORIGIN_API_DEFINE(sendfile);
    ORIGIN_API_DEFINE(sendfile64);
    ORIGIN_API_DEFINE(select);
    ORIGIN_API_DEFINE(pselect);
    ORIGIN_API_DEFINE(poll);
    ORIGIN_API_DEFINE(ppoll);
    ORIGIN_API_DEFINE(epoll_create);
    ORIGIN_API_DEFINE(epoll_create1);
    ORIGIN_API_DEFINE(epoll_ctl);
    ORIGIN_API_DEFINE(epoll_wait);
    ORIGIN_API_DEFINE(epoll_pwait);
    ORIGIN_API_DEFINE(fork);
    ORIGIN_API_DEFINE(vfork);
    ORIGIN_API_DEFINE(daemon);
    ORIGIN_API_DEFINE(sigaction);
    ORIGIN_API_DEFINE(signal);

    private:
    OsAPiMgr(void)
    {
        void *handle = dlopen ("libc.so.6", RTLD_NOW);
        if (handle == nullptr) {
            throw std::runtime_error("Unable to open dynamic link library");
        }
        ORIGIN_API_DEFINE_INITIALIZER(creat);
        ORIGIN_API_DEFINE_INITIALIZER(open);
        ORIGIN_API_DEFINE_INITIALIZER(dup);
        ORIGIN_API_DEFINE_INITIALIZER(dup2);
        ORIGIN_API_DEFINE_INITIALIZER(pipe);
        ORIGIN_API_DEFINE_INITIALIZER(socket);
        ORIGIN_API_DEFINE_INITIALIZER(socketpair);
        ORIGIN_API_DEFINE_INITIALIZER(close);
        ORIGIN_API_DEFINE_INITIALIZER(shutdown);
        ORIGIN_API_DEFINE_INITIALIZER(accept);
        ORIGIN_API_DEFINE_INITIALIZER(accept4);
        ORIGIN_API_DEFINE_INITIALIZER(bind);
        ORIGIN_API_DEFINE_INITIALIZER(connect);
        ORIGIN_API_DEFINE_INITIALIZER(listen);
        ORIGIN_API_DEFINE_INITIALIZER(setsockopt);
        ORIGIN_API_DEFINE_INITIALIZER(getsockopt);
        ORIGIN_API_DEFINE_INITIALIZER(fcntl);
        ORIGIN_API_DEFINE_INITIALIZER(fcntl64);
        ORIGIN_API_DEFINE_INITIALIZER(ioctl);
        ORIGIN_API_DEFINE_INITIALIZER(getsockname);
        ORIGIN_API_DEFINE_INITIALIZER(getpeername);
        ORIGIN_API_DEFINE_INITIALIZER(read);
        ORIGIN_API_DEFINE_INITIALIZER(readv);
        ORIGIN_API_DEFINE_INITIALIZER(recv);
        ORIGIN_API_DEFINE_INITIALIZER(recvmsg);
        ORIGIN_API_DEFINE_INITIALIZER(recvmmsg);
        ORIGIN_API_DEFINE_INITIALIZER(recvfrom);
        ORIGIN_API_DEFINE_INITIALIZER(write);
        ORIGIN_API_DEFINE_INITIALIZER(writev);
        ORIGIN_API_DEFINE_INITIALIZER(send);
        ORIGIN_API_DEFINE_INITIALIZER(sendmsg);
        ORIGIN_API_DEFINE_INITIALIZER(sendmmsg);
        ORIGIN_API_DEFINE_INITIALIZER(sendto);
        ORIGIN_API_DEFINE_INITIALIZER(sendfile);
        ORIGIN_API_DEFINE_INITIALIZER(sendfile64);
        ORIGIN_API_DEFINE_INITIALIZER(select);
        ORIGIN_API_DEFINE_INITIALIZER(pselect);
        ORIGIN_API_DEFINE_INITIALIZER(poll);
        ORIGIN_API_DEFINE_INITIALIZER(ppoll);
        ORIGIN_API_DEFINE_INITIALIZER(epoll_create);
        ORIGIN_API_DEFINE_INITIALIZER(epoll_create1);
        ORIGIN_API_DEFINE_INITIALIZER(epoll_ctl);
        ORIGIN_API_DEFINE_INITIALIZER(epoll_wait);
        ORIGIN_API_DEFINE_INITIALIZER(epoll_pwait);
        ORIGIN_API_DEFINE_INITIALIZER(fork);
        ORIGIN_API_DEFINE_INITIALIZER(vfork);
        ORIGIN_API_DEFINE_INITIALIZER(daemon);
        ORIGIN_API_DEFINE_INITIALIZER(sigaction);
        ORIGIN_API_DEFINE_INITIALIZER(signal);

        int ret = dlclose(handle);
        if (ret != 0) {
            RPC_ADPT_VLOG_ERR("Unable to close the dynamic link library: %s\n",
                              dlerror());
        }
    }
};

#endif