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
#ifndef UBS_COMM_URMA_IO_BUF_POOL_H
#define UBS_COMM_URMA_IO_BUF_POOL_H

#include <limits.h>
#include <pthread.h>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "common/ubsocket_common_includes.h"
#include "iobuf/allocator/ubsocket_alloc_fixed_size.h"
#include "iobuf/ubsocket_zcopy_adapter.h"
#include "under_api/urma/urma_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {

/**
 * @brief Unique identifier for a remote node
 *
 * A remote node is identified by the triple (EID, process-id, namespace).
 * Used as the key in unordered_map for remote segment lookup.
 */
struct IdentifyNode {
    urma_eid_t eid;        /**< URMA endpoint identifier (16 bytes) */
    uint32_t pid;          /**< OS process identifier */
    char ns[NAME_MAX + 1]; /**< Logical namespace for multi-tenancy */

    IdentifyNode() noexcept : eid({0}), pid(0)
    {
        ns[0] = '\0';
    }

    explicit IdentifyNode(urma_eid_t e, uint32_t p, const char *n) noexcept : eid(e), pid(p)
    {
        strncpy(ns, n, NAME_MAX + 1);
        ns[NAME_MAX] = '\0';
    }

    bool operator==(const IdentifyNode &other) const
    {
        return eid.in6.subnet_prefix == other.eid.in6.subnet_prefix &&
               eid.in6.interface_id == other.eid.in6.interface_id && pid == other.pid &&
               strncmp(ns, other.ns, NAME_MAX + 1) == 0;
    }
};

/**
 * @brief Hash functor for IdentifyNode
 *
 * Combines hashes of all four fields (subnet_prefix, interface_id, pid, ns)
 * using XOR, suitable for use as the hash policy of std::unordered_map.
 */
struct RemoteNodeHash {
    size_t operator()(const IdentifyNode &node) const noexcept
    {
        return std::hash<uint64_t>()(node.eid.in6.subnet_prefix) ^ std::hash<uint64_t>()(node.eid.in6.interface_id) ^
               std::hash<uint32_t>()(node.pid) ^ std::hash<std::string>()(node.ns);
    }
};

/**
 * @brief Per-remote-node segment tracking
 *
 * Holds the remote peer's buffer base address and a vector of URMA target
 * segments registered for that remote node. Shared across connections via
 * reference counting (inherits Referable).
 */
class RemoteSegments : public Referable {
public:
    uintptr_t remote_start_addr_;                         /**< Base address of remote buffer pool */
    std::vector<urma_target_seg_t *> remote_target_segs_; /**< URMA target segments indexed by block */
    pthread_spinlock_t target_lock_;

    RemoteSegments() noexcept : remote_start_addr_(0)
    {
        pthread_spin_init(&target_lock_, PTHREAD_PROCESS_PRIVATE);
    }

    ~RemoteSegments() override
    {
        pthread_spin_destroy(&target_lock_);
    }
};
using RemoteSegmentsPtr = Ref<RemoteSegments>;

/**
 * @brief Local and remote segment registry for zero-copy allocator
 *
 * Manages two-level segment lookup:
 * - local_target_segs_: URMA segments for local buffer blocks (indexed by block index)
 * - remote_target_segs_: per-remote-node segment vectors (keyed by IdentifyNode)
 *
 * Protected by a pthread spinlock for concurrent remote segment insertion/lookup.
 * Constructed and destroyed together with the owning UrmaIoBufferPool.
 */
struct MemPoolTargetSegments {
    std::vector<urma_target_seg_t *> local_target_segs_; /**< Local buffer segments */
    std::unordered_map<IdentifyNode, RemoteSegmentsPtr, RemoteNodeHash>
        remote_target_segs_;                  /**< Remote buffer segments */
    pthread_spinlock_t remote_segments_lock_; /**< Protects remote_target_segs_ */

    MemPoolTargetSegments() noexcept
    {
        pthread_spin_init(&remote_segments_lock_, PTHREAD_PROCESS_PRIVATE);
    }

    ~MemPoolTargetSegments()
    {
        pthread_spin_destroy(&remote_segments_lock_);
    }
};

/**
 * @brief URMA zero-copy memory allocator
 *
 * Implements UbsZeroCopyAllocator using URMA registered memory backed by a
 * fixed-size block allocator. Maintains two-level segment tracking:
 * - Local segments: URMA target segments for own buffer blocks, registered
 *   on allocation and indexed by block index in MemPoolTargetSegments.
 * - Remote segments: per-remote-node segment vectors, lazily created on
 *   first access and keyed by IdentifyNode.
 *
 * Allocation and deallocation delegate to the underlying FixedAllocator,
 * with memory registration/deregistration callbacks triggered on pool
 * expansion and contraction.
 */
class UrmaIoBufferPool : public UbsZeroCopyAllocator {
public:
    Result init(const UrmaContextPtr &ctx) noexcept;
    Result init(const allocator::FixedAllocatorOptions &options, const UrmaContextPtr &ctx) noexcept;
    void un_init() noexcept;
    void *allocate(size_t size) override;
    void *allocate(size_t size, const ubs_iobuf_alloc_option_t *option) override;
    void deallocate(void *ptr) override;
    void *direct_allocate(size_t size) noexcept;
    void direct_release(void *ptr) noexcept;
    uintptr_t get_local_start_addr() const noexcept
    {
        return local_start_addr_;
    }
    urma_target_seg_t *get_local_target_seg(uintptr_t ptr) const noexcept;
    RemoteSegmentsPtr get_remote_segments(const IdentifyNode &remote) noexcept;
    urma_target_seg_t *get_remote_target_seg(RemoteSegmentsPtr &remote, uintptr_t ptr) const noexcept;
    Result store_remote_target_seg(RemoteSegmentsPtr &remote, const urma_seg_t &seg, urma_target_seg_t *&tgt) noexcept;

    DEFINE_REF_OPERATION_FUNC

private:
    Result register_mem(const UrmaContextPtr &ctx, uintptr_t ptr, size_t size) noexcept;
    Result deregister_mem(const UrmaContextPtr &ctx, uintptr_t ptr, size_t size) noexcept;

    uintptr_t local_start_addr_{0};
    MemPoolTargetSegments target_segments_;
    DECLARE_REF_COUNT_VARIABLE;
    allocator::FixedAllocator fixed_allocator_;
    allocator::FixedAllocatorOptions options_;
};
using UrmaIoBufferPoolPtr = Ref<UrmaIoBufferPool>;

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_IO_BUF_POOL_H
