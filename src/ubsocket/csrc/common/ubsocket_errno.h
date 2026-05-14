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
    UBS_DL_LOAD_SYM_FAILED = -100,
    UBS_DL_OPEN_LIB_FAILED = -101,
    UBS_INVALID_PARAM = -102,
    UBS_MALLOC_FAILED = -103,

    // ubsocket 相关错误
    UBS_SET_DEV_INFO = 4097,
    UBS_PREFILL_RX,
    UBS_INIT_SHARED_JFR_RX_QUEUE,
    UBS_NEW_SOCKET_FD,
    UBS_TCP_EXCHANGE,
    UBS_UB_ACCEPT,
    UBS_NO_MAIN_UMQ,
    UBS_MAX,
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_ERRNO_H
