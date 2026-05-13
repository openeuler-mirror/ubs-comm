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
#include <iostream>

#include "dl_libc_api.h"
#include "ubsocket.h"
#include "ubsocket_common_includes.h"
#include "ubsocket_global_setting.h"
#include "ubsocket_version.h"

using namespace ock::ubs;

UBS_API int ubsocket_init_options(u_init_options_t *options)
{
    if (options == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    options->allowed_protocol = UBS_PROTOCOL_TCP; /* use raw tcp by default */
    options->async_acceptor_thread_count = 0;     /* tune off by default */
    options->async_connector_thread_count = 0;    /* tune off by default */
    options->async_epoll_thread_count = 1;        /* tune on by default */
    options->lock_ops = nullptr;
    options->rw_lock_ops = nullptr;
    options->sem_ops = nullptr;
    return UBS_OK;
}

UBS_API int ubsocket_init(u_init_options_t *options)
{
    if (options == nullptr) {
        errno = EINVAL;
        return UBS_ERROR;
    }

    /* do this under mutex */
    std::lock_guard<std::mutex> guard(GlobalSetting::MUTEX);
    if (GlobalSetting::UBS_INITED) {
        return UBS_OK;
    }

    /* do initialization */
    /* step1: global setting */

    /* step2: load under api */
    auto result = LibcApi::Load();
    if (result != UBS_OK) {
        errno = EBADF;
        return UBS_ERROR;
    }

    /* set initialized */
    GlobalSetting::UBS_INITED = true;

    return UBS_OK;
}

UBS_API void ubsocket_uninit(int flags)
{
    return;
}

UBS_API const char *ubsocket_version()
{
    /* log full version */
    std::cout << "full version: " << UBS_LIB_VERSION_FULL << std::endl;
    /* return short version */
    return UBS_LIB_VERSION;
}

UBS_API int ubsocket_set_logger(void (*func)(int level, const char *msg))
{
    return 0;
}

UBS_API int ubsocket_set_log_level(int level)
{
    return 0;
}