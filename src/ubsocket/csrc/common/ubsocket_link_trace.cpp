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
#include "ubsocket_link_trace.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

namespace ock {
namespace ubs {
std::atomic<int> LinkTrace::state_{0};
std::atomic<uint64_t> LinkTrace::base_us_{0};

long LinkTrace::AcceptQueueWaitMs(int fd) noexcept
{
#if defined(__linux__) && defined(TCP_INFO)
    struct tcp_info info;
    socklen_t len = sizeof(info);
    if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len) != 0 || len < sizeof(info)) {
        return -1;
    }
    /* last ACK from the peer = the 3WHS ACK (or its first data segment); the
     * time since then is how long the connection waited in the accept queue */
    return static_cast<long>(info.tcpi_last_ack_recv);
#else
    (void)fd;
    return -1;
#endif
}

long LinkTrace::PendingBytes(int fd) noexcept
{
#ifdef FIONREAD
    int n = 0;
    if (ioctl(fd, FIONREAD, &n) != 0) {
        return -1;
    }
    return n;
#else
    (void)fd;
    return -1;
#endif
}
} // namespace ubs
} // namespace ock
