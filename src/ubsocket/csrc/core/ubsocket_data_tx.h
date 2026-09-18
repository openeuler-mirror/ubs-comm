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
#ifndef UBS_COMM_UBSOCKET_DATA_TX_H
#define UBS_COMM_UBSOCKET_DATA_TX_H

#include <cstdint>

#include "common/ubsocket_common_includes.h"
#include "ubsocket_core_types.h"

namespace ock {
namespace ubs {
/*
 * Operation interface for data sending:
 * urma backend, shm backend, rdma backend, umq backend, need to implement this for data tx
 */
class DataTxOps {
public:
    virtual ~DataTxOps() = default;

    /**
     * @brief Simplified write interface for different protocol
     *
     * @param sock         [in] the socket object for writev
     * @param iov          [in] data io vector
     * @param iovcnt       [in] count of io vector
     * @param flags        [in] options flags
     *
     * @return written size or error
     */
    virtual ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept = 0;

    /**
     * @brief Flush data to peer before close, related socket linger related feature
     *
     * @param sock         [in] the socket object for flush
     * @param flags        [in] options flags
     *
     * @return 0 if successful or timeout
     */
    virtual Result Flush(const SocketPtr &sock, uint32_t flags) noexcept = 0;

    /**
     * @brief Trigger poll out event
     *
     * @param sock         [in] the socket object to be triggered
     * @param flags        [in] optional flags
     *
     * @return 0 if successful
     */
    virtual Result TriggerPollOut(const SocketPtr &sock, uint32_t flags) noexcept = 0;
};

// 通用层：流控、数据切分、故障回退
class DataTx {
public:
    DataTx() = default;
    DataTx(const SocketPtr &sock, DataTxOps *ops);
    ~DataTx();

    DataTx(DataTx &&other) noexcept;
    DataTx &operator=(DataTx &&other) noexcept;
    DataTx(const DataTx &) = delete;
    DataTx &operator=(const DataTx &) = delete;

    ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept;

    DataTxOps *GetTxOps() const noexcept;

private:
    int fd_ = -1;
    int event_fd_ = -1;
    DataTxOps *tx_ops_ = nullptr;
};

ALWAYS_INLINE DataTxOps *DataTx::GetTxOps() const noexcept
{
    return tx_ops_;
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_DATA_TX_H
