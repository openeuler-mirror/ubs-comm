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

/*
 * Receive slot state machine for tracking per-message lifecycle:
 *   IDLE      --(WRITE_IN_BAND CQE arrives)--> DATA_READY  (inline data, ready to consume)
 *   IDLE      --(PRE_WRITE CQE arrives)------> READING     (RDMA READ issued)
 *   READING   --(RDMA READ CQE completes)----> DATA_READY  (data in blocks, ready to consume)
 *   DATA_READY--(consumer fully reads)-------> IDLE         (slot freed, seq_no advanced)
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

    /*
     * Called from CQE poller when an URMA WRITE_IMM completion arrives. Stores the
     * sequence number and immediate data into the ring slot. For PRE_WRITE (large IO),
     * triggers RDMA READ via IssueReadRequest; for WRITE_IN_BAND, marks slot as
     * DATA_READY for consumer to pick up via ReadV.
     *
     * Returns: UBS_OK if slot is free and data was stored,
     *          UBS_LATER_SEQ if out-of-order (higher seq_no than consumed),
     *          UBS_PREFILL_RX if slot is occupied by a live message.
     */
    Result DataReached(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm) noexcept;

    /*
     * Called from CQE poller when a previously issued RDMA READ completes. Validates
     * slot state is READING, sends a POST_WRITE response to the sender (via
     * ResponseCtrlMessage), and transitions slot to DATA_READY.
     *
     * Returns: UBS_OK if the consumed seq_no matches (consumer can proceed),
     *          UBS_LATER_SEQ if out-of-order,
     *          UBS_ERROR if state was not READING.
     */
    Result DataReadFinished(const SocketPtr &sock, uint64_t seq_no) noexcept;

    /*
     * Main read entry point (override of DataRxOps::ReadV). Consumes ready data from
     * the ring slots in sequence order. For PRE_WRITE slots with UBS_IO_FLAG_USE_BRPC_BLOCK
     * flag, takes the zero-copy path (ZeroCopyRead); otherwise copies inline data from
     * recv_buf via ConsumeInlineData, or copies RDMA-read Block data via ConsumeBlockData.
     *
     * Returns: number of bytes copied to iov, or -1 with errno set (EINVAL/EAGAIN).
     */
    ssize_t ReadV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) override;

    /*
     * Trigger poll-in event on the socket (override of DataRxOps::TriggerPollIn).
     * Currently a no-op stub returning UBS_OK.
     */
    Result TriggerPollIn(const SocketPtr &sock, uint32_t flags) override;

    /*
     * Obtains the trace time of the current slot.
     * @return trace time of the current slot, 0 for error
     */
    uint64_t CurrentTraceNsTime();

private:
    /* Ring buffer slot tracking per-message receive state. 64-byte aligned for cache-line isolation. */
    struct alignas(64) DataSlot {
        std::atomic<LargeIoRxState> rx_state{URMA_RX_STS_IDLE}; /* current state in the state machine */
        uint64_t seq_no{INVALID_SEQ};                            /* message sequence number */
        uint64_t write_imm{0};                                   /* WRITE_IMM immediate data (opcode + buffer offset) */
        Block *data_block{nullptr};                              /* chain of received data blocks (PRE_WRITE path) */
        uint64_t start_read_time{0};                             /* timestamp when RDMA READ was issued (for timeout) */
    };

    /* Cursor tracking scatter-gather read progress across iov entries. */
    struct IovWriter {
        const struct iovec *iov; /* pointer to user iov array */
        int iovcnt;              /* number of iov entries */
        int idx;                 /* current iov entry index */
        size_t offset;           /* byte offset within current iov entry */
        size_t written;          /* total bytes written so far */
    };

    /*
     * Accumulator context for BuildReadWrs. Collects constructed RDMA READ
     * work requests and their SGE arrays. Pre-reserves 256 WR slots.
     */
    struct BuildReadWrCtx {
        UrmaIoBufferPool *allocator;
        UrmaSocket *urma_sock;
        uint64_t user_ctx;
        std::vector<urma_jfs_wr_t> wrs;
        std::vector<urma_sge_t> src_sges;
        std::vector<urma_sge_t> dst_sges;

        BuildReadWrCtx(UrmaIoBufferPool *alloca, UrmaSocket *sock, uint64_t ctx) noexcept
            : allocator(alloca),
              urma_sock(sock),
              user_ctx(ctx)
        {
            wrs.reserve(256U);
            src_sges.reserve(256U);
            dst_sges.reserve(256U);
        }
    };

    /*
     * Sends a control response via RDMA PostWriteImm. Used for:
     *   POST_WRITE       — acknowledge large IO completion to sender
     *   WRITE_IN_BAND_ACK — acknowledge inline write data consumed
     * Writes sizeof(UrmaMessageHead) from local recv_buf to remote send_buf.
     */
    Result ResponseCtrlMessage(const SocketPtr &sock, IOOpcode opcode, uint64_t offset, uint32_t flags) noexcept;

    /*
     * Processes a PRE_WRITE notification for large IO. Reads PageBufferInMessage
     * entries from recv_buf, imports remote segments, allocates local Blocks,
     * builds and posts RDMA READ WRs. Slot moves IDLE→READING; DataReadFinished
     * is called when the RDMA READ CQE arrives.
     */
    Result IssueReadRequest(const SocketPtr &sock, uint64_t seq_no, uint64_t write_imm, DataSlot &slot) noexcept;

    /*
     * Allocates a chain of Blocks from the IO buffer pool to hold total_bytes.
     * Blocks are linked via SetNext(). Returns nullptr on allocation failure.
     */
    Block *AllocBlocksForReq(uint32_t total_bytes) noexcept;

    /*
     * Builds RDMA READ WRs from the sender's PageBufferInMessage descriptors.
     * Pairs each remote buffer segment with the corresponding local Block.
     * Results accumulated into ctx.wrs as a linked list.
     */
    void BuildReadWrs(uint32_t data_count, uint64_t msg_offset, Block *head_block,
                      BuildReadWrCtx &ctx) noexcept;

    /*
     * Stores remote memory segment mappings (addr → target_seg) for each buffer
     * in the sender's PageBufferInMessage array. Required before RDMA READ can
     * target the remote memory regions.
     */
    void ImportRemoteSegments(const SocketPtr &sock, uint32_t data_count, uint64_t offset) noexcept;

    /*
     * Zero-copy read for PRE_WRITE (large IO) with UBS_IO_FLAG_USE_BRPC_BLOCK.
     * Moves slot's data_block chain into block_cache_, then cuts blocks from cache
     * and links them to the output Block (iov[0].iov_base). Advances consume_seq_no_
     * when cache is fully drained.
     *
     * Returns: bytes linked into the output chain.
     */
    ssize_t ZeroCopyRead(const SocketPtr &sock, DataSlot &slot, uint64_t seq, IovWriter &w) noexcept;

    /*
     * Copies data from the slot's Block chain (populated by RDMA READ) into the
     * user iovec. Handles partial reads via consume_offset_. Sends POST_WRITE and
     * advances consume_seq_no_ when fully consumed.
     *
     * Returns: true if all data consumed, false if partially read.
     */
    bool ConsumeBlockData(const SocketPtr &sock, DataSlot &slot, IovWriter &w, uint32_t flags) noexcept;

    /*
     * Copies inline data from recv_buf (sender's WRITE_IN_BAND) into the user
     * iovec. Sends WRITE_IN_BAND_ACK and advances consume_seq_no_ when fully
     * consumed.
     *
     * Returns: true if all inline data consumed, false if partially read.
     */
    bool ConsumeInlineData(const SocketPtr &sock, DataSlot &slot, IovWriter &w, uint32_t flags) noexcept;

    /*
     * Releases all Blocks in a slot's data_block chain via DecRef(). Sets
     * slot.data_block to nullptr. Static — does not depend on instance state.
     */
    static void ReleaseBlockInSlot(DataSlot &slot) noexcept;
    static constexpr size_t RING_SIZE = 256;
    static constexpr uint64_t INVALID_SEQ = UINT64_MAX;

private:
    int fd_ = -1;                                       /* socket file descriptor */
    std::atomic<uint64_t> consume_seq_no_{0};         /* next sequence number expected by consumer (ReadV) */
    uint64_t consume_offset_{0};                        /* bytes already consumed from current slot (partial read) */
    DataSlot data_slots_[RING_SIZE];                    /* per-sequence-number receive state ring */
    BlockCache block_cache_;                            /* cache for zero-copy block management */
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_DATA_RX_OPS_H
