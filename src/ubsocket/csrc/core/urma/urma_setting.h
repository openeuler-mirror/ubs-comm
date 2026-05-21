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
#ifndef UBS_COMM_URMA_SETTING_H
#define UBS_COMM_URMA_SETTING_H

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "under_api/urma/dl_urma_api.h"
#include "under_api/urma/urma_types.h"

namespace ock {
namespace ubs {
namespace urma {
class UrmaSetting {
public:
    static void Init() noexcept;

public:
    static std::string UB_DEV_NAME;

private:
    static void AddRules();
    static Result LoadEnv();
};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_SETTING_H
