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

/*
 * RX-STAT: per-socket 热路径自增宏。
 *
 * 设计要点同 tx_stat_block.h:
 *  - 统计未开启时 rx_stat_counters_ 为 nullptr, 宏内 1 次 nullptr 判断后短路返回, 零开销。
 *  - 统计开启后: 1 次 nullptr 判断 + 1 次 volatile uint32_t ++。
 *  - 宏在 UmqRxOps 方法内部调用, 直接使用 this->rx_stat_counters_。
 */
#ifndef UBS_COMM_RX_STAT_BLOCK_H
#define UBS_COMM_RX_STAT_BLOCK_H

#include <cstdint>
#include "common/ubsocket_defines.h"
#include "profiling/statistics/rx_stat_defs.h"

namespace ock {
namespace ubs {
namespace rxstat {

#define RX_POLL_ERR_ADD(k)                                                          \
    do {                                                                            \
        if (rx_stat_counters_ != nullptr) {                                         \
            ++rx_stat_counters_->poll_err[(k)];                                     \
        }                                                                           \
    } while (0)

#define RX_CQE_ERR_ADD(k)                                                           \
    do {                                                                            \
        if (rx_stat_counters_ != nullptr) {                                         \
            ++rx_stat_counters_->cqe_err[(k)];                                      \
        }                                                                           \
    } while (0)

#define RX_DATASET_ERR_ADD(rx_ops, k)                                               \
    do {                                                                            \
        auto *__c = (rx_ops)->GetRxStatCounters();                                  \
        if (__c != nullptr) {                                                       \
            ++__c->dataset_err[(k)];                                                \
        }                                                                           \
    } while (0)

}  // namespace rxstat
}  // namespace ubs
}  // namespace ock

#endif  // UBS_COMM_RX_STAT_BLOCK_H
