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

#include "ubsocket_logger.h"
#include "ubsocket_obj_statistics.h"
#include "ubsocket_signal_handler.h"

namespace ock {
namespace ubs {
void ubsocket_handle_signal(int signal)
{
    if (signal != SIGUSR2) {
        return;
    }

    /* dump object */
    UBS_SLOG_ERR(ObjectStatistics::Instance().DumpStr());
}
} // namespace ubs
} // namespace ock