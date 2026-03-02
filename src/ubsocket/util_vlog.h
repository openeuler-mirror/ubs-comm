/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: Provide vlog module
 * Create: 2025-07-29
 */

#ifndef UTIL_VLOG_H
#define UTIL_VLOG_H

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define UTIL_VLOG_SIZE              (1024)
#define UTIL_VLOG_NAME_STR_LEN      (64)
#define UTIL_VLOG_PRINT_PERIOD_MS   (1000)
#define UTIL_VLOG_PRINT_TIMES       (10)

#define UTIL_VLOG( __error_type, __ctx, __level, ...)  \
    if (!::ubsocket::util_vlog_drop(__ctx, __level)) {  \
        ::ubsocket::util_vlog_output( __error_type, __ctx, __level, __func__, __LINE__, ##__VA_ARGS__);    \
    }

#define UTIL_LIMIT_VLOG(__ctx, __level, ...)  \
    if (!::ubsocket::util_vlog_drop(__ctx, __level)) {  \
        static uint32_t count_call = 0; \
        static uint64_t last_time = 0;  \
        if (::ubsocket::util_vlog_limit(__ctx, &count_call, &last_time)) {  \
            ::ubsocket::util_vlog_output( __error_type, __ctx, __level, __func__, __LINE__, ##__VA_ARGS__);    \
        }   \
    }

namespace ubsocket {
typedef enum util_vlog_level {
    UTIL_VLOG_LEVEL_EMERG = 0,
    UTIL_VLOG_LEVEL_ALERT,
    UTIL_VLOG_LEVEL_CRIT,
    UTIL_VLOG_LEVEL_ERR,
    UTIL_VLOG_LEVEL_WARN,
    UTIL_VLOG_LEVEL_NOTICE,
    UTIL_VLOG_LEVEL_INFO,
    UTIL_VLOG_LEVEL_DEBUG,
    UTIL_VLOG_LEVEL_MAX,
} util_vlog_level_t;

typedef enum error_type {
    UMQ_AE,
    UMQ_API,
    UMQ_CQE,
    UBSocket,
    ERROR_TYPE_MAX,
} error_type_t;

const char *error_type_to_str(error_type_t error_type);

typedef struct util_vlog_ctx {
    util_vlog_level_t level;
    char vlog_name[UTIL_VLOG_NAME_STR_LEN];
    void (*vlog_output_func)(int level, char *log_msg);
    struct {
        uint32_t interval_ms;    // rate-limited log output interval. If the value is 0, rate is not limited.
        uint32_t num;            // maximum number of rate-limited logs that can be output in a specified interval.
    } rate_limited;
} util_vlog_ctx_t;

static inline void default_vlog_output(int level, char *log_msg)
{
    syslog(level, "%s", log_msg);
}

bool util_vlog_limit(util_vlog_ctx_t *ctx, uint32_t *print_count, uint64_t *last_time);

static inline bool util_vlog_drop(util_vlog_ctx_t *ctx, util_vlog_level_t level)
{
    return level > ctx->level;
}

void util_vlog_output(error_type_t error_type, util_vlog_ctx_t *ctx, util_vlog_level_t level, const char *function,
                      int line, const char *format, ...);

util_vlog_level_t util_vlog_level_converter_from_str(const char *str, util_vlog_level_t default_level);
const char *util_vlog_level_converter_to_str(util_vlog_level_t level);

}  // namespace ubsocket

#endif
