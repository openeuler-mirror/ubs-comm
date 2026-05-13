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
#ifndef UBS_COMM_UBSOCKET_ERRNO_H
#define UBS_COMM_UBSOCKET_ERRNO_H

#include "ubsocket_defines.h"

namespace ock {
namespace ubs {
enum InnerCode : int32_t {
    UBS_OK = 0,
    UBS_ERROR = -1,
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_ERRNO_H
