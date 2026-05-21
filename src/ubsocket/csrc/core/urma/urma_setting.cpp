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
#include "urma_setting.h"

namespace ock {
namespace ubs {
namespace urma {
#define ENV_URMA_DEV_NAME "UBSOCKET_UB_DEV"

std::string UrmaSetting::UB_DEV_NAME;

void UrmaSetting::Init() noexcept {}

void UrmaSetting::AddRules()
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {};

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {};

    /* str not empty rules: name, required */
    StrNotEmptyRule rules_str_not_empty[] = {{ENV_URMA_DEV_NAME, true}};

    for (auto &item : rules_int64) {
        Validator::Instance().AddNumRule(item);
    }

    for (auto &item : rules_str_enum) {
        Validator::Instance().AddStrEnumRule(item);
    }

    for (auto &item : rules_str_not_empty) {
        Validator::Instance().AddStrNotEmtpyRule(item);
    }

    UBS_SLOG_DEBUG(Validator::Instance().DumpString());
}

Result UrmaSetting::LoadEnv()
{
    return UBS_OK;
}
} // namespace urma
} // namespace ubs
} // namespace ock