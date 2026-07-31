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
#include "common/ubsocket_obj_pool.h"
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
    Result store_remote_target_seg(RemoteSegmentsPtr &remote, urma_seg_t &seg, urma_target_seg_t *&tgt) noexcept;

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

class UrmaDataMetaPool {
public:
    static UrmaDataMetaPool &Instance()
    {
        static UrmaDataMetaPool meta_pool;
        return meta_pool;
    }

public:
    Result Init() noexcept;
    void UnInit() noexcept;

    UrmaDataMeta *Allocate() noexcept;
    void Free(UrmaDataMeta *m) noexcept;

    UrmaDataMeta *BatchAllocate(uint16_t batchSize) noexcept;
    void BatchFree(UrmaDataMeta *m) noexcept;

private:
    ObjectThreadCache &GetTc() noexcept;

private:
    ObjectGlobalCachePtr global_cache_;
    std::mutex mutex_;
};

ALWAYS_INLINE Result UrmaDataMetaPool::Init() noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (global_cache_ != nullptr) {
        UBS_VLOG_DEBUG("Already initialized, skip");
        return UBS_OK;
    }

    ObjectPoolOptions options;
    options.block_size_mb = 2;
    options.mini_block_size_bytes = 64;
    options.tc_batch_alloc_count = 128;

    auto tmp_gc = MakeRef<ObjectGlobalCache>("UrmaDataMetaPool", options);
    if (tmp_gc == nullptr) {
        UBS_VLOG_ERR("Failed to init UrmaDataMetaPool ojbect, as create global cache failed, probably out of memory");
        return UBS_ERROR;
    }

    auto result = tmp_gc->Initialize();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("Failed to init UrmaDataMetaPool ojbect as init global cache failed");
        return UBS_ERROR;
    }

    global_cache_ = tmp_gc;

    UBS_VLOG_DEBUG("Init UrmaDataMetaPool finished");
    return UBS_OK;
}

ALWAYS_INLINE void UrmaDataMetaPool::UnInit() noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    if (global_cache_ == nullptr) {
        UBS_VLOG_DEBUG("Not initialized, skip");
        return;
    }

    global_cache_->UnInitialize();
    global_cache_ = nullptr;
}

ALWAYS_INLINE ObjectThreadCache &UrmaDataMetaPool::GetTc() noexcept
{
    static thread_local ObjectThreadCache tc(global_cache_.Get());
    return tc;
}

ALWAYS_INLINE UrmaDataMeta *UrmaDataMetaPool::Allocate() noexcept
{
    return reinterpret_cast<UrmaDataMeta *>(GetTc().Allocate());
}

ALWAYS_INLINE void UrmaDataMetaPool::Free(UrmaDataMeta *m) noexcept
{
    GetTc().Free(reinterpret_cast<void *>(m));
}

ALWAYS_INLINE UrmaDataMeta *BatchAllocate(uint16_t batchSize) noexcept
{
    // TODO
    (void)batchSize;
    return nullptr;
}

ALWAYS_INLINE void BatchFree(UrmaDataMeta *m) noexcept
{
    (void)m;
    // TODO
}
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_IO_BUF_POOL_H
