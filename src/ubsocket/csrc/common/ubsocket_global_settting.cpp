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
#include "ubsocket_global_settting.h"

namespace ock {
namespace ubs {
int GlobalSetting::UBS_ALLOWED_PROTOCOL = 0;
bool GlobalSetting::UBS_TRACE_ENABLED = false;
bool GlobalSetting::UBS_INITED = false;
bool GlobalSetting::UBS_ACCEPTOR_ASYNC_ENABLED = false;
bool GlobalSetting::UBS_CONNECTOR_ASYNC_ENABLED = false;
bool GlobalSetting::UBS_EPOLL_ASYNC_ENABLED = false;

Result GlobalSetting::Initialize()
{
    /* load from env */
    return UBS_OK;
}
} // namespace ubs
} // namespace ock