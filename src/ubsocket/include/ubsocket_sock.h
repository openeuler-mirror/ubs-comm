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
#ifndef UBS_COMM_REFACTOR_BAO_UBSOCKET_SOCK_H
#define UBS_COMM_REFACTOR_BAO_UBSOCKET_SOCK_H

#include "ubsocket_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * socket compatible api
 */
int UB_API_WRAP(socket)(int domain, int type, int protocol);
int UB_API_WRAP(shutdown)(int fd, int how);
int UB_API_WRAP(close)(int fd);
int UB_API_WRAP(accept)(int socket, struct sockaddr *address, socklen_t *address_len);
int UB_API_WRAP(accept4)(int socket, struct sockaddr *address, socklen_t *address_len, int flag);
int UB_API_WRAP(listen)(int fd, int backlog);
int UB_API_WRAP(connect)(int socket, const struct sockaddr *address, socklen_t address_len);
ssize_t UB_API_WRAP(readv)(int fildes, const struct iovec *iov, int iovcnt);
ssize_t UB_API_WRAP(writev)(int fildes, const struct iovec *iov, int iovcnt);
ssize_t UB_API_WRAP(send)(int sockfd, const void *buf, size_t len, int flags);
ssize_t UB_API_WRAP(recv)(int sockfd, void *buf, size_t len, int flags);
ssize_t UB_API_WRAP(read)(int fildes, void *buf, size_t nbyte);
ssize_t UB_API_WRAP(write)(int fildes, const void *buf, size_t nbyte);
ssize_t UB_API_WRAP(sendto)(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                            socklen_t addrlen);
ssize_t UB_API_WRAP(recvfrom)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                              socklen_t *addrlen);
ssize_t UB_API_WRAP(sendmsg)(int sockfd, const struct msghdr *msg, int flags);
ssize_t UB_API_WRAP(recvmsg)(int sockfd, struct msghdr *msg, int flags);
ssize_t UB_API_WRAP(sendfile)(int out_fd, int in_fd, off_t *offset, size_t count);
ssize_t UB_API_WRAP(sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count);
int UB_API_WRAP(setsockopt)(int fd, int level, int optname, const void *optval, socklen_t optlen);

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_REFACTOR_BAO_UBSOCKET_SOCK_H
