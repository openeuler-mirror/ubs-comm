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

#include "core/ubsocket_buf_converter.h"
#include "core/ubsocket_data_tx.h"
#include "iobuf/ubsocket_iobuf.h"
#include "umq_backend.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqSocket;

class UmqTxOps : public DataTxOps {
public:
    explicit UmqTxOps(int fd, uint64_t umq_handle = UMQ_INVALID_HANDLE) : local_umqh_(umq_handle)
    {
        fd_ = fd;
        // 初始化链表头尾为空
        QBUF_LIST_INIT(&head_buf_);
        QBUF_LIST_INIT(&tail_buf_);

        // 初始化流控与统计计数器为 0
        unsolicited_bytes_ = 0;
        unsolicited_wr_num_ = 0;
        unsignaled_wr_num_ = 0;
    }

    ~UmqTxOps() override = default;

    ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept override;
    Result Flush(const SocketPtr &sock, uint32_t flags) noexcept override;
    Result TriggerPollOut(const SocketPtr &sock, uint32_t flags) noexcept override;
    ConverterPtr BuildIovConverter(const struct iovec *iov, int iovcnt);
    ConverterPtr BuildBufferConverter(const void *buf, size_t size);

    // 分配发送缓冲区
    uintptr_t AllocTxBuf(uint32_t size, uint32_t count);

    // 发送请求
    int PostSend(const SocketPtr &sock, uintptr_t buf, uint32_t batch, const ConverterPtr &cvt);

    int PollTx(Socket *sock) noexcept override;

    uint32_t IOBufSize();

    // Flush
    void FlushTx(Socket *sock, uint32_t timeout_ms = FLUSH_TIMEOUT_MS);

    void WakeUpTx(Socket *sock);

    bool Writable(const SocketPtr &sock);

private:
    // 处理 umq_post 失败时的坏 buffer
    uint32_t HandleBadQBuf(const SocketPtr &sock, umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf, umq_buf_t *last_head_qbuf,
                           uint32_t batch, uint16_t unsolicited_wr_num, uint32_t unsolicited_bytes,
                           uint16_t unsignaled_wr_num, uint32_t *buf_num);
    Block *DataToBlock(void *data);
    int PollUmqTx(Socket *sock, bool poll_to_empty);
    int PollUmqTxOnce(Socket *sock);
    int DoUmqTxPoll(Socket *sock, ops_error_code &err_code);
    int GetAndAckEvent();
    int DpRearmTxInterrupt();
    void ProcessTracePacket(const SocketPtr &sock, umq_buf_t *cur_buf, int seq_no, int i, uint64_t tx_total_len);

private:
    // --- 私有成员变量 ---
    // umq 相关的句柄
    uint64_t local_umqh_ = UMQ_INVALID_HANDLE;

    /* m_tx.m_head_buf -> |umq_buf 0| -> |umq_buf 1| -> ... -> |umq_buf n| <- m_tx.m_tailbuf */
    umq_buf_list_t head_buf_ = {0};
    umq_buf_list_t tail_buf_ = {0};

    uint32_t unsolicited_bytes_ = 0;                 // length of accumulated work request without setting solicited
    uint16_t unsolicited_wr_num_ = 0;                // number of accumulated work request without setting solicited
    uint16_t unsignaled_wr_num_ = 0;                 // number of accumulated work request without setting signaled
    std::atomic<uint32_t> successful_post_count_{0}; // number of successfully posted work request

public:
    int fd_ = -1;
    std::atomic<uint32_t> tx_queue_avail_num_{GlobalSetting::UBS_TX_DEPTH}; // current window size for TX
    uint16_t ack_event_num_ = 0;
    bool get_and_ack_event_ = false;
    std::atomic<int> epoll_event_num_{0};
    int expect_epoll_event_num_ = 0;
    std::atomic<bool> need_fc_awake_{false};

    friend class UmqSocket;
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_DATA_TX_H
