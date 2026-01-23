/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-28
 *Note:
 *History: 2025-07-28
*/
#include <ctime>
#include <sys/time.h>

#include "umq_types.h"
#include "umq_api.h"
#include "rpc_adpt_vlog.h"

static util_vlog_ctx_t g_rpc_adpt_vlog_ctx = {
    UTIL_VLOG_LEVEL_INFO,
    "RPC_ADPT",
    default_vlog_output,
    {
        UTIL_VLOG_PRINT_PERIOD_MS,
        UTIL_VLOG_PRINT_TIMES,
    }
};

static const char *g_log_level_to_str[UMQ_LOG_LEVEL_MAX] = {"EMERG", "ALERT", "CRIT", "ERROR", "WARNING",
                                                            "NOTICE", "INFO", "DEBUG"};

static void DefaultPrintfOutput(int level, char *logMsg)
{
    struct timeval tval;
    struct tm time;
    (void)gettimeofday(&tval, nullptr);
    (void)localtime_r(&tval.tv_sec, &time);
    (void)fprintf(stdout, "%02d%02d %02d:%02d:%02d.%06ld|%s|%s", time.tm_mon + 1, time.tm_mday, time.tm_hour,
        time.tm_min, time.tm_sec, (long)tval.tv_usec, g_log_level_to_str[level], logMsg);
}

int RpcAdptSetLogCtx(util_vlog_level_t level)
{
    g_rpc_adpt_vlog_ctx.vlog_output_func = DefaultPrintfOutput;

    umq_log_config_t log_cfg = {
        .log_flag = UMQ_LOG_FLAG_FUNC | UMQ_LOG_FLAG_LEVEL,
        .func = DefaultPrintfOutput,
        .level = static_cast<umq_log_level_t>(level)
    };

    int ret = umq_log_config_set(&log_cfg);
    return ret;
}

util_vlog_ctx_t *RpcAdptGetLogCtx(void)
{
    return &g_rpc_adpt_vlog_ctx;
}
