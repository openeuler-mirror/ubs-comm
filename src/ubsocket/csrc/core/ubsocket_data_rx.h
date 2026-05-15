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
#ifndef UBS_COMM_UBSOCKET_DATA_RX_H
#define UBS_COMM_UBSOCKET_DATA_RX_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "ubsocket_core_types.h"
#include "ubsocket_defines.h"
#include "ubsocket_global_setting.h"
#include "ubsocket_iobuf.h"

namespace ock {
namespace ubs {
// 接口层，实现 Polltx等动作
class DataRxOps {
public:
    DataRxOps() = default;
    virtual ~DataRxOps() = default;

    virtual int PollRx(bool flow_control_failed) = 0;
    virtual int RearmRxInterrupt() = 0;

protected:
    virtual void *PtrFloorToBoundary(void *ptr) = 0;

protected:
    int fd_ = -1;
    // RX fields
    uint8_t epoll_in_msg_ = 0;
    uint8_t epoll_in_msg_recv_size_ = 0;
    uint16_t rx_queue_avail_num_ = 0; // current window size for RX
    uint16_t event_num_ = 0;
    std::atomic<int> epoll_event_num_{0};
    int expect_epoll_event_num_ = 0;
    bool get_and_ack_event_ = false;
    bool poll_ = false;
    BlockCache block_cache_;
    std::atomic<bool> need_fc_awake_{false};

    size_t remaining_size_ = 0;

    friend class DataRx;
};

// 通用层：缓存获取数据，故障回退
class DataRx {
public:
    DataRx() = default;
    DataRx(const SocketPtr &sock, DataRxOps *ops);

    ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);

private:
    ssize_t OutputErrorMagicNumber(const struct iovec *iov, int iovcnt);

private:
    bool flow_control_failed_ = false;
    int fd_ = -1;
    int event_fd_ = -1;

    DataRxOps *rx_ops_ = nullptr;
};
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UBSOCKET_DATA_RX_H