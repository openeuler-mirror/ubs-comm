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

#include "ubsocket_logger.h"

#define ALWAYS_INLINE inline __attribute__((always_inline))

ubsocket::util_vlog_ctx_t *RpcAdptGetLogCtx(void);

int RpcAdptSetLogCtx(ubsocket::util_vlog_level_t level);

static ALWAYS_INLINE void RpcAdptVlogCtxSet(ubsocket::util_vlog_level_t level, char *vlog_name)
{
    // use temp context to avoid modifications to default configurations caused by exceptions during context creation.
    RpcAdptGetLogCtx()->level = level;
}

#endif