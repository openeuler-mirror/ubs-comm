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
#include "iobuf/ubsocket_iobuf.h"
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

    /*
     * Main send entry point (override of DataTxOps::WriteV). Dispatches by size:
     *   total ≤ URMA_INLINE_DATA_THRESHOLD     → WriteInline  (RDMA WRITE_IMM with inline data)
     *   UBS_IO_FLAG_USE_BRPC_BLOCK flag set    → WriteZeroCopy (large IO with BlockRef descriptors)
     *   otherwise                              → WriteLargeIO
     *
     * Returns: bytes sent, or -1 with errno set.
     */
    ssize_t WriteV(const SocketPtr &sock, const struct iovec *iov, int iovcnt, uint32_t flags) noexcept override;

    /*
     * Flush pending data to peer before close (override of DataTxOps::Flush).
     * Currently a no-op stub returning UBS_OK.
     */
    Result Flush(const SocketPtr &sock, uint32_t flags) noexcept override;

    /*
     * Trigger EPOLLOUT event on the socket (override of DataTxOps::TriggerPollOut).
     * Currently a no-op stub returning UBS_OK.
     */
    Result TriggerPollOut(const SocketPtr &sock, uint32_t flags) noexcept override;

private:
    /*
     * Holds the source/destination SGE arrays for a single RDMA write operation.
     * The underlying send_buf (UrmaSockContinuousBuf) guarantees physically
     * contiguous allocations, so only one SGE per side is needed.
     */
    struct SgeContext {
        std::vector<urma_sge_t> src_sges; /* source SGE array (local send_buf) */
        std::vector<urma_sge_t> dst_sges; /* destination SGE array (remote recv_buf) */
        urma_sg_t src_sg;                 /* source scatter-gather descriptor */
        urma_sg_t dst_sg;                 /* destination scatter-gather descriptor */
    };

    /*
     * Small IO path (≤URMA_INLINE_DATA_THRESHOLD). Copies control message header +
     * user data into a send_buf allocation, then posts RDMA WRITE_IMM with
     * WRITE_IN_BAND opcode. Data arrives inline at the receiver's recv_buf.
     *
     * Returns: payload bytes sent (excludes header), or -1 with errno set.
     */
    ssize_t WriteInline(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total, uint32_t flags) noexcept;

    /*
     * Large IO path (with UBS_IO_FLAG_USE_BRPC_BLOCK). Builds PageBufferInMessage
     * descriptors for each BRPC Block in the iov, then posts RDMA WRITE_IMM with
     * PRE_WRITE opcode. The receiver uses these descriptors to issue RDMA READs
     * that pull the actual data. Sender's Blocks are IncRef'd to prevent premature
     * release; they are DecRef'd when POST_WRITE acknowledgement arrives.
     *
     * Returns: total bytes committed, or -1 with errno set.
     */
    ssize_t WriteZeroCopy(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total, uint32_t flags) noexcept;

    /*
     * Large IO path WITHOUT zero-copy. Allocates local Blocks, copies user data
     * from iov into them, then sends via WriteZeroCopy. The allocated Blocks are
     * ref-counted and freed by the POST_WRITE/DecRef path on the receiver side.
     *
     * Returns: total bytes committed, or -1 with errno set.
     */
    ssize_t WriteLargeIO(const SocketPtr &sock, const iovec *iov, int iovcnt, size_t total, uint32_t flags) noexcept;

    /*
     * Scatter-gather copy from source iov entries into destination SGEs. Handles
     * source iov fragmentation across multiple dest SGE segments. Used to pack
     * the control header + user data into the send_buf before posting RDMA write.
     */
    void CopyFromIovToSendBuf(const std::vector<iovec> &iov, const std::vector<urma_sge_t> &dest) noexcept;

    /*
     * Sets up src/dst SGE arrays for an RDMA write from local send_buf to remote
     * recv_buf. The allocation is guaranteed contiguous (UrmaSockContinuousBuf),
     * so only a single SGE is needed per side.
     */
    void SetupSgeContext(const UrmaSocketPtr &sock, uint64_t buf_offset, uint64_t alloc_size,
                         SgeContext &ctx) noexcept;

    /*
     * Builds a vector of BlockRef descriptors from the iov entries. Each BRPC
     * Block backing an iov entry is identified via get_mini_block_head(). Skipped
     * entries (null base or zero length) are excluded.
     */
    void BuildBlockRefs(const struct iovec *iov, int iovcnt, std::vector<BlockRef> &block_refs) noexcept;

    Block *IovecToBlockList(const struct iovec *iov, int iovcnt) const;

    /*
     * Checks if the send_buf has enough free space for at least one allocation
     * unit, indicating the socket is ready for more writes.
     */
    bool Writable(const SocketPtr &sock) noexcept;

    int fd_ = -1;            /* socket file descriptor */
    uint64_t seq_gen_ = 0;   /* atomic sequence number generator for request IDs */
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_DATA_TX_OPS_H
