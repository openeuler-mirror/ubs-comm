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
#ifndef UBS_COMM_URMA_DATA_RX_OPS_H
#define UBS_COMM_URMA_DATA_RX_OPS_H

#include <cstdint>

#include "core/ubsocket_data_rx.h"
#include "urma_socket.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

class UrmaRxOps : public DataRxOps {
public:
    explicit UrmaRxOps(int fd)
    {
        fd_ = fd;
    }

    ~UrmaRxOps() override = default;

    Result DataReached(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm) noexcept;
    ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) override;
    Result TriggerPollIn(const SocketPtr &sock, uint32_t flags) override;

private:
    Result ResponseCtrlMessage(const SocketPtr &sock, IOOpcode opcode, uint64_t offset, uint32_t flags) noexcept;
    static constexpr size_t RING_SIZE = 256;
    static constexpr uint64_t INVALID_SEQ = UINT64_MAX;

    struct alignas(64) DataSlot {
        std::atomic<bool> valid{false};
        uint64_t seq_no{INVALID_SEQ};
        uint64_t write_imm{0};
    };

    int fd_ = -1;
    DataSlot data_slots_[RING_SIZE];
    std::atomic<uint64_t> consume_seq_no_{0};
    uint64_t consume_offset_{0};
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_DATA_RX_OPS_H
