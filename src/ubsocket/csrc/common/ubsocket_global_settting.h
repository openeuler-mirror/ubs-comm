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
#ifndef UBS_COMM_UBSOCKET_GLOBAL_SETTING_H
#define UBS_COMM_UBSOCKET_GLOBAL_SETTING_H

#include "ubsocket_def.h"
#include "ubsocket_errno.h"

namespace ock {
namespace ubs {
class GlobalSetting {
public:
    /**
     * @brief Initialize all global setting, either from env or other ...
     */
    static Result Initialize();

    /* disable constructor */
    GlobalSetting() = delete;

public:
    static int UBS_ALLOWED_PROTOCOL;         /* allowed protocol */
    static bool UBS_TRACE_ENABLED;           /* if enable tracing, from env */
    static bool UBS_INITED;                  /* if ubsocket initialized, from API */
    static bool UBS_ACCEPTOR_ASYNC_ENABLED;  /* if enable async acceptor, from env */
    static bool UBS_CONNECTOR_ASYNC_ENABLED; /* if enable async connector, from env */
    static bool UBS_EPOLL_ASYNC_ENABLED;     /* if enable async epoll_wait, from env */
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_GLOBAL_SETTING_H
