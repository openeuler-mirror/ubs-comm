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

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/tcp.h>
#include <fstream>

#include "common/ubsocket_common_includes.h"
#include "under_api/dl_libc_api.h"

namespace ock {
namespace ubs {
class SocketConnHelper {
public:
    SocketConnHelper() = delete;

public:
    /**
     * Get and set blocking option
     */
    static bool IsBlocking(int fd);
    static int SetBlocking(int fd);
    static int SetNonBlocking(int fd);

    /**
     * Send data and recv data with in timeout period
     */
    static ssize_t SendSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms);
    static ssize_t RecvSocketData(int fd, const void *buf, size_t size, uint32_t timeout_ms);
    static void FlushSocketMsg(int fd);

    static bool IsUbsConnection(const int &fd);

    static int SetTcpNoDelay(int fd);

    /**
     * Length-prefixed wire helpers: [4B body_len][body]
     * On success returns UBS_OK, on failure returns UBS_ERROR.
     * RecvLengthPrefixed reads min(body_len, obj_size), zero-fills tail if shorter, discards if longer.
     * Both functions log failure details internally (phase, fd, sizes, errno).
     */
    static Result SendLengthPrefixed(int fd, const void *body, uint32_t obj_size, uint32_t timeout_ms);
    static Result RecvLengthPrefixed(int fd, void *body, uint32_t obj_size, uint32_t timeout_ms);

    static std::string ExtractIpFromSockAddr(const struct sockaddr *address);
    static uint16_t ExtractPortFromSockAddr(const struct sockaddr *address);
    static int GetCurrentProcessSocketId();
    // 获取所有 Socket ID
    static std::vector<uint32_t> GetSocketIdsViaNumaSysfs();

    static bool IsTimeout(uint64_t start_ms, uint32_t timeout_ms);
    static uint64_t GetTimeMs();

private:
    static int SetSockOpt(int fd, int level, int optname, const void *optval, socklen_t optlen) noexcept;

    // 从 CPU ID 获取其 Socket ID（physical_package_id）
    static int GetSocketIdOfCpu(int cpu);
    // 通过 NUMA 节点获取所有 Socket ID
    static std::vector<uint32_t> GetSocketIdsViaNuma();
    // CPU 扫描方式获取 Socket IDs
    static std::vector<uint32_t> GetSocketIdsViaCpuScan();
    // 解析cpulist字符串
    static int GetFirstCpuFromCpulist(const std::string &cpuListStr);

#if defined(ENABLE_CPU_MONOTONIC) && defined(__aarch64__)
    static uint32_t InitTickMs()
    {
        uint64_t tmpFreq = 0;
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(tmpFreq));
        auto freq = static_cast<uint32_t>(tmpFreq);

        /* calculate */
        freq = freq / 1000U;
        if (freq == 0) {
            UBS_VLOG_ERR("Failed to get tick as freq is 0");
            return 1;
        }

        return freq;
    }
#endif
};

ALWAYS_INLINE int SocketConnHelper::SetBlocking(int fd)
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

ALWAYS_INLINE bool SocketConnHelper::IsBlocking(int fd)
{
    const int flags = LibcApi::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && !(flags & O_NONBLOCK);
}

ALWAYS_INLINE int SocketConnHelper::SetNonBlocking(int fd)
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

ALWAYS_INLINE int SocketConnHelper::SetSockOpt(int fd, int level, int optname, const void *optval,
                                               socklen_t optlen) noexcept
{
    UBS_VLOG_DEBUG("SetSockOpt set fd %d, level %d, optname %d", fd, level, optname);
    return LibcApi::setsockopt(fd, level, optname, optval, optlen);
}

ALWAYS_INLINE int SocketConnHelper::SetTcpNoDelay(int fd)
{
    constexpr int on = 1;
    return SetSockOpt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
}

ALWAYS_INLINE bool SocketConnHelper::IsTimeout(uint64_t start_ms, uint32_t timeout_ms)
{
    uint64_t now_ms = GetTimeMs();
    return (now_ms - start_ms) > timeout_ms;
}

ALWAYS_INLINE uint64_t SocketConnHelper::GetTimeMs()
{
#if defined(ENABLE_CPU_MONOTONIC) && defined(__aarch64__)
    const static uint32_t tick_per_ms = InitTickMs();
    uint64_t timeValue = 0;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(timeValue));
    return timeValue / tick_per_ms;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (static_cast<uint64_t>(ts.tv_sec)) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
#endif
}

} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UBSOCKET_SOCKET_HELPER_H
