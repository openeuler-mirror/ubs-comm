// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
// Description: Provide the utility for cli client, etc

#ifndef UBSOCKET_BRPC_SOCKET_ADAPTER_H
#define UBSOCKET_BRPC_SOCKET_ADAPTER_H

#include <sys/eventfd.h>
#include <unistd.h>

#include "socket_adapter.h"

#define UB_API_WRAP(name) ubsocket_##name

EXPOSE_C_DEFINE int UB_API_WRAP(socket)(int domain, int type, int protocol);
EXPOSE_C_DEFINE int UB_API_WRAP(shutdown)(int fd, int how);
EXPOSE_C_DEFINE int UB_API_WRAP(close)(int fd);
EXPOSE_C_DEFINE int UB_API_WRAP(accept)(int socket, struct sockaddr *address, socklen_t *address_len);
EXPOSE_C_DEFINE int UB_API_WRAP(accept4)(int socket, struct sockaddr *address, socklen_t *address_len, int flag);
EXPOSE_C_DEFINE int UB_API_WRAP(listen)(int fd, int backlog);
EXPOSE_C_DEFINE int UB_API_WRAP(connect)(int socket, const struct sockaddr *address, socklen_t address_len);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(readv)(int fildes, const struct iovec *iov, int iovcnt);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(writev)(int fildes, const struct iovec *iov, int iovcnt);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(send)(int sockfd, const void *buf, size_t len, int flags);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(recv)(int sockfd, void *buf, size_t len, int flags);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(read)(int fildes, void *buf, size_t nbyte);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(write)(int fildes, const void *buf, size_t nbyte);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendto)(int sockfd, const void *buf, size_t len, int flags,
                                            const struct sockaddr *dest_addr, socklen_t addrlen);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(recvfrom)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                                              socklen_t *addrlen);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendmsg)(int sockfd, const struct msghdr *msg, int flags);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(recvmsg)(int sockfd, struct msghdr *msg, int flags);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendfile)(int out_fd, int in_fd, off_t *offset, size_t count);
EXPOSE_C_DEFINE ssize_t UB_API_WRAP(sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count);
EXPOSE_C_DEFINE int UB_API_WRAP(fcntl)(int fd, int cmd, ...);
EXPOSE_C_DEFINE int UB_API_WRAP(fcntl64)(int fd, int cmd, ...);
EXPOSE_C_DEFINE int UB_API_WRAP(ioctl)(int fd, unsigned long request, ...);
EXPOSE_C_DEFINE int UB_API_WRAP(setsockopt)(int fd, int level, int optname, const void *optval, socklen_t optlen);
EXPOSE_C_DEFINE int UB_API_WRAP(epoll_create)(int size);
EXPOSE_C_DEFINE int UB_API_WRAP(epoll_create1)(int flags);
EXPOSE_C_DEFINE int UB_API_WRAP(epoll_ctl)(int epfd, int op, int fd, struct epoll_event *event);
EXPOSE_C_DEFINE int UB_API_WRAP(epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout);
EXPOSE_C_DEFINE int UB_API_WRAP(epoll_pwait)(int epfd, struct epoll_event *events, int maxevents, int timeout,
                                             const sigset_t *sigmask);

#endif  // UBSOCKET_BRPC_SOCKET_ADAPTER_H
