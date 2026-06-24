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

#include "core/ubsocket_data_tx.h"
#include "umq_backend.h"
#include "umq_setting.h"

namespace ock {
namespace ubs {
namespace umq {

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

    ConverterPtr BuildIovConverter(const struct iovec *iov, int iovcnt) override;

    ConverterPtr BuildBufferConverter(const void *buf, size_t size) override;

    // 分配发送缓冲区
    uintptr_t AllocTxBuf(uint32_t size, uint32_t count) override;

    // 发送请求
    int PostSend(const SocketPtr &sock, uintptr_t buf, uint32_t batch, const ConverterPtr &cvt) override;

    int PollTx(const SocketPtr &sock) override;

    uint32_t IOBufSize() override;

    // Flush
    void FlushTx(const SocketPtr &sock, uint32_t timeout_ms = FLUSH_TIMEOUT_MS) override;

    void WakeUpTx(Socket *sock);

    bool Writable(const SocketPtr &sock) override;

private:
    // 处理 umq_post 失败时的坏 buffer
    uint32_t HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf, umq_buf_t *last_head_qbuf,
                           uint16_t unsolicited_wr_num, uint32_t unsolicited_bytes, uint16_t unsignaled_wr_num,
                           uint32_t *buf_num);
    void *PtrFloorToBoundary(void *ptr);
    int PollUmqTx(const SocketPtr &sock, bool poll_to_empty);
    int PollUmqTxOnce(const SocketPtr &sock);
    int DoUmqTxPoll(const SocketPtr &sock, ops_error_code &err_code);
    int GetAndAckEvent();
    int DpRearmTxInterrupt();

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
    std::atomic<uint16_t> successful_post_count_{0}; // number of successfully posted work request
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_DATA_TX_H
