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
 * TX-STAT: per-socket 热路径自增宏。
 *
 * 设计要点：
 *  - 统计未开启时 tx_stat_counters_ 为 nullptr, 宏内 1 次 nullptr 判断后短路返回, 零开销。
 *  - 统计开启后: 1 次 nullptr 判断 + 1 次 volatile uint32_t ++。
 *  - volatile 而非 atomic: 并发 ++ 多核下可能丢更新, 只看错误趋势, 可接受。
 *  - uint32_t 桶: 错误计数上限 42 亿, 足够。
 *  - 宏在 UmqTxOps 方法内部调用, 直接使用 this->tx_stat_counters_。
 */
#ifndef UBS_COMM_TX_STAT_BLOCK_H
#define UBS_COMM_TX_STAT_BLOCK_H

#include <cstdint>
#include "common/ubsocket_defines.h"
#include "profiling/statistics/tx_stat_defs.h"

namespace ock {
namespace ubs {
namespace txstat {

#define UMQ_POST_ERR_ADD(k)                                                         \
    do {                                                                            \
        if (tx_stat_counters_ != nullptr) {                                         \
            ++tx_stat_counters_->post_err[(k)];                                     \
        }                                                                           \
    } while (0)

#define UMQ_CQE_ERR_ADD(tx_ops, k)                                                  \
    do {                                                                            \
        if ((tx_ops) != nullptr) {                                                  \
            auto *__c = (tx_ops)->GetTxStatCounters();                              \
            if (__c != nullptr) {                                                   \
                ++__c->cqe_err[(k)];                                                \
            }                                                                       \
        }                                                                           \
    } while (0)

}  // namespace txstat
}  // namespace ubs
}  // namespace ock

#endif  // UBS_COMM_TX_STAT_BLOCK_H
