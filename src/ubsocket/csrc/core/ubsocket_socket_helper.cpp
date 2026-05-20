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

#include "ubsocket_socket_helper.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <under_api/dl_libc_api.h>

namespace ock {
namespace ubs {
bool SocketConnHelper::IsTfoConnection(const int &fd)
{
    if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::TFO) {
        tcp_info info{};
        socklen_t len = sizeof(info);
        bool is_tfo_connection = false;
        if (LibcApi::getsockopt(fd, SOL_TCP, TCP_INFO, &info, &len) == 0) {
            // check TCPI_OPT_SYN_DATA
            is_tfo_connection = (info.tcpi_options & TCPI_OPT_SYN_DATA) != 0;
        }
        UBS_VLOG_INFO("Current tcpi_options: 0x%x, tfo connection: %s \n", info.tcpi_options,
                      is_tfo_connection ? "true" : "false");
        return is_tfo_connection;
    } else if (GlobalSetting::UBS_HAND_SHAKE_MODE == UBHandshakeMode::UB_SOCK_OPT) {
        uint64_t opt = -1;
        socklen_t len = sizeof(opt);
        if (LibcApi::getsockopt(fd, IPPROTO_TCP, TCP_UB_SOCKET_HANDSHAKE, &opt, &len) == 0) {
            UBS_VLOG_INFO("Current handshake opt: %lx, tfo connection: %s \n", opt,
                          opt == CONTROL_PLANE_PROTOCOL_NEGOTIATION ? "true" : "false");
            return opt == CONTROL_PLANE_PROTOCOL_NEGOTIATION;
        }
        return false;
    }
    UBS_VLOG_WARN("Unsupported handshake mode: 0x%x\n", static_cast<unsigned int>(GlobalSetting::UBS_HAND_SHAKE_MODE));
    return false;
}

int SetSockOpt(int fd, int level, int optname, const void *optval, socklen_t optlen)
{
    UBS_VLOG_INFO("SetSockOpt set fd %d, level %d, optname %d\n", fd, level, optname);
    return LibcApi::setsockopt(fd, level, optname, optval, optlen);
}

int SocketConnHelper::SetTcpNoDelay(int fd)
{
    int on = 1;
    return SetSockOpt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

std::string SocketConnHelper::ExtractIpFromSockAddr(const struct sockaddr *address)
{
    if (address == nullptr) {
        return "";
    }
    char ip_str[INET6_ADDRSTRLEN] = {0};
    const char *result = nullptr;
    if (address->sa_family == AF_INET) {
        const sockaddr_in *addr_in = reinterpret_cast<const sockaddr_in *>(address);
        result = inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, INET_ADDRSTRLEN);
    } else if (address->sa_family == AF_INET6) {
        const sockaddr_in6 *addr_in6 = reinterpret_cast<const sockaddr_in6 *>(address);
        result = inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, INET6_ADDRSTRLEN);
    }
    return (result != nullptr) ? std::string(ip_str) : "";
}

bool SocketConnHelper::IsBlocking(int fd)
{
    const int flags = LibcApi::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && !(flags & O_NONBLOCK);
}

int SocketConnHelper::SetNonBlocking(int fd)
{
    const int flags = LibcApi::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return flags;
    }
    if (flags & O_NONBLOCK) {
        return 0;
    }
    return LibcApi::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

ssize_t SocketConnHelper::SendSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    errno = 0;
    char *cur = (char *)(uintptr_t)buf;
    ssize_t sent = 0;
    size_t total = size;
    auto start = std::chrono::high_resolution_clock::now();
    while (total != 0) {
        if (IsTimeout(start, timeout_ms)) {
            errno = ETIMEDOUT;
            return sent;
        }
        sent = LibcApi::send(fd, cur, total, MSG_NOSIGNAL);
        if (errno == EAGAIN) {
            // reset errno to 0
            errno = 0;
            if (sent > 0) {
                total -= sent;
                cur += sent;
            }
            continue;
        }
        if (sent <= 0 || errno != 0) {
            UBS_VLOG_ERR("send() failed, ret: %zd, errno: %d, errmsg: %s, sent: %zd\n", sent, errno,
                         Func::Error2Str(errno), sent);
            return sent;
        } else {
            UBS_VLOG_DEBUG("Send socket message successful, fd: %d, sent = %zd, total: %zu\n", fd, sent, size);
        }
        total -= sent;
        cur += sent;
    }
    return size;
}

ssize_t SocketConnHelper::RecvSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms)
{
    // reset errno to 0
    errno = 0;
    char *cur = (char *)(uintptr_t)buf;
    ssize_t received = 0;
    size_t total = size;
    auto start = std::chrono::high_resolution_clock::now();
    while (total != 0) {
        if (IsTimeout(start, timeout_ms)) {
            errno = ETIMEDOUT;
            return received;
        }
        received = LibcApi::recv(fd, cur, total, MSG_NOSIGNAL);
        if (errno == EAGAIN) {
            // reset errno to 0
            errno = 0;
            if (received > 0) {
                total -= received;
                cur += received;
            }

            continue;
        }
        if (received == 0) {
            UBS_VLOG_INFO("The connection has been closed by peer.\n");
            return 0;
        } else if (received < 0) {
            UBS_VLOG_ERR("recv() failed, ret: %zd, errno: %d, errmsg: %s, received: %zd, fd: %d\n", received, errno,
                         Func::Error2Str(errno), received, fd);
            return received;
        } else {
            UBS_VLOG_DEBUG("Receive socket message successful, fd: %d, received: %zd, total: %zu\n", fd, received,
                           size);
        }
        total -= received;
        cur += received;
    }
    return size;
}

void SocketConnHelper::FlushSocketMsg(int fd)
{
    // reset errno to 0
    errno = 0;
    char tmp_buf[FLUSH_SOCKET_MSG_BUFFER_LEN];
    ssize_t received = 0;
    do {
        received = LibcApi::recv(fd, tmp_buf, FLUSH_SOCKET_MSG_BUFFER_LEN, MSG_NOSIGNAL);
        if (errno == EAGAIN || errno == EINTR) {
            // reset errno to 0
            errno = 0;
            continue;
        }

        if (received < 0 || errno != 0) {
            return;
        }
    } while (received > 0);
}

int SocketConnHelper::SetBlocking(int fd)
{
    const int flags = LibcApi::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return flags;
    }

    if (flags & O_NONBLOCK) {
        return LibcApi::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }

    return 0;
}
} // namespace ubs
} // namespace ock