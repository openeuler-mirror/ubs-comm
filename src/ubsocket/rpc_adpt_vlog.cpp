/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-28
 *Note:
 *History: 2025-07-28
*/

#include "rpc_adpt_vlog.h"

static util_vlog_ctx_t g_rpc_adpt_vlog_ctx = {
    .level = UTIL_VLOG_LEVEL_INFO,
    .vlog_name = "RPC_ADPT",
    .vlog_output_func = default_vlog_output,
    .rate_limited = {
        .interval_ms = UTIL_VLOG_PRINT_PERIOD_MS,
        .num = UTIL_VLOG_PRINT_TIMES,
    }
};

util_vlog_ctx_t *RpcAdptGetLogCtx(void)
{
    return &g_rpc_adpt_vlog_ctx;
}

