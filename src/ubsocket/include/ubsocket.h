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
#ifndef UBS_COMM_UBSOCKET_H
#define UBS_COMM_UBSOCKET_H

#include "ubsocket_ctnl.h"
#include "ubsocket_def.h"
#include "ubsocket_epoll.h"
#include "ubsocket_sock.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Set the init options to default values
 *
 * @param options          [in] the options to be set
 */
int ubsocket_init_options(u_init_options_t *options);

/**
 * @brief Initialize ubsocket library
 *
 * @param options          [in] options for
 * @return
 */
int ubsocket_init(u_init_options_t *options);

/**
 * @brief Un-initialize ubsocket library
 *
 * @param flags
 */
void ubsocket_uninit();

/**
 * @brief Get version of ubsocet library
 * @return version string, formated x.x.x
 */
const char *ubsocket_version();

/**
 * @brief Set external log function
 *
 * @param func
 * @return
 */
int ubsocket_set_logger(void (*func)(int level, const char *msg, const char *filename, int line));

/**
 * @brief Set log level
 *
 * @param level
 * @return
 */
int ubsocket_set_log_level(int level);

typedef enum ubs_iobuf_pool_type
{
    UBS_IOBUF_POOL_TINY = 1,
    UBS_IOBUF_POOL_NORMAL = 2,
    UBS_IOBUF_POOL_ESCAPE = 3,
} ubs_iobuf_pool_type_t;

typedef struct ubs_iobuf_alloc_option {
    uint32_t flag;
    ubs_iobuf_pool_type_t pool_type;
} ubs_iobuf_alloc_option_t;

#define UBS_IOBUF_ALLOC_FLAG_POOL_TYPE 1U

void *ubsocket_iobuf_allocate(size_t size, const ubs_iobuf_alloc_option_t *option);

void ubsocket_iobuf_deallocate(void *addr);

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_H
