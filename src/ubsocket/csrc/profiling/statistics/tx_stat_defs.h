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
 * TX-STAT: per-socket 发送方向流控定界统计 — 枚举、桶名、计数器结构。
 *
 * 发送方向包含两类埋点：
 *   A) umq_post 失败（提交侧，UmqTxOps::PostSend）
 *      —— 本端提交即失败（信用/jetty/qbuf 池耗尽、授信回复超时、流控）。
 *   B) TX CQE 完成异常（完成侧，tx poll 线程处理 umq_poll 返回的完成事件）
 *      —— 提交成功但发送完成失败（对端 RNR / ACK 超时 / 远端错误 / 本端错误 / 流控失败）。
 * 维度：失败原因桶 PostErr（9 桶）+ 完成异常桶 CqeErr（6 桶）。
 * 范围：只覆盖发送方向；接收方向（RX 数据面 umq_poll）不在此范畴。
 * 作用域：per-socket, 惰性分配（UBS_MONITOR_ENABLE=on 时才 new）。
 */

#ifndef UBS_COMM_TX_STAT_DEFS_H
#define UBS_COMM_TX_STAT_DEFS_H

#include <cstdint>

namespace ock {
namespace ubs {
namespace txstat {

/* 埋点 A — umq_post 失败（9 桶） */
enum PostErr : uint8_t {
    POST_ERR_EAGAIN_ALL = 0,  // 流控 credit 不足，全部失败   ★核心
    POST_ERR_EAGAIN_PART,     // 流控 credit 不足，部分失败   ★核心
    POST_ERR_ENOBUFS_ALL,     // qbuf 池耗尽，全失败
    POST_ERR_ENOBUFS_PART,    // qbuf 池耗尽，部分失败
    POST_ERR_EMLINK,          // 无可用 jetty，已入 TpWaitQueue
    POST_ERR_ETIMEDOUT,       // 等对端授信回复超时（默认 1s）★核心
    POST_ERR_EFLOWCTL,        // ret == -UMQ_ERR_EFLOWCTL
    POST_ERR_NO_BADQBUF,      // bad_qbuf == nullptr 的异常分支
    POST_ERR_OTHER,           // 其他（flagEIO 分支）
    POST_ERR_MAX
};

constexpr const char *TX_STAT_POST_NAME[POST_ERR_MAX] = {
    "eagain_all", "eagain_part", "enobufs_all", "enobufs_part",
    "emlink", "timeout", "eflowctl", "no_badqbuf", "other"};

/* 埋点 B — TX CQE 完成异常（6 桶）。status 已在调用方保证 != 0（成功态 UMQ_BUF_SUCCESS 提前返回）。 */
enum CqeErr : uint8_t {
    CQE_ERR_RNR = 0,         // 对端 RQ 不足 (UMQ_BUF_RNR_RETRY_CNT_EXC_ERR)        ★核心
    CQE_ERR_ACK_TIMEOUT,     // 对端未回 ACK (UMQ_BUF_ACK_TIMEOUT_ERR)              ★核心
    CQE_ERR_FC,              // 流控失败 (UMQ_FAKE_BUF_FC_ERR)
    CQE_ERR_REMOTE,          // 对端错误 (REM_* 系列：resp_len / unsupported_req / op / access_abort)
    CQE_ERR_LOCAL,           // 本端错误 (LOC_* 系列：len / op / access)
    CQE_ERR_OTHER,           // 其他 (unsupported opcode / flush / suspend / poison / 未知)
    CQE_ERR_MAX
};

constexpr const char *TX_STAT_CQE_NAME[CQE_ERR_MAX] = {
    "rnr", "ack_timeout", "fc", "remote", "local", "other"};

struct TxStatCounters {
    volatile uint32_t post_err[POST_ERR_MAX];
    volatile uint32_t cqe_err[CQE_ERR_MAX];
};

}  // namespace txstat
}  // namespace ubs
}  // namespace ock

#endif  // UBS_COMM_TX_STAT_DEFS_H
