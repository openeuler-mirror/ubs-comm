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
#include <iostream>

#include "ubsocket.h"
#include "ubsocket_common_includes.h"
#include "ubsocket_version.h"

using namespace ock::ubs;

UBS_API int ubsocket_init(u_init_options_t *options)
{
    return 0;
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