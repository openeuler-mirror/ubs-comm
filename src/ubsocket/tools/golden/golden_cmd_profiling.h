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
#ifndef UBS_COMM_GOLDEN_CMD_PROFILING_H
#define UBS_COMM_GOLDEN_CMD_PROFILING_H

#include "golden_common.h"
#include "profiling/impl/ubsocket_prof_tracer.h"
#include "profiling/ubsocket_prof.h"

namespace golden {
enum GoldenProfEnum
{
    CONN_URMA_CREATE_JFC = 0,
    CONN_URMA_CREATE_JFS,
    CONN_URMA_CREATE_JFR,
    CONN_URMA_CREATE_JETTY,
    CONN_URMA_EXCHANGE_JETTY,
    CONN_URMA_IMPORT_JETTY,
    CONN_URMA_TOTAL,

    CMD_PROF_COUNT,
};

class GoldenProfiling {
public:
    static void Init()
    {
        ubsocket_prof_option_t option{};
        option.tracepoint_count = CMD_PROF_COUNT + 1;
        option.enable_dump = 0;

        ubsocket_prof_init(&option);
    }
};
} // namespace golden

#endif // UBS_COMM_GOLDEN_CMD_PROFILING_H
