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
#include "golden_cmd.h"
#include "golden_cmd_data.h"
#include "golden_cmd_help.h"
#include "golden_cmd_pingpong.h"
#include "golden_cmd_pingpong_epoll.h"

namespace golden {
void SubCommandRegistry::RegisterAll() noexcept
{
    (void)RegisterSubCommand(SUB_CMD_MINUS_H, CreateHelp);
    (void)RegisterSubCommand(SUB_CMD_PINGPONG, CreatePingpong);
    (void)RegisterSubCommand(SUB_CMD_PINGPONG_EPOLL, CreatePingpongEpoll);
    (void)RegisterSubCommand(SUB_CMD_DATA, CreateData);
}
} // namespace golden