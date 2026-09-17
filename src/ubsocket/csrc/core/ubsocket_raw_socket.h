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
#ifndef UBS_COMM_RAW_SOCKET_H
#define UBS_COMM_RAW_SOCKET_H

#include "core/ubsocket_socket.h"

namespace ock {
namespace ubs {
class RawSocket : public SocketBase {
public:
    explicit RawSocket(int fd) noexcept : SocketBase(fd, SocketType::SOCK_TYPE_TCP){}
    ~RawSocket() override {}

    ALWAYS_INLINE int Accept(const SocketPtr &sock, struct sockaddr *address, socklen_t *address_len) override
    {
        int fd = LibcApi::accept(sock->Fd(), address, address_len);
        if (fd < 0) {
            return fd;
        }
        SocketPtr socketPtr;
        int ret = SocketBase::Create(fd, SocketType::SOCK_TYPE_TCP, socketPtr);
        if (ret != UBS_OK) {
            UBS_VLOG_ERR("CreateSocketFd() failed, fd: %d, ret: %d\n", fd, ret);
            LibcApi::close(fd);
            return -1;
        }
        socketPtr->create_type_ = SOCK_CREATE_TYPE_ACCEPT;
        ArraySet<Socket>::GetInstance().OverrideItem(fd, socketPtr.Get());
        return fd;
    }

    ALWAYS_INLINE int Connect(SocketPtr &sock, const struct sockaddr *address, socklen_t address_len) override
    {
        return LibcApi::connect(sock->Fd(), address, address_len);
    }

    ALWAYS_INLINE ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt) override
    {
        return LibcApi::writev(sock->Fd(), iov, iovcnt);
    }

    ALWAYS_INLINE ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt) override
    {
        return LibcApi::readv(sock->Fd(), iov, iovcnt);
    }

    ALWAYS_INLINE ssize_t Send(const SocketPtr &sock, const void *buf, size_t len, int flags) override
    {
        return LibcApi::send(sock->Fd(), buf, len, flags);
    }

    ALWAYS_INLINE ssize_t Recv(const SocketPtr &sock, void *buf, size_t len, int flags) override
    {
        return LibcApi::recv(sock->Fd(), buf, len, flags);
    }

    ALWAYS_INLINE int GetSockOpt(int fd, int level, int optname, void *optval, socklen_t *optlen) override
    {
        return LibcApi::getsockopt(fd, level, optname, optval, optlen);
    }

private:
    Result Initialize() noexcept override {}
    void UnInitialize() noexcept override {}
    bool IsBindRemote() override {}
};

using RawSocketPtr = Ref<RawSocket>;

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_RAW_SOCKET_H
