/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UMQ_DATA_TX_H
#define UBS_COMM_UMQ_DATA_TX_H

#include <cstdint>
#include <memory>

#include "core/ubsocket_data_tx.h"
#include "profiling/statistics/tx_stat_defs.h"
#include "umq_backend.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {

/* v1.7 去虚化合并：唯一实现已下沉为 DataTxOps 本体（见 core/ubsocket_data_tx.h），
 * 本别名保留既有引用与 UmqTxOps(fd, umq_handle) 构造形式。 */
using UmqTxOps = ::ock::ubs::DataTxOps;

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_DATA_TX_H
