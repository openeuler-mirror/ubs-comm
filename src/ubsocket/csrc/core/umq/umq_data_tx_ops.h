/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef UBS_COMM_UMQ_DATA_TX_H
#define UBS_COMM_UMQ_DATA_TX_H

#include <cstdlib>
#include <cstdint>
#include "umq_setting.h"
#include "../ubsocket_data_tx.h"
#include "../../../../hcom/umq/include/umq/umq_api.h"
#include "../../../../hcom/umq/include/umq/umq_types.h"
#include "../../../../hcom/umq/include/umq/umq_dfx_api.h"
#include "../../../../hcom/umq/include/umq/umq_dfx_types.h"
#include "../../../../hcom/umq/include/umq/umq_errno.h"
#include "../../../../hcom/umq/include/umq/umq_pro_api.h"
#include "../../../../hcom/umq/src/qbuf/qbuf_list.h"

namespace ock {
namespace ubs {
class UmqDataTxOps : public DataTxOps {
public:
    explicit UmqDataTxOps(uint64_t umq_handle = UMQ_INVALID_HANDLE) : local_umqh_(umq_handle)
    {
        // 初始化链表头尾为空
        QBUF_LIST_INIT(&head_buf_);
        QBUF_LIST_INIT(&tail_buf_);

        // 初始化流控与统计计数器为 0
        unsolicited_bytes_ = 0;
        unsolicited_wr_num_ = 0;
        unsignaled_wr_num_ = 0;
    }

    ~UmqDataTxOps() override = default;

    // 分配发送缓冲区
    uintptr_t AllocTxBuf(uint32_t count) override;

    // 发送请求
    int PostSend(uintptr_t buf, uint32_t batch, IovConverter cvt) override;

    int PollTx() override;

private:
    // --- 私有辅助函数 ---
    // 处理 umq_post 失败时的坏 buffer
    uint32_t HandleBadQBuf(umq_buf_t *tx_buf_list, umq_buf_t *bad_qbuf, umq_buf_t *head_qbuf,
        uint16_t _unsolicited_wr_num, uint32_t _unsolicited_bytes, uint16_t _unsignaled_wr_num);
    // 辅助函数
    void *PtrFloorToBoundary(void *ptr);        // 假设的指针对齐函数
    void UpdateTraceStats(int type, int value); // 假设的统计更新函数
    int PollUmqTx(bool);
    int GetAndAckEvent(umq_io_direction_t io_dir);
    int DpRearmTxInterrupt();

    bool CutLast(IovConverter cvt, uint32_t len, umq_buf_t *buf);
private:
    // --- 私有成员变量 ---
    // umq 相关的句柄
    uint64_t local_umqh_ = UMQ_INVALID_HANDLE;

    bool get_and_ack_event_ = false;

    /* m_tx.m_head_buf -> |umq_buf 0| -> |umq_buf 1| -> ... -> |umq_buf n| <- m_tx.m_tailbuf */
    umq_buf_list_t head_buf_ = { 0 };
    umq_buf_list_t tail_buf_ = { 0 };

    uint32_t unsolicited_bytes_ = 0;  // length of accumulated work request without setting solicited
    uint16_t unsolicited_wr_num_ = 0; // number of accumulated work request without setting solicited
    uint16_t unsignaled_wr_num_ = 0;  // number of accumulated work request without setting signaled
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_DATA_TX_H