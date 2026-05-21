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

#include "common/ubsocket_global_setting.h"
#include "iobuf/ubsocket_iobuf.h"
#include "ubsocket_buf_converter.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
// 接口层，实现 Alloc Post Free PollTx等动作
class DataTxOps {
public:
    DataTxOps()
    {
        tx_queue_avail_num_ = GlobalSetting::UBS_TX_DEPTH;
    }

    virtual ~DataTxOps() = default;

    virtual ConverterPtr BuildIovConverter(const struct iovec *iov, int iovcnt) = 0;

    virtual ConverterPtr BuildBufferConverter(const void *buf, size_t size) = 0;

    // 分配发送缓冲区
    virtual uintptr_t AllocTxBuf(uint32_t size, uint32_t count) = 0;

    // 投递发送请求
    virtual int PostSend(const SocketPtr &sock, uintptr_t buf_list, uint32_t batch, const ConverterPtr &cvt) = 0;

    virtual int PollTx(const SocketPtr &sock) = 0;

    virtual uint32_t IOBufSize() = 0;

protected:
    int fd_ = -1;
    uint16_t tx_queue_avail_num_ = 0; // current window size for TX
    uint16_t ack_event_num_ = 0;
    std::atomic<int> epoll_event_num_{0};
    int expect_epoll_event_num_ = 0;
    std::atomic<bool> need_fc_awake_{false};

    friend class DataTx;
};

// 通用层：流控、数据切分、故障回退
class DataTx {
public:
    DataTx() = default;
    DataTx(const SocketPtr &sock, DataTxOps *ops);

    ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);

private:
    int fd_ = -1;
    int event_fd_ = -1;
    DataTxOps *tx_ops_ = nullptr;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_DATA_TX_H
