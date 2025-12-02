/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-28
 *Note:
 *History: 2025-07-28
*/

#ifndef RPC_ADPT_VLOG_H
#define RPC_ADPT_VLOG_H

#include "util_vlog.h"

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define RPC_ADPT_VLOG_ERR(__format, ...)  \
  UTIL_VLOG(RpcAdptGetLogCtx(), UTIL_VLOG_LEVEL_ERR, __format, ##__VA_ARGS__)
#define RPC_ADPT_VLOG_WARN(__format, ...)  \
  UTIL_VLOG(RpcAdptGetLogCtx(), UTIL_VLOG_LEVEL_WARN, __format, ##__VA_ARGS__)  
#define RPC_ADPT_VLOG_NOTICE(__format, ...)  \
  UTIL_VLOG(RpcAdptGetLogCtx(), UTIL_VLOG_LEVEL_NOTICE, __format, ##__VA_ARGS__)
#define RPC_ADPT_VLOG_INFO(__format, ...)  \
  UTIL_VLOG(RpcAdptGetLogCtx(), UTIL_VLOG_LEVEL_INFO, __format, ##__VA_ARGS__)
#define RPC_ADPT_VLOG_DEBUG(__format, ...)  \
  UTIL_VLOG(RpcAdptGetLogCtx(), UTIL_VLOG_LEVEL_DEBUG, __format, ##__VA_ARGS__)

util_vlog_ctx_t *RpcAdptGetLogCtx(void);

int RpcAdptSetLogCtx();

static ALWAYS_INLINE void RpcAdptVlogCtxSet(util_vlog_level_t level, char *vlog_name)
{
    // use temp context to avoid modifications to default configurations caused by exceptions during context creation.
    RpcAdptGetLogCtx()->level = level;
}

#endif