/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide vlog module
 * Create: 2025-07-29
 */

#include <stdarg.h>

#include "urpc_util.h"
#include "util_vlog.h"

namespace ubsocket {
typedef struct util_vlog_level_def {
    const char *output_name;
    const char **alias_names;
} util_vlog_level_def_t;

static const char *g_emerg_alias_names_def[] = { nullptr };
static const char *g_alert_alias_names_def[] = { nullptr };
static const char *g_crit_alias_names_def[] = { nullptr };
static const char *g_err_alias_names_def[] = { "error", nullptr };
static const char *g_warn_alias_names_def[] = { "warn", nullptr };
static const char *g_notice_alias_names_def[] = { "notice", nullptr };
static const char *g_info_alias_names_def[] = { "info", nullptr };
static const char *g_debug_alias_names_def[] = { "debug", "7", nullptr };

static const util_vlog_level_def_t g_util_vlog_level_def[] = {
    { "EMERG", g_emerg_alias_names_def },
    { "ALERT", g_alert_alias_names_def },
    { "CRIT", g_crit_alias_names_def },
    { "ERR", g_err_alias_names_def },
    { "WARN", g_warn_alias_names_def },
    { "NOTICE", g_notice_alias_names_def },
    { "INFO", g_info_alias_names_def },
    { "DEBUG", g_debug_alias_names_def },
};

static bool next_print_cycle(uint64_t *last_time, uint32_t time_interval)
{
    uint64_t now_time = urpc_get_cpu_cycles();
    if (((now_time - *last_time) * MS_PER_SEC / urpc_get_cpu_hz()) >= time_interval) {
        *last_time = now_time;
        return true;
    }
    return false;
}

bool util_vlog_limit(util_vlog_ctx_t *ctx, uint32_t *print_count, uint64_t *last_time)
{
    if (ctx->rate_limited.interval_ms == 0) {
        return true;
    }

    if (next_print_cycle(last_time, ctx->rate_limited.interval_ms)) {
        *print_count = 0;
    }

    if (*print_count < ctx->rate_limited.num) {
        *print_count += 1;
        return true;
    }

    return false;
}

void util_vlog_output(
    error_type_t error_type, util_vlog_ctx_t *ctx, util_vlog_level_t level,
    const char *function, int line, const char *format, ...)
{
    char log_msg[UTIL_VLOG_SIZE];
    int len = snprintf(log_msg, UTIL_VLOG_SIZE, "%s|%s|%s[%d]|", ctx->vlog_name, error_type_to_str(error_type),
                       function, line);
    if (len < 0 || len >= UTIL_VLOG_SIZE) {
        return;
    }

    va_list va;
    va_start(va, format);
    len = vsnprintf(&log_msg[len], (size_t)(UTIL_VLOG_SIZE - len), format, va);
    va_end(va);
    if (len < 0) {
        return;
    }

    ctx->vlog_output_func(level, log_msg);
}

util_vlog_level_t util_vlog_level_converter_from_str(const char *str, util_vlog_level_t default_level)
{
    int array_size = (int)sizeof(g_util_vlog_level_def) / (int)sizeof(g_util_vlog_level_def[0]);
    for (int i = 0; i < array_size; ++i) {
        for (const char **name_def = g_util_vlog_level_def[i].alias_names; *name_def != NULL; ++name_def) {
            if (strcasecmp(str, *name_def) == 0) {
                return (util_vlog_level_t)i;
            }
        }
    }

    return default_level;
}

const char *util_vlog_level_converter_to_str(util_vlog_level_t level)
{
    return g_util_vlog_level_def[level].output_name;
}
static const char *g_error_type_to_str[ubsocket::ERROR_TYPE_MAX] = {
    "UMQ_AE",
    "UMQ_API",
    "UMQ_CQE",
    "UBSocket",
};

const char *error_type_to_str(ubsocket::error_type_t error_type)
{
    if (error_type >= ubsocket::ERROR_TYPE_MAX) {
        return "UNKNOWN";
    }
    return g_error_type_to_str[error_type];
}
}  // namespace ubsocket
