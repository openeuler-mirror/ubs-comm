/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_DATA_TX_H
#define UBS_COMM_UBSOCKET_DATA_TX_H

#include <sys/time.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstring>
#include <memory>

#include "common/ubsocket_global_setting.h"
#include "core/umq/umq_qbuf_list.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/statistics/tx_stat_defs.h"
#include "ubsocket_buf_converter.h"
#include "ubsocket_core_types.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
class UmqSocket;
} // namespace umq
/* TX ops：Alloc / Post / PollTx 等动作的唯一实现（原 UmqTxOps）。
 * 去虚化依据：TCP 直通在 DataTx 壳层即短路（RAW_ESTABLISHED 走 libc），
 * GenerateSocketCommOps 对非 UMQ 类型直接拒绝——本类是唯一实现，
 * 虚表与动态分发是纯开销。方法体在 umq_data_tx_ops.cpp。 */
class DataTxOps {
public:
    /* owner_ 显式后向指针（由 DataPlaneEntry 构造注入）：生产路径 fd/umq 句柄
     * 恒读 socket 本体（重协商后天然最新）；无宿主的独立实例（UT 栈对象）
     * 回退到构造参数。不用 container_of——栈上/独立构造的 ops 反查是野指针。 */
    explicit DataTxOps(int fd, uint64_t umq_handle = UMQ_INVALID_HANDLE, umq::UmqSocket *owner = nullptr)
        : fd_(fd), fallback_umqh_(umq_handle), owner_(owner)
    {
        QBUF_LIST_INIT(&head_buf_);
        QBUF_LIST_INIT(&tail_buf_);
        if (GlobalSetting::UBS_MONITOR_ENABLE) {
            tx_stat_counters_.reset(new txstat::TxStatCounters());
            std::memset(tx_stat_counters_.get(), 0, sizeof(txstat::TxStatCounters));
        }
    }

    ~DataTxOps() = default;

    txstat::TxStatCounters *GetTxStatCounters() const
    {
        return tx_stat_counters_.get();
    }

    ConverterPtr BuildIovConverter(const struct iovec *iov, int iovcnt);

    ConverterPtr BuildBufferConverter(const void *buf, size_t size);

    // 分配发送缓冲区
    uintptr_t AllocTxBuf(uint32_t size, uint32_t count);

    // 投递发送请求
    int PostSend(const SocketPtr &sock, uintptr_t buf_list, uint32_t batch, const ConverterPtr &cvt);

    int PollTx(Socket *sock);

    /* Drain all pending TX CQEs to completion (poll-to-empty) so the
     * background TxCqePoller reclaims SQ slots that PollTx's state-gated
     * branches would leave unprocessed, preventing status:12 (SQ full). */
    void ForceDrainTx(Socket *sock);

    /* Single-shot TX poll: reclaim at most one batch of CQEs (up to
     * POLL_BATCH_MAX) without drain-to-empty. Used by TxSweepOnce in the
     * RX active loop to bound per-socket TX poll latency. */
    void QuickPollTx(Socket *sock);

    uint32_t IOBufSize();

    // Flush
    void FlushTx(Socket *sock, uint32_t timeout_ms = FLUSH_TIMEOUT_MS);

    void WakeUpTx(Socket *sock);

    bool Writable(const SocketPtr &sock);

    /* Poll TX CQEs once and return the number of CQEs reclaimed. Used by
     * DoReadOffer's inline READ-completion wait loop. */
    int PollUmqTxOnce(Socket *sock);

public:
    std::atomic<uint16_t> tx_queue_avail_num_{GlobalSetting::UBS_TX_DEPTH}; // current window size for TX
    uint16_t ack_event_num_ = 0;
    bool get_and_ack_event_ = false;
    std::atomic<int> epoll_event_num_{0};
    int expect_epoll_event_num_ = 0;
    std::atomic<bool> need_fc_awake_{false};

private:
    // 处理 umq_post 失败时的坏 buffer
    uint32_t HandleBadQBuf(const SocketPtr &sock, umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf, umq_buf_t *last_head_qbuf,
                           uint32_t batch, uint16_t unsolicited_wr_num, uint32_t unsolicited_bytes,
                           uint16_t unsignaled_wr_num, uint32_t *buf_num);
    Block *DataToBlock(void *data);
    int PollUmqTx(Socket *sock, bool poll_to_empty);
    int DoUmqTxPoll(Socket *sock, ops_error_code &err_code);
    int GetAndAckEvent();
    int DpRearmTxInterrupt();

private:
    umq::UmqSocket *Owner() const
    {
        return owner_;
    }
    int OwnerFd() const;
    uint64_t OwnerUmqh() const;

    int fd_ = -1;                 /* 无宿主实例的回退 fd */
    uint64_t fallback_umqh_ = 0;  /* 无宿主实例的回退句柄 */
    umq::UmqSocket *owner_ = nullptr;

    /* m_tx.m_head_buf -> |umq_buf 0| -> |umq_buf 1| -> ... -> |umq_buf n| <- m_tx.m_tailbuf */
    umq_buf_list_t head_buf_ = {0};
    umq_buf_list_t tail_buf_ = {0};

    uint32_t unsolicited_bytes_ = 0;  // length of accumulated work request without setting solicited
    uint16_t unsolicited_wr_num_ = 0; // number of accumulated work request without setting solicited
    uint16_t unsignaled_wr_num_ = 0;  // number of accumulated work request without setting signaled

    std::unique_ptr<txstat::TxStatCounters> tx_stat_counters_;

    friend class DataTx;
};

// 通用层：流控、数据切分、故障回退
class DataTx {
public:
    DataTx() = default;
    DataTx(const SocketPtr &sock, DataTxOps *ops);

    ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);

    DataTxOps *GetTxOps()
    {
        return tx_ops_;
    }

private:
    /* fd 不再另存一份：三个入口方法都携带 sock，直接读 sock->raw_socket_ */
    /* 非拥有指针：ops 按值内嵌于 UmqSocket，生命周期随 socket 本体 */
    DataTxOps *tx_ops_ = nullptr;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_DATA_TX_H
