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

#include "common/ubsocket_common_includes.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/statistics/rx_stat_defs.h"
#include "ubsocket_core_types.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
class UmqSocket;
} // namespace umq
/* RX ops：PollRx / RxDataSet 等动作的唯一实现（原 UmqRxOps）。
 * 去虚化依据同 DataTxOps：TCP 直通在 DataRx 壳层短路，
 * GenerateSocketCommOps 只为 UMQ 类型接线——虚表与动态分发是纯开销。
 * UMQ 侧方法体在 umq_data_rx_ops.cpp。 */
class DataRxOps {
public:
    /* owner_ 显式后向指针，见 DataTxOps 同注释 */
    explicit DataRxOps(int fd, uint64_t umq_handle = 0, umq::UmqSocket *owner = nullptr)
        : fd_(fd), fallback_umqh_(umq_handle), owner_(owner)
    {
        if (GlobalSetting::UBS_MONITOR_ENABLE) {
            rx_stat_counters_.reset(new rxstat::RxStatCounters());
            std::memset(rx_stat_counters_.get(), 0, sizeof(rxstat::RxStatCounters));
        }
    }
    ~DataRxOps() = default;

    /**
     * poll rx and put data into block_cache_
     * @return
     */
    int PollRx(const SocketPtr &sock);
    ssize_t RxDataSet(void *buf, uint32_t size);
    int RearmRxInterrupt();
    void FlushRx(Socket *sock, uint32_t timeout_ms = FLUSH_TIMEOUT_MS);
    void HandleErrorRxCqe(umq_buf_t *buf);

    rxstat::RxStatCounters *GetRxStatCounters()
    {
        return rx_stat_counters_.get();
    }

public:
    // RX fields
    uint8_t epoll_in_msg_ = 0;
    uint8_t epoll_in_msg_recv_size_ = 0;
    uint16_t rx_queue_avail_num_ = 0; // current window size for RX
    uint16_t ack_event_num_ = 0;
    std::atomic<int> epoll_event_num_{0};
    int expect_epoll_event_num_ = 0;
    bool get_and_ack_event_ = false;
    bool poll_ = false;
    BlockCache block_cache_;
    size_t remaining_size_ = 0;
    bool flow_control_failed_ = false;
    uint32_t last_rx_seq_no_{0};

protected:
    Block *DataToBlock(void *data);

private:
    int GetQbuf(const SocketPtr &sock, umq_buf_t **buf, int max_num);
    int UmqPollAndRefillRx(umq_buf_t **buf, uint32_t max_buf_size);
    uint32_t HandleBadQBuf(umq_buf_t *head_qbuf, umq_buf_t *bad_qbuf);
    int GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size);
    int GetAndAckEvent();
    bool PollSubUmqRx(umq_buf_t *buf[], int i) const;

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

    std::unique_ptr<rxstat::RxStatCounters> rx_stat_counters_;

    friend class DataRx;
};

// 通用层：缓存获取数据，故障回退
class DataRx {
public:
    DataRx() = default;
    DataRx(const SocketPtr &sock, DataRxOps *ops);

    ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt);

    DataRxOps *GetRxOps()
    {
        return rx_ops_;
    }

private:
private:
    /* fd 不再另存一份：入口方法都携带 sock，直接读 sock->raw_socket_ */

    /* 非拥有指针：ops 按值内嵌于 UmqSocket，生命周期随 socket 本体 */
    DataRxOps *rx_ops_ = nullptr;
};
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UBSOCKET_DATA_RX_H
