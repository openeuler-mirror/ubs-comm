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

#include "common/ubsocket_common_includes.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
/*
 * Operation interface for data receive:
 * urma backend, shm backend, rdma backend, need to implement this for data rx
 */
class DataRxOps {
public:
    virtual ~DataRxOps() = default;

    /**
     * @brief Simplified read interface for different protocol
     *
     * @param sock         [in] the socket object to be operated on
     * @param iov          [in] io vector for data receiving
     * @param iovcnt       [in] count of vector
     * @param flags        [in] optional flags
     *
     * @return size of data read or error
     */
    virtual ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) = 0;

    /**
     * @brief Trigger poll in event
     *
     * @param sock         [in] the socket object to be operated on
     * @param event        [in] the reached data event for IN
     * @param flags        [in] optional flags
     * @return
     */
    virtual Result TriggerPollIn(const SocketPtr &sock, uint32_t flags) = 0;
};

// 通用层：缓存获取数据，故障回退
class DataRx {
public:
    DataRx() = default;
    DataRx(const SocketPtr &sock, DataRxOps *ops);
    ~DataRx();

    DataRx(DataRx &&other) noexcept;
    DataRx &operator=(DataRx &&other) noexcept;
    DataRx(const DataRx &) = delete;
    DataRx &operator=(const DataRx &) = delete;

    ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags);

    DataRxOps *GetRxOps() const noexcept;

private:
    ssize_t OutputErrorMagicNumber(const SocketPtr &sock, const struct iovec *iov, int iovcnt);

private:
    int fd_ = -1;
    int event_fd_ = -1;
    // TODO 初始化赋值
    uint64_t protocol_negotiation_ = 0;
    uint32_t protocol_negotiation_recv_size_ = 0;
    uint32_t protocol_negotiation_offset_ = 0;

    DataRxOps *rx_ops_ = nullptr;
};

ALWAYS_INLINE DataRxOps *DataRx::GetRxOps() const noexcept
{
    return rx_ops_;
}
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UBSOCKET_DATA_RX_H
