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
#include "iobuf/ubsocket_iobuf.h"
#include "urma_socket.h"
#include "urma_socket_types.h"

namespace ock {
namespace ubs {
namespace urma {

/**
 * @brief 数据接收状态
 * IDLE --(收到写消息，且是大IO)--> CTRL_REQ_RECV
 * CTRL_REQ_RECV --(本地未import 对端seg, 发送消息请求SEG信息)--> WAIT_REMOTE_SEG
 * CTRL_REQ_RECV --(本地已import，已发送单边读)--> READING
 * WAIT_REMOTE_SEG --(收到对端seg信息，已import，已发送单边读)--> READING
 * READING --(读完成)--> DATA_READY
 * IDLE --(收到写消息，且是小IO)--> DATA_READY
 */
enum LargeIoRxState : uint8_t {
    URMA_RX_STS_IDLE = 0,
    URMA_RX_STS_READING,
    URMA_RX_STS_DATA_READY,
    URMA_RX_STS_COUNT
};

class UrmaRxOps : public DataRxOps {
public:
    explicit UrmaRxOps(int fd) : fd_(fd) {}

    ~UrmaRxOps() override = default;

    Result DataReached(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm) noexcept;
    Result DataReadFinished(const SocketPtr &sock, uint64_t seq_no) noexcept;
    ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) override;
    Result TriggerPollIn(const SocketPtr &sock, uint32_t flags) override;

private:
    struct alignas(64) DataSlot {
        std::atomic<LargeIoRxState> rx_state{URMA_RX_STS_IDLE};
        uint64_t seq_no{INVALID_SEQ};
        uint64_t write_imm{0};
        Block *data_block{nullptr};
    };

    struct IovWriter {
        const struct iovec *iov;
        int iovcnt;
        int idx;
        size_t offset;
        size_t written;
    };

    struct BuildReadWrCtx {
        UrmaIoBufferPool *allocator;
        UrmaSocket *urma_sock;
        uint64_t user_ctx;
        std::vector<urma_jfs_wr_t> wrs;
        std::vector<urma_sge_t> src_sges;
        std::vector<urma_sge_t> dst_sges;

        BuildReadWrCtx(UrmaIoBufferPool *alloca, UrmaSocket *sock, uint64_t ctx) noexcept: allocator(alloca), urma_sock(sock), user_ctx(ctx)
        {
            wrs.reserve(256U);
            src_sges.reserve(256U);
            dst_sges.reserve(256U);
        }
    };

    Result ResponseCtrlMessage(const SocketPtr &sock, IOOpcode opcode, uint64_t offset, uint32_t flags) noexcept;
    Result ReadDataToBlocks(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm, DataSlot &slot) noexcept;
    Block *AllocBlocksForReq(uint32_t total_bytes) noexcept;
    void BuildReadWrs(const UrmaCtrlMessage *req, Block *head_block, BuildReadWrCtx &ctx) noexcept;
    void ImportRemoteSegments(const SocketPtr &sock, const UrmaCtrlMessage *req) noexcept;
    ssize_t ZeroCopyRead(const SocketPtr &sock, DataSlot &slot, uint64_t seq, const WriteImmData &imm_data,
                         IovWriter &w, uint32_t flags) noexcept;
    bool ReadBlockData(const SocketPtr &sock, DataSlot &slot, IovWriter &w, uint32_t flags) noexcept;
    bool ReadInlineData(const SocketPtr &sock, DataSlot &slot, IovWriter &w, uint32_t flags) noexcept;
    static void ReleaseBlockInSlot(DataSlot &slot) noexcept;
    static constexpr size_t RING_SIZE = 256;
    static constexpr uint64_t INVALID_SEQ = UINT64_MAX;

private:
    int fd_ = -1;
    std::atomic<uint64_t> consume_seq_no_{0};
    uint64_t consume_offset_{0};
    DataSlot data_slots_[RING_SIZE];
    BlockCache block_cache_;
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_DATA_RX_OPS_H
