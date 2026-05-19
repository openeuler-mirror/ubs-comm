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
#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_data_tx.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_set.h"
#include "include/ubsocket.h"
#include "under_api/dl_libc_api.h"

using namespace ock::ubs;
UBS_API int UB_API_WRAP(socket)(int domain, int type, int protocol)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::socket(domain, type, protocol);
    }

    return 0;
}

UBS_API int UB_API_WRAP(shutdown)(int fd, int how)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::shutdown(fd, how);
    }

    return 0;
}

UBS_API int UB_API_WRAP(close)(int fd)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::close(fd);
    }

    return 0;
}

UBS_API int UB_API_WRAP(accept)(int fd, struct sockaddr *address, socklen_t *address_len)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::accept(fd, address, address_len);
    }

    SocketPtr sock = SocketSet::Instance().GetSocket(fd);
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

    return 0;
}

UBS_API int UB_API_WRAP(bind)(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::bind(fd, addr, addrlen);
    }

    return 0;
}

UBS_API int UB_API_WRAP(listen)(int fd, int backlog)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::listen(fd, backlog);
    }

    return 0;
}

UBS_API int UB_API_WRAP(connect)(int fd, const struct sockaddr *address, socklen_t address_len)
{
    if (!GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::connect(fd, address, address_len);
    }

    SocketPtr sock = SocketSet::Instance().GetSocket(fd);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    if (sockBase == nullptr) {
        return LibcApi::connect(fd, address, address_len);
    }

    return sockBase->Connect(sock, address, address_len);

    return 0;
}

UBS_API ssize_t UB_API_WRAP(readv)(int fd, const struct iovec *iov, int iovcnt)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::readv(fd, iov, iovcnt);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(writev)(int fd, const struct iovec *iov, int iovcnt)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::writev(fd, iov, iovcnt);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(send)(int fd, const void *buf, size_t len, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::send(fd, buf, len, flags);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(recv)(int fd, void *buf, size_t len, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recv(fd, buf, len, flags);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(read)(int fd, void *buf, size_t nbyte)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::read(fd, buf, nbyte);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(write)(int fd, const void *buf, size_t nbyte)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::write(fd, buf, nbyte);
    }
    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendto)(int fd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                                    socklen_t addrlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::sendto(fd, buf, len, flags, dest_addr, addrlen);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(recvfrom)(int fd, void *buf, size_t len, int flags, struct sockaddr *dest_addr,
                                      socklen_t *addrlen)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recvfrom(fd, buf, len, flags, dest_addr, addrlen);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendmsg)(int fd, const struct msghdr *msg, int flags)
{
    return 0;
}

UBS_API ssize_t UB_API_WRAP(recvmsg)(int fd, struct msghdr *msg, int flags)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::recvmsg(fd, msg, flags);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendfile)(int out_fd, int in_fd, off_t *offset, size_t count)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::sendfile64(out_fd, in_fd, offset, count);
    }

    return 0;
}

UBS_API ssize_t UB_API_WRAP(sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count)
{
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::sendfile64(out_fd, in_fd, offset, count);
    }

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
    if (GlobalSetting::UBS_NATIVE_TCP_MODE) {
        return LibcApi::setsockopt(fd, level, optname, optval, optlen);
    }

    return 0;
}