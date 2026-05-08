/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
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
void ubsocket_uninit(int flags);

/**
 * @brief Set external log function
 *
 * @param func
 * @return
 */
int ubsocket_set_logger(void (*func)(int level, const char *msg));

/**
 * @brief Set log level
 *
 * @param level
 * @return
 */
int ubsocket_set_log_level(int level);

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_H
