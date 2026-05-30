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
#ifndef UBS_COMM_URMA_BACKEND_H
#define UBS_COMM_URMA_BACKEND_H

#include "common/ubsocket_common_includes.h"
#include "urma_setting.h"

namespace ock {
namespace ubs {
namespace urma {
class Urma {
public:
    static Result Init() noexcept;
    static void UnInit() noexcept;

private:
    static std::mutex URMA_MUTEX;
    static bool URMA_INITED;
};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_BACKEND_H
