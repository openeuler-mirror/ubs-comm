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
#ifndef UBS_COMM_URMA_SOCKET_SEG_EX_H
#define UBS_COMM_URMA_SOCKET_SEG_EX_H

#include "core/ubsocket_conn_types.h"
#include "urma_io_buf_pool.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

/*
 * 段信息交换 wire 帧（非 LengthPrefixed，两端按 sizeof 定长收发的裸 struct）。
 * 字段类型与顺序必须与同步路径 UrmaSocket::DoExchangeSocketResources 内部定义完全一致，
 * 保证全异步 server 与既有 client/半异步 server wire 互通。
 */
struct UrmaSockSegExInfo {
    UrmaSockExInfo ex{};
    IdentifyNode node;
    uint32_t uasid;
    uintptr_t pool_start_addr;
    urma_seg_t seg{};
};

static_assert(sizeof(UrmaSockSegExInfo) <= CONN_CTRL_BUF_SIZE,
              "UrmaSockSegExInfo must fit in the per-connection control buffer");

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_SOCKET_SEG_EX_H
