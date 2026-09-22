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
 * RX-STAT: per-socket 接收方向流控定界统计 — 枚举、桶名、计数器结构。
 *
 * 接收方向包含三类埋点：
 *   A) Poll 阶段失败 (umq_poll / refill)
 *   B) CQE 处理阶段 (RX buf status 异常)
 *   C) 数据提取阶段 (RxDataSet 失败)
 * 作用域：per-socket, 惰性分配（UBS_MONITOR_ENABLE=on 时才 new）。
 */

#ifndef UBS_COMM_RX_STAT_DEFS_H
#define UBS_COMM_RX_STAT_DEFS_H

#include <cstdint>

namespace ock {
namespace ubs {
namespace rxstat {

enum RxPollErr : uint8_t {
    RX_POLL_GET_EVENT_FAIL = 0,
    RX_POLL_FAIL,
    RX_REFILL_ALLOC_FAIL,
    RX_REFILL_POST_FAIL,
    RX_QBUF_POP_FAIL,
    RX_POLL_ERR_MAX
};

constexpr const char *RX_STAT_POLL_NAME[RX_POLL_ERR_MAX] = {
    "get_event_fail", "poll_fail", "refill_alloc_fail", "refill_post_fail", "qbuf_pop_fail"};

enum RxCqeErr : uint8_t {
    RXCQE_FC = 0,
    RXCQE_REMOTE,
    RXCQE_LOCAL,
    RXCQE_ACK_TIMEOUT,
    RXCQE_RNR,
    RXCQE_OTHER,
    RXCQE_ERR_MAX
};

constexpr const char *RX_STAT_CQE_NAME[RXCQE_ERR_MAX] = {
    "fc", "remote", "local", "ack_timeout", "rnr", "other"};

enum RxDatasetErr : uint8_t {
    RX_DATASET_NO_BLOCK = 0,
    RX_FLOW_CTRL_FAILED,
    RX_REARM_FAIL,
    RX_PEER_CLOSED,
    RX_DATASET_ERR_MAX
};

constexpr const char *RX_STAT_DATASET_NAME[RX_DATASET_ERR_MAX] = {
    "dataset_no_block", "flow_ctrl_failed", "rearm_fail", "peer_closed"};

struct RxStatCounters {
    volatile uint32_t poll_err[RX_POLL_ERR_MAX];
    volatile uint32_t cqe_err[RXCQE_ERR_MAX];
    volatile uint32_t dataset_err[RX_DATASET_ERR_MAX];
};

}  // namespace rxstat
}  // namespace ubs
}  // namespace ock

#endif  // UBS_COMM_RX_STAT_DEFS_H
