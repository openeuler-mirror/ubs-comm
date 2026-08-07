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

#ifndef UBS_COMM_UBSOCKET_SOCKET_BUF_H
#define UBS_COMM_UBSOCKET_SOCKET_BUF_H

#include <pthread.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory_resource>
#include <set>

#include "common/ubsocket_ref.h"

namespace ock {
namespace ubs {
namespace urma {

/**
 * @brief Abstract interface for URMA socket buffer allocators
 *
 * Provides offset-based allocation and release for fixed-size RDMA memory buffers
 * registered with the NIC. Concrete implementations may use ring buffer, free list,
 * buddy allocator, or other strategies.
 */
class UrmaSockBuf : public Referable {
public:
    /** Minimum allocation granularity (bytes) */
    static constexpr uint64_t kChunkSize = 64;
    /** Maximum capacity (4MB) */
    static constexpr uint64_t kMaxCapacity = 4 * 1024 * 1024;
    /** Bitmap array size (computed from max capacity) */
    static constexpr uint64_t kMaxChunks = kMaxCapacity / kChunkSize;

    virtual ~UrmaSockBuf() = 0;

    /**
     * @brief Allocate buffer space (allocator hot path)
     * @param size   Requested bytes, internally rounded up to kChunkSize multiple
     * @param offset [out] Physical offset of the allocated region
     * @return true on success, false if insufficient space
     */
    virtual bool Allocate(uint64_t size, uint64_t &offset) noexcept = 0;

    /**
     * @brief Allocate buffer space within a range (allocator hot path)
     * @param minSize Minimum acceptable bytes, internally rounded up to kChunkSize multiple
     * @param maxSize Preferred maximum bytes, internally rounded up to kChunkSize multiple
     * @param offset  [out] Physical offset of the allocated region
     * @return actual allocated bytes in [aligned(minSize), aligned(maxSize)], or 0 if insufficient space
     */
    virtual uint64_t Allocate(uint64_t minSize, uint64_t maxSize, uint64_t &offset) noexcept = 0;

    /**
     * @brief Release buffer space (reclaimer path)
     * @param offset Physical offset returned by Allocate
     * @param size   Original byte count (same value passed to Allocate)
     */
    virtual void Release(uint64_t offset, uint64_t size) noexcept = 0;

    /**
     * @brief Query available space (called by allocator)
     * @return Allocatable byte count
     */
    virtual uint64_t Available() const noexcept = 0;

    /**
     * @brief Query used space
     * @return Allocated but not yet released byte count
     */
    virtual uint64_t Used() const noexcept = 0;

    /**
     * @brief Query total buffer capacity
     * @return capacity value passed at construction
     */
    virtual uint64_t Capacity() const noexcept = 0;
};
using UrmaSockBufPtr = Ref<UrmaSockBuf>;

/**
 * @brief Lock-free ring buffer allocator for URMA socket buffers
 *
 * Ring Buffer + Bitmap based SPSC (single-allocator single-reclaimer) lock-free allocator.
 *
 * ## Design points
 * - **Coordinate system**: alloc_tail_ / reclaim_tail_ use logical offsets (monotonic uint64_t),
 *   physical offset = logical_pos & cap_mask_
 * - **Lock-free principle**: Allocator owns alloc_tail_ + never touches bitmap; Reclaimer owns
 *   bitmap + reclaim_tail_; alloc_tail_atomic_ one-way sync to Reclaimer, reclaim_tail_ one-way sync to Allocator
 * - **Allocation granularity**: 64B chunk, size rounded up to 64B boundary
 * - **Capacity constraint**: capacity must be power-of-2 and 64B-aligned, max 4MB
 * - **Memory layout**: alloc_tail_ / alloc_tail_atomic_ / reclaim_tail_ each on independent cache lines
 *   to eliminate false sharing
 */
class UrmaSockRingBuf : public UrmaSockBuf {
public:
    /**
     * @brief Construct the allocator
     * @param capacity Total buffer capacity, must satisfy: >0 && <=kMaxCapacity && power-of-2 && 64B-aligned
     * @throw std::invalid_argument if constraints are violated
     */
    explicit UrmaSockRingBuf(uint64_t capacity);

    ~UrmaSockRingBuf() override = default;

    bool Allocate(uint64_t size, uint64_t &offset) noexcept override;
    uint64_t Allocate(uint64_t minSize, uint64_t maxSize, uint64_t &offset) noexcept override;
    void Release(uint64_t offset, uint64_t size) noexcept override;
    uint64_t Available() const noexcept override;
    uint64_t Used() const noexcept override;
    uint64_t Capacity() const noexcept override
    {
        return capacity_;
    }

private:
    void ValidateArgs(uint64_t capacity) const;

    // --- Bitmap helpers (reclaimer-only) ---

    /** Logical offset to physical chunk index */
    uint64_t ChunkIndex(uint64_t logical_pos) const noexcept;
    /** Mark chunks [start_chunk, start_chunk+count) as released */
    void MarkReleased(uint64_t start_chunk, uint64_t count) noexcept;
    /** Check whether the chunk at logical_pos is released */
    bool IsChunkReleased(uint64_t logical_pos) const noexcept;
    /** Clear the release flag for the chunk at logical_pos */
    void ClearChunk(uint64_t logical_pos) noexcept;

    // ---- Allocator domain (cache line 0) ----
    alignas(64) uint64_t alloc_tail_{0};

    // ---- Reclaimer-visible copy of alloc_tail_ (cache line 1) ----
    alignas(64) std::atomic<uint64_t> alloc_tail_atomic_{0};

    // ---- Reclaimer domain (cache line 2) ----
    alignas(64) std::atomic<uint64_t> reclaim_tail_{0};

    // ---- Read-only (cache line 3) ----
    const uint64_t capacity_;
    const uint64_t cap_mask_;
    const uint64_t num_chunks_;

    // ---- Bitmap (reclaimer exclusive read/write, cache line 4+) ----
    alignas(64) uint64_t bitmap_[UrmaSockBuf::kMaxChunks / 64];

    friend class UrmaSockContinuousBuf;
};

/**
 * @brief Red-black tree based allocator for URMA socket buffers
 *
 * Uses two red-black trees (`std::map` + `std::set`) to manage free blocks.
 * Protected by a pthread spinlock for thread safety between allocator and reclaimer.
 *
 * - **addressTree_**: `std::map<uint64_t, uint64_t>` keyed by offset (address) for O(log n)
 *   predecessor/successor lookup during Release coalescing.
 * - **sizeTree_**: `std::set<SpaceRange, RangeSizeFirst>` sorted by size-then-offset,
 *   used for O(log n) best-fit search during Allocate via `lower_bound`.
 * - **free_space_**: O(1) running counter for Available/Used queries.
 */
class UrmaSockRbTreeBuf : public UrmaSockBuf {
public:
    explicit UrmaSockRbTreeBuf(uint64_t capacity);
    ~UrmaSockRbTreeBuf() override;

    bool Allocate(uint64_t size, uint64_t &offset) noexcept override;
    uint64_t Allocate(uint64_t minSize, uint64_t maxSize, uint64_t &offset) noexcept override;
    void Release(uint64_t offset, uint64_t size) noexcept override;
    uint64_t Available() const noexcept override;
    uint64_t Used() const noexcept override;
    uint64_t Capacity() const noexcept override
    {
        return capacity_;
    }

private:
    void ValidateArgs(uint64_t capacity) const;

    struct SpaceRange {
        const uint64_t offset;
        const uint64_t size;

        SpaceRange(uint64_t o, uint64_t s) noexcept : offset{o}, size{s} {}
    };

    struct RangeSizeFirst {
        bool operator()(const SpaceRange &sr1, const SpaceRange &sr2) const noexcept;
    };

    const uint64_t capacity_;
    uint64_t free_space_{0};
    mutable pthread_spinlock_t lock_{};
    std::pmr::unsynchronized_pool_resource pool_res_;
    std::pmr::map<uint64_t, uint64_t> addressTree_;      ///< Free blocks by offset, key=offset, value=size
    std::pmr::set<SpaceRange, RangeSizeFirst> sizeTree_; ///< Free blocks by size (best-fit lookup)
};

/**
 * @brief Offset-shifting decorator for URMA socket buffer allocators
 *
 * Wraps any UrmaSockBuf and applies a logical offset shift: Allocate returns
 * `(base_offset + shift_bytes) % capacity_`, and Release reverse-shifts back.
 * The underlying buffer capacity and space accounting are unchanged —
 * Available, Used, and Capacity delegate directly to the base allocator.
 *
 * Primary use case: reserve a guard region at the "logical end" of a ring buffer
 * for URMA control messages, preventing the allocator from returning offsets
 * near the capacity boundary where a small allocation would immediately wrap.
 *
 * - **Coordinate transform**: `caller_offset = (base_offset + shift_bytes) % capacity_`
 * - **Reverse transform**: `base_offset = (caller_offset + capacity_ - shift_bytes) % capacity_`
 * - **Ownership**: takes ownership of base, deletes in destructor
 * - **Shift normalization**: constructor applies `shift % capacity_` to keep shift in [0, capacity_)
 */
class UrmaSockShiftBuf : public UrmaSockBuf {
public:
    /**
     * @brief Construct with a base allocator and offset shift
     * @param base  Underlying allocator (ownership transferred)
     * @param shift Byte offset to add to all returned offsets (normalized mod capacity_)
     */
    explicit UrmaSockShiftBuf(UrmaSockBuf *base, uint64_t shift) noexcept
        : base_{base},
          shift_bytes{shift % base_->Capacity()},
          capacity_{base->Capacity()}
    {
    }
    ~UrmaSockShiftBuf() override
    {
        delete base_;
        base_ = nullptr;
    }
    bool Allocate(uint64_t size, uint64_t &offset) noexcept override;
    uint64_t Allocate(uint64_t minSize, uint64_t maxSize, uint64_t &offset) noexcept override;
    void Release(uint64_t offset, uint64_t size) noexcept override;
    uint64_t Available() const noexcept override;
    uint64_t Used() const noexcept override;
    uint64_t Capacity() const noexcept override
    {
        return capacity_;
    }

private:
    UrmaSockBuf *base_;
    const uint64_t shift_bytes; ///< Normalized offset shift applied to Allocate/Release
    const uint64_t capacity_;   ///< Cached from base for inline Capacity()
};

/**
 * @brief Contiguous-allocation decorator for URMA socket buffer allocators
 *
 * Guarantees that every returned allocation is physically contiguous — the caller
 * always sees `offset + size <= capacity_`, even when the base allocator (e.g. a ring buffer)
 * returns an allocation that wraps around the buffer boundary.
 *
 * ## How it handles wrapping
 *
 * When the base allocator produces a wrapped allocation spanning `[base_offset, capacity_)`
 * (end_size) and `[0, wrap_size)` (wrap_size), the decorator reshapes it:
 *
 *    1. Release the end portion `[base_offset, capacity_)` — it's no longer needed
 *    2. Allocate `end_size` additional bytes to extend the wrap portion into a full block
 *    3. Verify the new allocation is contiguous: `append_offset == wrap_size`
 *    4. Return `offset = 0, size = wrap_size + end_size` — a single contiguous region
 *
 * ## Best-effort min/max semantics
 *
 * When the base returns less than max_size and wrapping occurs, the decorator releases
 * the end portion and returns only the wrap_size bytes (if >= min_size) rather than
 * performing a secondary allocation. This avoids fragmenting the buffer with an
 * expensive re-allocation when the caller accepts a smaller result.
 *
 * - **Ownership**: takes ownership of base, deletes in destructor
 * - **Release**: transparent pass-through — contiguous allocations never wrap on release
 * - **Capacity/Available/Used**: delegate directly to base
 */
class UrmaSockContinuousBuf : public UrmaSockBuf {
public:
    /**
     * @brief Construct with a base allocator
     * @param base Underlying allocator (ownership transferred)
     */
    explicit UrmaSockContinuousBuf(UrmaSockBuf *base, uintptr_t addr) noexcept
        : base_{static_cast<UrmaSockRingBuf *>(base)},
          capacity_{base->Capacity()}, start_addr_{addr}
    {
        pthread_spin_init(&lock_, 0);
    }
    ~UrmaSockContinuousBuf() override
    {
        delete base_;
        base_ = nullptr;
        pthread_spin_destroy(&lock_);
    }

    bool Allocate(uint64_t size, uint64_t &offset) noexcept override;
    uint64_t Allocate(uint64_t min_size, uint64_t max_size, uint64_t &offset) noexcept override;
    void Release(uint64_t offset, uint64_t size) noexcept override;
    uint64_t Available() const noexcept override;
    uint64_t Used() const noexcept override;
    uint64_t Capacity() const noexcept override
    {
        return capacity_;
    }

private:
    struct LaterReleaseNode {
        LaterReleaseNode *next;
        uint64_t offset;
        uint64_t size;

        LaterReleaseNode() noexcept: next{nullptr}, offset{0}, size{0} {}
        LaterReleaseNode(uint64_t o, uint64_t s) noexcept : next{nullptr}, offset{o}, size{s} {}
    };

    void AddLaterReleaseNode(uint64_t offset, uint64_t size) noexcept;
    void AddLaterReleaseNode(uint64_t offset1, uint64_t size1, uint64_t offset2, uint64_t size2) noexcept;
    void ReleaseAllLaterNodes() noexcept;

    UrmaSockRingBuf *base_;
    const uint64_t capacity_; ///< Cached from base for inline Capacity()
    const uintptr_t start_addr_;
    pthread_spinlock_t lock_{};
    LaterReleaseNode *later_release_head_{nullptr};
    std::atomic<uint64_t> later_release_node_count{0};
    std::atomic<uint64_t> wrap_end_offset_{0};
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SOCKET_BUF_H
