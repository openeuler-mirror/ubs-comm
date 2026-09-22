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
 * @brief Initialize ubsocket library.
 *
 * Must be called at most once per process. A second call before
 * ubsocket_uninit() is a no-op (returns UBS_OK). After ubsocket_uninit() has
 * been called, re-initialization is NOT supported: ubsocket_init() fails with
 * errno EPERM.
 *
 * @param options          [in] init options
 * @return UBS_OK on success; negative on failure with errno set:
 *           EINVAL - null or invalid options;
 *           EBADF  - underlying API load / lock registration failed;
 *           EPERM  - ubsocket_uninit() was already called.
 */
int ubsocket_init(u_init_options_t *options);

/**
 * @brief Un-initialize ubsocket library.
 *
 * Must be called at most once per process, typically before exit. This is
 * irreversible: after it returns, the library cannot be re-initialized.
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

/**
 * @brief Check whether a fd is using UB native transport.
 *
 * After connect/accept, ubscomm may degrade a connection to TCP
 * (fd removed from ArraySet). This function provides a direct query
 * for the upper layer (e.g. brpc) to detect degradation.
 *
 * @param fd          [in] file descriptor to check
 * @return 1 if fd is UB transport (in ArraySet)
 *         0 if fd is TCP (not in ArraySet, degraded or plain TCP)
 *         -1 on error (UBS_NATIVE_TCP_MODE or not initialized)
 */
int ubsocket_is_ub_transport(int fd);

/**
 * @brief Enable or disable UB-to-TCP degradation at runtime.
 *
 * Sets GlobalSetting::UBS_ENABLE_DEGRADE directly, avoiding the need
 * to call setenv("UBSOCKET_DEGRADE_ENABLE") before ubsocket_init().
 * When degradation is disabled, ubscomm will not degrade connections
 * to TCP during connect/accept negotiation — failures are hard errors.
 *
 * @param enable      [in] 1 to enable degradation, 0 to disable
 * @return 0 on success, -1 if ubsocket not initialized
 */
int ubsocket_set_degrade_enable(int enable);

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_H
