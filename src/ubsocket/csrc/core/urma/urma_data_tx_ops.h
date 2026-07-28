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
#ifndef UBS_COMM_URMA_DATA_TX_OPS_H
#define UBS_COMM_URMA_DATA_TX_OPS_H

#include <cstdint>
#include <vector>

#include "core/ubsocket_data_tx.h"
#include "iobuf/allocator/ubsocket_alloc_fixed_size.h"
#include "urma_socket.h"

namespace ock {
namespace ubs {
namespace urma {

class UrmaTxOps : public DataTxOps {
public:
    explicit UrmaTxOps(int fd)
    {
        fd_ = fd;
    }

    ~UrmaTxOps() override = default;

    ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept override;
    Result Flush(const SocketPtr &sock, uint32_t flags) noexcept override;
    Result TriggerPollOut(const SocketPtr &sock, uint32_t flags) noexcept override;

private:
    ssize_t WriteInline(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total, uint32_t flags) noexcept;
    void CopyFromIovToSendBuf(const std::vector<iovec> &iov, const std::vector<urma_sge_t> &dest) noexcept;

    bool Writable(const SocketPtr &sock) noexcept;
    int fd_ = -1;
    uint64_t seq_gen_ = 0;
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_DATA_TX_OPS_H
