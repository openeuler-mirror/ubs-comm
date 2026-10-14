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
#include "ubsocket_fd_table.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include "ubsocket_logger.h"

namespace ock {
namespace ubs {

uint32_t ClampFdTableTarget(uint32_t want, uint64_t rlim_cur) noexcept
{
    if (want == 0 || rlim_cur == 0) {
        return want;
    }
    if (want >= rlim_cur) {
        /* 最大可用 fd 号是 rlim_cur - 1；软上限为 1 时无事可做 */
        return rlim_cur > 1 ? static_cast<uint32_t>(rlim_cur - 1) : 0;
    }
    return want;
}

uint32_t PreExpandFdTable(uint32_t want) noexcept
{
    if (want == 0) {
        return 0;
    }
    struct rlimit rl {};
    uint64_t soft = 0;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY) {
        soft = static_cast<uint64_t>(rl.rlim_cur);
    }
    const uint32_t target = ClampFdTableTarget(want, soft);
    if (target == 0) {
        UBS_VLOG_WARN("pre-expand fd table skipped: want %u, RLIMIT_NOFILE soft %llu\n", want,
                      static_cast<unsigned long long>(soft));
        return 0;
    }
    /* 这里直接用 libc：本函数在 DlApi::Load 之前调用，且 ubsocket 不拦截 open/fcntl/close */
    const int dummy = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (dummy < 0) {
        UBS_VLOG_WARN("pre-expand fd table: open /dev/null failed, errno %d\n", errno);
        return 0;
    }
    const int high = ::fcntl(dummy, F_DUPFD_CLOEXEC, static_cast<int>(target));
    const int dup_errno = errno;
    uint32_t done = 0;
    if (high < 0) {
        UBS_VLOG_WARN("pre-expand fd table to %u failed, errno %d\n", target, dup_errno);
    } else {
        (void)::close(high);
        done = target;
        UBS_VLOG_INFO("fd table pre-expanded: want %u, target %u (RLIMIT_NOFILE soft %llu)\n", want, target,
                      static_cast<unsigned long long>(soft));
    }
    (void)::close(dummy);
    return done;
}

} // namespace ubs
} // namespace ock
