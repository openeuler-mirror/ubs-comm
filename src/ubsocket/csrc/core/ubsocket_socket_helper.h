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
#ifndef UBS_COMM_UBSOCKET_SOCKET_HELPER_H
#define UBS_COMM_UBSOCKET_SOCKET_HELPER_H

#include "common/ubsocket_common_includes.h"

namespace ock {
namespace ubs {
class SocketConnHelper {
    SocketConnHelper() = delete;

public:
    static bool IsBlocking(int fd);
    static int SetNonBlocking(int fd);
    static int SetBlocking(int fd);
    static ssize_t SendSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms);
    static ssize_t RecvSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms);
    static void FlushSocketMsg(int fd);
    static bool IsTfoConnection(const int &fd);
    static int SetTcpNoDelay(int fd);
    static std::string ExtractIpFromSockAddr(const struct sockaddr *address);
    template <typename TimePoint>
    static ALWAYS_INLINE bool IsTimeout(TimePoint &start, uint32_t timeout_ms)
    {
        TimePoint end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end-start);
        return duration.count() > timeout_ms;
    }
};
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UBSOCKET_SOCKET_HELPER_H
