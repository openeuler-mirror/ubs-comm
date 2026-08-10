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

#ifndef UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_TC2_H
#define UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_TC2_H

#include "ubsocket_alloc_fixed_size_gc.h"
#include "ubsocket_alloc_fixed_size_types.h"

namespace ock {
namespace ubs {
namespace allocator {
class FixedThreadCache2 {
public:
    FixedThreadCache2(FixedGlobalCache *pool, uint16_t gcAllocBatchSize, uint16_t gcFreeBatchSize)
        : gc_alloc_batch_size_(gcAllocBatchSize),
          gc_free_batch_size_(gcFreeBatchSize),
          gc_(pool)
    {
    }

    ~FixedThreadCache2()
    {
        UnInit();
    }

    void UnInit() noexcept;

    /**
     * @brief Allocate one buffer from thread cache, if there are no enough buffer, allocate from global cache
     *
     * @param address      [out] allocated buffer address
     * @return true if allocated
     */
    bool Allocate(uintptr_t &address) noexcept;

    /**
     * @brief Free one buffer to thread cache, if there are enough buffer in thread cache then free to global cache
     *
     * @param address      [in] the address of buffer to be freed
     */
    void Free(uintptr_t address) noexcept;

    /**
     * @brief Allocated a batch of buffer from thread cache, if there are no enough buffer, allocate from global cache
     *
     * @param batchSize    [in] count of buffer to be allocated
     * @param address      [out] address of first buffer
     * @return true
     */
    bool BatchAllocate(uint16_t batchSize, uintptr_t *&address) noexcept;

    /**
     * @brief Allocated a batch of buffer from thread cache, if there are no enough buffer, allocate from global cache
     *
     * @param batchSize    [in] count of buffer to be allocated
     * @param address      [out] head of linked list
     * @return true
     */
    bool BatchAllocate(uint16_t batchSize, FixedMiniBlock *&block) noexcept;

private:
    FixedMiniBlock *AllocateFromGC(uint16_t requestSize, uint16_t &allocatedCount) noexcept;
    void FreeAllToGC() noexcept;
    void FreeToGC() noexcept;

    void TryFreeToNMAligned() noexcept;

    /* batch allocate function for linked list */
    bool BatchAllocFromUnAligned(uint16_t &remaining_count, FixedMiniBlock *&head, FixedMiniBlock *&tail) noexcept;
    bool BatchAllocFromNMAligned(uint16_t &remaining_count, FixedMiniBlock *&head, FixedMiniBlock *&tail) noexcept;

    static uint16_t AlignDown(uint16_t size) noexcept;

private:
    /* make the size of variables equal to 64 bytes, which is the cache line of CPU */
    uint16_t gc_alloc_batch_size_ = 16;          /* how many mini blocks allocated from global cache at one time */
    uint16_t gc_free_batch_size_ = 16;           /* how many mini blocks allocated from global cache at one time */
    uint16_t unaligned_free_count_ = 0;          /* how many mini blocks in aligned list */
    uint16_t n_m_aligned_free_count_ = 0;        /* how many mini blocks n*m aligned list */
    FixedMiniBlock *unaligned_head_ = nullptr;   /* head of unaligned blocks */
    FixedMiniBlock *unaligned_tail_ = nullptr;   /* tail of unaligned blocks */
    FixedMiniBlock *n_m_aligned_head_ = nullptr; /* aligned blocks with n*m */
    FixedMiniBlock *n_m_aligned_tail_ = nullptr; /* tail of aligned blocks with n*m */
    FixedGlobalCache *gc_ = nullptr;             /* point to global cache */
    uint64_t reserved[2]{};                      /* reserved */
};

ALWAYS_INLINE uint16_t FixedThreadCache2::AlignDown(uint16_t size) noexcept
{
    constexpr uint16_t mask = ~(FIX_GC_BATCH_FREE_SIZE - 1);
    return size & mask;
}

ALWAYS_INLINE void FixedThreadCache2::UnInit() noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        return;
    }

    FreeAllToGC();
    gc_ = nullptr;
}

ALWAYS_INLINE FixedMiniBlock *FixedThreadCache2::AllocateFromGC(uint16_t requestSize, uint16_t &allocatedCount) noexcept
{
    FixedMiniBlock *tmp = nullptr;
    allocatedCount = gc_->BatchAllocate(requestSize, false, tmp);
    return tmp;
}

ALWAYS_INLINE void FixedThreadCache2::FreeAllToGC() noexcept
{
    if (UNLIKELY(gc_ == nullptr)) {
        return;
    }

    /* step1: free n*m aligned */
    if (n_m_aligned_head_ != nullptr) {
        gc_->BatchFree(n_m_aligned_head_, n_m_aligned_free_count_);
        n_m_aligned_head_ = nullptr;
        n_m_aligned_tail_ = nullptr;
        n_m_aligned_free_count_ = 0;
    }

    /* step2: free unaligned */
    if (unaligned_head_ != nullptr) {
        gc_->BatchFree(unaligned_head_, unaligned_free_count_);
        unaligned_head_ = nullptr;
        unaligned_tail_ = nullptr;
        unaligned_free_count_ = 0;
    }
}

ALWAYS_INLINE bool FixedThreadCache2::Allocate(uintptr_t &address) noexcept
{
    /* step1: allocated from un-aligned */
    if (unaligned_head_ != nullptr) {
        address = reinterpret_cast<uintptr_t>(unaligned_head_);
        unaligned_head_ = unaligned_head_->next;
        --unaligned_free_count_;
        unaligned_tail_ = (unaligned_free_count_ == 0) ? nullptr : unaligned_tail_;
        return true;
    }

    /* step2: try to allocate from n_m_aligned list */
    if (n_m_aligned_head_ != nullptr) {
        address = reinterpret_cast<uintptr_t>(n_m_aligned_head_);
        /* adjust unaligned */
        unaligned_head_ = n_m_aligned_head_->next;
        unaligned_tail_ = n_m_aligned_head_->next_n_m;
        unaligned_free_count_ += FIX_GC_BATCH_FREE_SIZE - 1;

        /* adjust n_m_aligned */
        n_m_aligned_head_ = unaligned_tail_->next;
        n_m_aligned_free_count_ -= FIX_GC_BATCH_FREE_SIZE;
        n_m_aligned_tail_ = (n_m_aligned_free_count_ == 0) ? nullptr : n_m_aligned_tail_;

        /* set tail to nullptr, for unaligned */
        unaligned_tail_->next = nullptr;

        return true;
    }

    /* step3: allocate from global cache */
    uint16_t allocated_count = 0;
    auto mini_block_from_gc = AllocateFromGC(FIX_GC_BATCH_FREE_SIZE, allocated_count);
    if (mini_block_from_gc == nullptr || allocated_count < FIX_GC_BATCH_FREE_SIZE) {
        UBS_VLOG_ERR("Failed to allocate one batch with batch size(%d) from global cache, "
                     "ret allocated_count is %d", FIX_GC_BATCH_FREE_SIZE, allocated_count);
        if (mini_block_from_gc != nullptr) {
            gc_->BatchFree(mini_block_from_gc, allocated_count);
        }
        return false;
    }

    /* step4: try to allocate from new allocated list, put the left to unaligned list */
    address = reinterpret_cast<uintptr_t>(mini_block_from_gc);
    /* adjust unaligned */
    unaligned_head_ = mini_block_from_gc->next;
    unaligned_tail_ = mini_block_from_gc->next_n_m;
    unaligned_free_count_ += FIX_GC_BATCH_FREE_SIZE - 1;

    /* set the end to nullptr, for unaligned */
    unaligned_tail_->next = nullptr;

    return true;
}

ALWAYS_INLINE void FixedThreadCache2::Free(uintptr_t address) noexcept
{
    if (UNLIKELY(address == 0)) {
        return;
    }

    /* step1: add to unaligned list */
    FixedMiniBlock *new_mini_block = reinterpret_cast<FixedMiniBlock *>(address);
    new_mini_block->next = unaligned_head_;
    unaligned_head_ = new_mini_block;
    unaligned_tail_ = (unaligned_free_count_ == 0) ? new_mini_block : unaligned_tail_;
    ++unaligned_free_count_;

    /* step2: try to free to n_aligned */
    TryFreeToNMAligned();
}

ALWAYS_INLINE void FixedThreadCache2::TryFreeToNMAligned() noexcept
{
    if (unaligned_free_count_ == FIX_GC_BATCH_FREE_SIZE) {
        /* set next_n_m of n_aligned head to tail */
        unaligned_head_->next_n_m = unaligned_tail_;
        unaligned_tail_->next = n_m_aligned_head_;

        /* adjust n_m_aligned list */
        n_m_aligned_head_ = unaligned_head_;
        n_m_aligned_tail_ = (n_m_aligned_free_count_ == 0) ? unaligned_tail_ : n_m_aligned_tail_;
        n_m_aligned_free_count_ += FIX_GC_BATCH_FREE_SIZE;

        /* adjust unaligned_list */
        unaligned_head_ = nullptr;
        unaligned_tail_ = nullptr;
        unaligned_free_count_ = 0;
    }

    /* try to free to global cache */
    FreeToGC();
}

ALWAYS_INLINE void FixedThreadCache2::FreeToGC() noexcept
{
    if (n_m_aligned_free_count_ <= FIX_GC_BATCH_FREE_THRESHOLD || n_m_aligned_head_ == nullptr) {
        return;
    }

    if (UNLIKELY(gc_ == nullptr)) {
        UBS_VLOG_ERR("Un-reachable global cache(%x) is null", gc_);
        return;
    }

    /* free n*m to global cache */
    auto tmp_n_m_aligned = n_m_aligned_head_;

    /* adjust n_m_aligned list */
    n_m_aligned_head_ = n_m_aligned_head_->next_n_m->next;
    n_m_aligned_free_count_ -= FIX_GC_BATCH_FREE_SIZE;
    n_m_aligned_tail_ = (n_m_aligned_free_count_ == 0) ? nullptr : n_m_aligned_tail_;

    /* free to global cache */
    tmp_n_m_aligned->next_n_m->next = nullptr;
    gc_->BatchFree(tmp_n_m_aligned, FIX_GC_BATCH_FREE_SIZE);
}

ALWAYS_INLINE bool FixedThreadCache2::BatchAllocate(uint16_t batchSize, uintptr_t *&address) noexcept
{
    if (UNLIKELY(batchSize == 0 || address == nullptr)) {
        return false;
    }

    /* step1: call InnerBatchAllocate to get the chain head */
    FixedMiniBlock *head_block = 0;
    if (UNLIKELY(!BatchAllocate(batchSize, head_block))) {
        return false;
    }

    /* step2: fill the externally provided array by walking the chain */
    for (uint16_t i = 0; i < batchSize; ++i) {
        address[i] = reinterpret_cast<uintptr_t>(head_block);
        head_block = head_block->next;
    }
    return true;
}

ALWAYS_INLINE bool FixedThreadCache2::BatchAllocate(uint16_t batchSize, FixedMiniBlock *&block) noexcept
{
    FixedMiniBlock *tail = nullptr;

    /* step1: allocate from unaligned list */
    uint16_t aligned_count = AlignDown(batchSize);
    uint16_t un_aligned_count = batchSize - aligned_count;
    if (BatchAllocFromUnAligned(un_aligned_count, block, tail) && aligned_count == 0) {
        return true;
    }

    /* step2: allocate from global cache, i.e. nothing in unaligned and n_m_aligned */
    uint16_t remaining_count = aligned_count + un_aligned_count;
    if (remaining_count > n_m_aligned_free_count_) {
        uint16_t allocated_count = 0;
        auto tmp_block_from_gc = AllocateFromGC(remaining_count - n_m_aligned_free_count_, allocated_count);
        if (tmp_block_from_gc == nullptr || allocated_count < remaining_count - n_m_aligned_free_count_) {
            UBS_VLOG_ERR("Failed to allocate batches with batch size(%d) from global cache, "
                         "ret allocated_count is %d", FIX_GC_BATCH_FREE_SIZE, allocated_count);
            if (tmp_block_from_gc != nullptr) {
                gc_->BatchFree(tmp_block_from_gc, allocated_count);
            }
            return false;
        }
        tmp_block_from_gc->tail->next = n_m_aligned_head_;
        n_m_aligned_head_ = tmp_block_from_gc;
        n_m_aligned_tail_ = (n_m_aligned_free_count_ == 0) ? tmp_block_from_gc->tail : n_m_aligned_tail_;
        n_m_aligned_free_count_ += allocated_count;
    }

    /* step3: allocate from n_m_aligned list */
    return BatchAllocFromNMAligned(remaining_count, block, tail);
}

ALWAYS_INLINE bool FixedThreadCache2::BatchAllocFromUnAligned(uint16_t &remaining_count, FixedMiniBlock *&head,
                                                              FixedMiniBlock *&tail) noexcept
{
    /* step0: check */
    if (remaining_count == 0) {
        return true;
    }
    if (unaligned_free_count_ == 0) {
        return false;
    }

    /* step1: allocate from unaligned list */
    const uint16_t tmp_alloc_count = std::min(unaligned_free_count_, remaining_count);
    const uint16_t loop_count = tmp_alloc_count - 1;
    FixedMiniBlock *iter = unaligned_head_;
    for (uint16_t i = 0; i < loop_count; i++) {
        iter = iter->next;
    }

    /* step2: update head and tail */
    if (head == nullptr) {
        head = unaligned_head_;
    } else {
        tail->next = unaligned_head_;
    }
    tail = iter;

    /* step3: adjust unaligned info */
    unaligned_head_ = iter->next;
    unaligned_free_count_ -= tmp_alloc_count;
    unaligned_tail_ = (unaligned_free_count_ == 0) ? nullptr : unaligned_tail_;
    tail->next = nullptr;

    /* step4: update remaining */
    remaining_count -= tmp_alloc_count;

    /* return true if satisfied */
    return remaining_count == 0;
}

ALWAYS_INLINE bool FixedThreadCache2::BatchAllocFromNMAligned(uint16_t &remaining_count, FixedMiniBlock *&head,
                                                              FixedMiniBlock *&tail) noexcept
{
    /* step0: check */
    if (n_m_aligned_free_count_ == 0) {
        return false;
    }

    /* step1: align down with FIX_GC_BATCH_FREE_SIZE and get */
    const uint16_t align_down_count = AlignDown(remaining_count);

    /* step2: take from n_m_aligned */
    if (align_down_count >= n_m_aligned_free_count_) {
        /* case1: not enough, take all */
        if (head == nullptr) {
            head = n_m_aligned_head_;
        } else {
            tail->next = n_m_aligned_head_;
        }
        tail = n_m_aligned_tail_;

        /* update n_m_aligned info */
        n_m_aligned_head_ = nullptr;
        n_m_aligned_tail_ = nullptr;
        remaining_count -= n_m_aligned_free_count_;
        n_m_aligned_free_count_ = 0;
    } else if (align_down_count > 0) {
        /* case2: take some from n_m_aligned */
        const uint16_t tmp_alloc_count = std::min(align_down_count, n_m_aligned_free_count_);
        const uint16_t loop_count = tmp_alloc_count / FIX_GC_BATCH_FREE_SIZE - 1;

        FixedMiniBlock *iter = n_m_aligned_head_;
        for (uint16_t i = 0; i < loop_count; i++) {
            iter = iter->next_n_m->next;
        }

        if (head == nullptr) {
            head = n_m_aligned_head_;
        } else {
            tail->next = n_m_aligned_head_;
        }
        tail = iter->next_n_m;

        /* adjust n_m_aligned info */
        n_m_aligned_head_ = iter->next_n_m->next;
        n_m_aligned_free_count_ -= tmp_alloc_count;
        n_m_aligned_tail_ = (n_m_aligned_free_count_ == 0) ? nullptr : n_m_aligned_tail_;

        remaining_count -= tmp_alloc_count;
    }

    /* step4: update remaining and check if satisfied */
    if (remaining_count == 0) {
        tail->next = nullptr;
        return true;
    }

    /* step5: checked if there are blocks n_m_list */
    if (n_m_aligned_head_ == nullptr) {
        return false;
    }

    /* step6: take and split one n_m_aligned chain */
    unaligned_tail_ = n_m_aligned_head_->next_n_m;
    FixedMiniBlock *iter = n_m_aligned_head_;
    const uint16_t loop_count = remaining_count - 1;
    for (uint16_t i = 0; i < loop_count; i++) {
        iter = iter->next;
    }

    if (head == nullptr) {
        head = n_m_aligned_head_;
    } else {
        tail->next = n_m_aligned_head_;
    }
    tail = iter;

    /* adjust unaligned */
    /* move the left to unaligned list, since we already allocated all unaligned, unaligned_head_ should be null */
    unaligned_head_ = iter->next;
    unaligned_free_count_ = FIX_GC_BATCH_FREE_SIZE - remaining_count;

    /* adjust n_m_aligned */
    n_m_aligned_head_ = unaligned_tail_->next;
    n_m_aligned_free_count_ -= FIX_GC_BATCH_FREE_SIZE;
    n_m_aligned_tail_ = (n_m_aligned_free_count_ == 0) ? nullptr : n_m_aligned_tail_;
    tail->next = nullptr;
    return true;
}

} // namespace allocator
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_UBSOCKET_ALLOC_FIXED_SIZE_TC2_H
