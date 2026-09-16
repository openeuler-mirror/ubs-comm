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
#include <csignal>

#include "ubsocket_signal_handler.h"

namespace ock {
namespace ubs {

/* volatile sig_atomic_t — POSIX 标准信号安全标志类型：
 * 信号处理器仅对此标志赋值（1），保证 async-signal-safe。
 * 实际的 DumpStr / 日志输出由 ConsumeDumpRequest() 在普通线程上下文完成。 */
static volatile sig_atomic_t g_dumpRequested = 0;

void ubsocket_handle_signal(int signal)
{
    if (signal != SIGUSR2) {
        return;
    }

    g_dumpRequested = 1;
}

bool ConsumeDumpRequest() noexcept
{
    if (g_dumpRequested) {
        g_dumpRequested = 0;
        return true;
    }
    return false;
}
} // namespace ubs
} // namespace ock