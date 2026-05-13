/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_DL_LIBC_API_H
#define UBS_COMM_DL_LIBC_API_H

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <csignal>

#include "dl_api_common.h"

using creat_api = int (*)(const char *pathname, mode_t mode);
using open_api = int (*)(const char *file, int oflag, ...);
using dup_api = int (*)(int fildes);
using dup2_api = int (*)(int fildes, int fildes2);
using pipe_api = int (*)(int filedes[2]);
using socket_api = int (*)(int domain, int type, int protocol);
using socketpair_api = int (*)(int domain, int type, int protocol, int sv[2]);
using close_api = int (*)(int fd);
using shutdown_api = int (*)(int fd, int how);
using accept_api = int (*)(int socket, struct sockaddr *address, socklen_t *address_len);
using accept4_api = int (*)(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags);
using bind_api = int (*)(int fd, const struct sockaddr *addr, socklen_t addrlen);
using connect_api = int (*)(int socket, const struct sockaddr *address, socklen_t address_len);
using listen_api = int (*)(int fd, int backlog);
using setsockopt_api = int (*)(int fd, int level, int optname, const void *optval, socklen_t optlen);
using getsockopt_api = int (*)(int fd, int level, int optname, void *optval, socklen_t *optlen);
using fcntl_api = int (*)(int fd, int cmd, ...);
using fcntl64_api = int (*)(int fd, int cmd, ...);
using ioctl_api = int (*)(int fd, unsigned long int request, ...);
using getsockname_api = int (*)(int fd, struct sockaddr *name, socklen_t *namelen);
using getpeername_api = int (*)(int fd, struct sockaddr *name, socklen_t *namelen);
using read_api = ssize_t (*)(int fd, void *buf, size_t nbytes);
using readv_api = ssize_t (*)(int fildes, const struct iovec *iov, int iovcnt);
using recv_api = ssize_t (*)(int sockfd, void *buf, size_t size, int flags);
using recvmsg_api = ssize_t (*)(int fd, struct msghdr *message, int flags);
using recvmmsg_api = int (*)(int fd, struct mmsghdr *mmsghdr, unsigned int vlen, int flags, struct timespec *timeout);
using recvfrom_api = ssize_t (*)(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *fromlen);
using write_api = ssize_t (*)(int fd, const void *buf, size_t n);
using writev_api = ssize_t (*)(int fildes, const struct iovec *iov, int iovcnt);
using send_api = ssize_t (*)(int sockfd, const void *buf, size_t size, int flags);
using sendmsg_api = ssize_t (*)(int fd, const struct msghdr *message, int flags);
using sendmmsg_api = int (*)(int fd, struct mmsghdr *mmsghdr, unsigned int vlen, int flags);
using sendto_api = ssize_t (*)(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to,
                               socklen_t tolen);
using sendfile_api = ssize_t (*)(int out_fd, int in_fd, off_t *offset, size_t count);
using sendfile64_api = ssize_t (*)(int out_fd, int in_fd, off64_t *offset, size_t count);
using select_api = int (*)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
using pselect_api = int (*)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds,
                            const struct timespec *timeout, const sigset_t *sigmask);
using poll_api = int (*)(struct pollfd *fds, nfds_t nfds, int timeout);
using ppoll_api = int (*)(struct pollfd *fds, nfds_t nfds, const struct timespec *timeout, const sigset_t *sigmask);
using epoll_create_api = int (*)(int size);
using epoll_create1_api = int (*)(int flags);
using epoll_ctl_api = int (*)(int epfd, int op, int fd, struct epoll_event *event);
using epoll_wait_api = int (*)(int epfd, struct epoll_event *events, int maxevents, int timeout);
using epoll_pwait_api = int (*)(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                const sigset_t *sigmask);
using clone_api = int (*)(int (*fn)(void *), void *child_stack, int flags, void *arg);
using fork_api = pid_t (*)(void);
using vfork_api = pid_t (*)(void);
using daemon_api = int (*)(int nochdir, int noclose);
using sigaction_api = int (*)(int signum, const struct sigaction *act, struct sigaction *oldact);
using signal_api = sighandler_t (*)(int signum, sighandler_t handler);

namespace ock {
namespace ubs {
class LibcApi {
public:
public:
    static Result Load() noexcept;
    static void UnLoad() noexcept;

    static int open(const char *file, int oflag, ...)
    {
        unsigned long int arg{0};
        va_list va;
        va_start(va, oflag);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return open_ptr(file, oflag, arg);
    }

    static int socket(int domain, int type, int protocol)
    {
        return socket_ptr(domain, type, protocol);
    }

    static int close(int fd)
    {
        return close_ptr(fd);
    }

    static int shutdown(int fd, int how)
    {
        return shutdown_ptr(fd, how);
    }

    static int accept(int socket, struct sockaddr *address, socklen_t *address_len)
    {
        return accept_ptr(socket, address, address_len);
    }

    static int accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
    {
        return accept4_ptr(fd, addr, addrlen, flags);
    }

    static int bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
    {
        return bind_ptr(fd, addr, addrlen);
    }

    static int connect(int socket, const struct sockaddr *address, socklen_t address_len)
    {
        return connect_ptr(socket, address, address_len);
    }

    static int listen(int fd, int backlog)
    {
        return listen_ptr(fd, backlog);
    }

    static int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
    {
        return setsockopt_ptr(fd, level, optname, optval, optlen);
    }

    static int fcntl(int fd, int cmd, ...)
    {
        unsigned long int arg{0};
        va_list va;
        va_start(va, cmd);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return fcntl_ptr(fd, cmd, arg);
    }

    static int fcntl64(int fd, int cmd, ...)
    {
        unsigned long int arg{0};
        va_list va;
        va_start(va, cmd);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return fcntl64_ptr(fd, cmd, arg);
    }

    static int ioctl(int fd, unsigned long int request, ...)
    {
        unsigned long int arg{0};
        va_list va;
        va_start(va, request);
        arg = va_arg(va, decltype(arg));
        va_end(va);
        return ioctl_ptr(fd, request, arg);
    }

    static ssize_t read(int fd, void *buf, size_t nbytes)
    {
        return read_ptr(fd, buf, nbytes);
    }

    static ssize_t readv(int fildes, const struct iovec *iov, int iovcnt)
    {
        return readv_ptr(fildes, iov, iovcnt);
    }

    static ssize_t recv(int sockfd, void *buf, size_t size, int flags)
    {
        return recv_ptr(sockfd, buf, size, flags);
    }

    static ssize_t recvmsg(int fd, struct msghdr *message, int flags)
    {
        return recvmsg_ptr(fd, message, flags);
    }

    static ssize_t recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr *from, socklen_t *fromlen)
    {
        return recvfrom_ptr(fd, buf, n, flags, from, fromlen);
    }

    static ssize_t write(int fd, const void *buf, size_t n)
    {
        return write_ptr(fd, buf, n);
    }

    static ssize_t writev(int fildes, const struct iovec *iov, int iovcnt)
    {
        return writev_ptr(fildes, iov, iovcnt);
    }

    static ssize_t send(int sockfd, const void *buf, size_t size, int flags)
    {
        return send_ptr(sockfd, buf, size, flags);
    }

    static ssize_t sendmsg(int fd, const struct msghdr *message, int flags)
    {
        return sendmsg_ptr(fd, message, flags);
    }

    static ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr *to, socklen_t tolen)
    {
        return sendto_ptr(fd, buf, n, flags, to, tolen);
    }

    static ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
    {
        return sendfile_ptr(out_fd, in_fd, offset, count);
    }

    static ssize_t sendfile64(int out_fd, int in_fd, off64_t *offset, size_t count)
    {
        return sendfile64_ptr(out_fd, in_fd, offset, count);
    }

    static int epoll_create(int size)
    {
        return epoll_create_ptr(size);
    }

    static int epoll_create1(int flags)
    {
        return epoll_create1_ptr(flags);
    }

    static int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
    {
        return epoll_ctl_ptr(epfd, op, fd, event);
    }

    static int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)
    {
        return epoll_wait_ptr(epfd, events, maxevents, timeout);
    }

    static int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *sigmask)
    {
        return epoll_pwait_ptr(epfd, events, maxevents, timeout, sigmask);
    }

private:
    static void UnLoadInner() noexcept;

private:
    DL_API_DECLARE(creat);
    DL_API_DECLARE(open);
    DL_API_DECLARE(dup);
    DL_API_DECLARE(dup2);
    DL_API_DECLARE(pipe);
    DL_API_DECLARE(socket);
    DL_API_DECLARE(socketpair);
    DL_API_DECLARE(close);
    DL_API_DECLARE(shutdown);
    DL_API_DECLARE(accept);
    DL_API_DECLARE(accept4);
    DL_API_DECLARE(bind);
    DL_API_DECLARE(connect);
    DL_API_DECLARE(listen);
    DL_API_DECLARE(setsockopt);
    DL_API_DECLARE(getsockopt);
    DL_API_DECLARE(fcntl);
    DL_API_DECLARE(fcntl64);
    DL_API_DECLARE(ioctl);
    DL_API_DECLARE(getsockname);
    DL_API_DECLARE(getpeername);
    DL_API_DECLARE(read);
    DL_API_DECLARE(readv);
    DL_API_DECLARE(recv);
    DL_API_DECLARE(recvmsg);
    DL_API_DECLARE(recvmmsg);
    DL_API_DECLARE(recvfrom);
    DL_API_DECLARE(write);
    DL_API_DECLARE(writev);
    DL_API_DECLARE(send);
    DL_API_DECLARE(sendmsg);
    DL_API_DECLARE(sendmmsg);
    DL_API_DECLARE(sendto);
    DL_API_DECLARE(sendfile);
    DL_API_DECLARE(sendfile64);
    DL_API_DECLARE(select);
    DL_API_DECLARE(pselect);
    DL_API_DECLARE(poll);
    DL_API_DECLARE(ppoll);
    DL_API_DECLARE(epoll_create);
    DL_API_DECLARE(epoll_create1);
    DL_API_DECLARE(epoll_ctl);
    DL_API_DECLARE(epoll_wait);
    DL_API_DECLARE(epoll_pwait);
    DL_API_DECLARE(fork);
    DL_API_DECLARE(vfork);
    DL_API_DECLARE(daemon);
    DL_API_DECLARE(sigaction);
    DL_API_DECLARE(signal);

    static std::mutex LOAD_MUTEX;
    static bool LOADED;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_DL_LIBC_API_H
