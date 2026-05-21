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
#include "urma_backend.h"

namespace ock {
namespace ubs {
namespace urma {
std::mutex Urma::URMA_MUTEX;
bool Urma::URMA_INITED = false;

Result Urma::Init() noexcept
{
    UBS_VLOG_DEBUG("enter");

    std::lock_guard<std::mutex> guard(URMA_MUTEX);
    if (URMA_INITED) {
        UBS_VLOG_DEBUG("umq already initialized");
        return UBS_OK;
    }

    /* init setting, includes env loading etc */
    UrmaSetting::Init();

    /* init urma */
    urma_init_attr_t init_attr{};
    auto result = UrmaApi::urma_init(&init_attr);
    if (result != 0) {
        UBS_VLOG_ERR("Init urma failed, result: %d", result);
        return result;
    }

    /* set inited */
    URMA_INITED = true;

    UBS_VLOG_DEBUG("leave, inited = %d", URMA_INITED);
    return UBS_OK;
}

void Urma::UnInit() noexcept
{
    UBS_VLOG_DEBUG("enter");

    std::lock_guard<std::mutex> guard(URMA_MUTEX);
    if (!URMA_INITED) {
        UBS_VLOG_DEBUG("umq not initialized");
        return;
    }

    UrmaApi::urma_uninit();

    URMA_INITED = false;

    UBS_VLOG_DEBUG("leave, inited = %d", URMA_INITED);
}
} // namespace urma
} // namespace ubs
} // namespace ock