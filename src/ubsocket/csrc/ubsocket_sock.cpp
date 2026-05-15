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
#include "dl_libc_api.h"
#include "ubsocket.h"
#include "ubsocket_common_includes.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_socket.h"

using namespace ock::ubs;

UBS_API int UB_API_WRAP(socket)(int domain, int type, int protocol)
{
    return 0;
}

UBS_API int UB_API_WRAP(shutdown)(int fd, int how)
{
    return 0;
}

UBS_API int UB_API_WRAP(close)(int fd)
{
    return 0;
}

UBS_API int UB_API_WRAP(accept)(int socket, struct sockaddr *address, socklen_t *address_len)
{
    if (!GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::accept(socket, address, address_len);
    }

    Socket *sock = SocketSet::GetInstance().GetSocket(socket);
    if (sock == nullptr) {
        return LibcApi::accept(socket, address, address_len);
    }
    return sock->Accept(*sock, address, address_len);
}

UBS_API int UB_API_WRAP(accept4)(int socket, struct sockaddr *address, socklen_t *address_len, int flag)
{
    return 0;
}

UBS_API int UB_API_WRAP(listen)(int fd, int backlog)
{
    return 0;
}

UBS_API int UB_API_WRAP(connect)(int socket, const struct sockaddr *address, socklen_t address_len)
{
    if (!GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::connect(socket, address, address_len);
    }

    Socket *sock = SocketSet::GetInstance().GetSocket(socket);
    if (sock == nullptr) {
        return LibcApi::connect(socket, address, address_len);
    }
    return sock->Connect(*sock, address, address_len);
}
UBS_API ssize_t UB_API_WRAP(readv)(int fildes, const struct iovec *iov, int iovcnt)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(writev)(int fildes, const struct iovec *iov, int iovcnt)
{
    // 1.从map根据fd获取socket , 然后获取tx
    // tx->WriteV(sock, &iov, iovcnt);
    return 0;
}

UBS_API ssize_t UB_API_WRAP(send)(int sockfd, const void *buf, size_t len, int flags)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(recv)(int sockfd, void *buf, size_t len, int flags)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(read)(int fildes, void *buf, size_t nbyte)
{
    // 1.从map根据fd获取socket , 然后获取rx
    // tx->ReadV(&iov, iovcnt);
    return 0;
}

UBS_API ssize_t UB_API_WRAP(write)(int fildes, const void *buf, size_t nbyte)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendto)(int sockfd, const void *buf, size_t len, int flags,
                                    const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(recvfrom)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                                      socklen_t *addrlen)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendmsg)(int sockfd, const struct msghdr *msg, int flags)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(recvmsg)(int sockfd, struct msghdr *msg, int flags)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendfile)(int out_fd, int in_fd, off_t *offset, size_t count)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count)
{
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
    return 0;
}
