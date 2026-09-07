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

/* This file provides LD_PRELOAD transparent hijacking support.
 *
 * When compiled with UBSOCKET_ENABLE_INTERCEPT defined, it exports standard
 * POSIX symbol names (socket, close, read, write, etc.) that intercept
 * application calls and route them to the ubsocket_* implementation.
 *
 * Usage:
 *   cmake -DUBSOCKET_ENABLE_INTERCEPT=ON ...
 *   env LD_PRELOAD=/path/to/libubsocket.so \
 *       UBSOCKET_TRANS_MODE=ub \
 *       UBSOCKET_DEV_NAME="bonding_dev_0" \
 *       ./application
 *
 * The library auto-initializes via __attribute__((constructor)) when loaded
 * via LD_PRELOAD, so applications need no source code changes.
 */

#ifdef UBSOCKET_ENABLE_INTERCEPT

#include <cstdarg>
#include <cstdlib>
#include <sys/socket.h>

#include "common/ubsocket_common_includes.h"
#include "include/ubsocket.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;

/* Auto-initialization when loaded via LD_PRELOAD.
 * This constructor runs before main(), initializing the ubsocket library
 * so that intercepted POSIX calls can be routed to UB transport. */
__attribute__((constructor)) static void ubsocket_preload_init(void)
{
    /* Only auto-init when loaded via LD_PRELOAD */
    if (getenv("LD_PRELOAD") == nullptr) {
        return;
    }

    /* Load libc function pointers first (needed by LibcApi) */
    (void)DlApi::Load(LOAD_LIBC);

    u_init_options_t options;
    if (ubsocket_init_options(&options) != UBS_OK) {
        UBS_VLOG_ERR("LD_PRELOAD: ubsocket_init_options failed\n");
        return;
    }

    /* Enable UB protocol for LD_PRELOAD mode */
    options.allowed_protocol = UBS_PROTOCOL_UB_RM_RTP | UBS_PROTOCOL_UB_RC_RTP;

    if (ubsocket_init(&options) != UBS_OK) {
        UBS_VLOG_ERR("LD_PRELOAD: ubsocket_init failed, fallback to native TCP mode\n");
        GlobalSetting::UBS_NATIVE_TCP_MODE = true;
    }
}

/* Destructor to clean up on process exit */
__attribute__((destructor)) static void ubsocket_preload_fini(void)
{
    if (GlobalSetting::UBS_INITED) {
        ubsocket_uninit();
    }
}

/* Export standard POSIX symbols that intercept application calls.
 * Each symbol delegates to the corresponding ubsocket_* function. */

#define EXPOSE_C_DEFINE extern "C" __attribute__((visibility("default")))

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

EXPOSE_C_DEFINE int bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    return UB_API_WRAP(bind)(fd, addr, addrlen);
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

EXPOSE_C_DEFINE ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
                               const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return UB_API_WRAP(sendto)(sockfd, buf, len, flags, dest_addr, addrlen);
}

EXPOSE_C_DEFINE ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
                                 struct sockaddr *dest_addr, socklen_t *addrlen)
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
    unsigned long int arg{0};
    va_list va;
    va_start(va, cmd);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    return UB_API_WRAP(fcntl)(fd, cmd, arg);
}

EXPOSE_C_DEFINE int fcntl64(int fd, int cmd, ...)
{
    unsigned long int arg{0};
    va_list va;
    va_start(va, cmd);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    return UB_API_WRAP(fcntl64)(fd, cmd, arg);
}

EXPOSE_C_DEFINE int ioctl(int fd, unsigned long request, ...)
{
    unsigned long int arg{0};
    va_list va;
    va_start(va, request);
    arg = va_arg(va, decltype(arg));
    va_end(va);
    return UB_API_WRAP(ioctl)(fd, request, arg);
}

EXPOSE_C_DEFINE int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    return UB_API_WRAP(setsockopt)(fd, level, optname, optval, optlen);
}

EXPOSE_C_DEFINE int getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen)
{
    return UB_API_WRAP(getsockopt)(fd, level, optname, optval, optlen);
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

#endif /* UBSOCKET_ENABLE_INTERCEPT */
