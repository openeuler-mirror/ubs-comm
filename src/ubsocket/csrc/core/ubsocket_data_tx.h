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
#include <memory>

#include "buffer_util.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
// 接口层，实现 Alloc Post Free PollTx等动作
class DataTxOps {
public:
    DataTxOps()
    {
        tx_queue_avail_num_ = GlobalSetting::GetTxDepth();
    }

    virtual ~DataTxOps() = default;

    // 分配发送缓冲区
    virtual uintptr_t AllocTxBuf(uint32_t count) = 0;

    // 投递发送请求
    virtual int PostSend(uintptr_t buf_list, uint32_t batch) = 0;

    virtual int PollTx() = 0;

    virtual int GetAndAckEvent() = 0;

    virtual uint32_t IOBufSize() = 0;

    DEFINE_REF_OPERATION_FUNC

protected:
    DECLARE_REF_COUNT_VARIABLE

    int fd_ = -1;
    uint16_t tx_queue_avail_num_ = 0; // current window size for TX
    uint16_t ack_event_num_ = 0;
    std::atomic<int> epoll_event_num_{0};
    int expect_epoll_event_num_ = 0;

    std::atomic<bool> need_fc_awake_{false};

    uint64_t protocol_negotiation_ = 0;
    uint32_t protocol_negotiation_recv_size_ = 0;
    uint32_t protocol_negotiation_offset_ = 0;

    friend class DataTx;
};
using DataTxOpsPtr = Ref<DataTxOps>;

// 通用层：流控、数据切分、故障回退
class DataTx {
public:
    DataTx() = default;

    /**
     * @brief 构造函数
     *
     * @param ops 具体的协议实现 (UMQ 或 RDMA)
     */
    DataTx(const SocketInfo &info, DataTxOpsPtr ops);

    ALWAYS_INLINE ssize_t WriteV(const SocketInfo &sock, const struct iovec *iov, int iovcnt);

private:
    int fd_ = -1;
    int event_fd_ = -1;
    DataTxOpsPtr tx_ops_{nullptr};
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_DATA_TX_H
