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

#ifndef UBSOCKET_ALLOC_FIXED_SIZE_TC_H
#define UBSOCKET_ALLOC_FIXED_SIZE_TC_H

#include "ubsocket_alloc_fixed_size_gc.h"
#include "ubsocket_alloc_fixed_size_types.h"

namespace ock {
namespace ubs {
namespace allocator {

class FixedThreadCache {
public:
    FixedThreadCache(FixedGlobalCache *pool, uint16_t gcAllocBatchSize, uint16_t gcFreeBatchSize)
        : gc_alloc_batch_size_(gcAllocBatchSize),
          gc_free_batch_size_(gcFreeBatchSize),
          gc_(pool)
    {
    }

    ~FixedThreadCache()
    {
        UnInit();
    }

    void UnInit()
    {
        if (gc_ == nullptr) {
            return;
        }

        FreeAllToGC();
        gc_ = nullptr;
    }

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
     * @param addresses    [out] addresses of buffers
     * @return true
     */
    bool BatchAllocate(uint16_t batchSize, uintptr_t *&addresses) noexcept;

    /**
     * @brief Free buffers to thread cache in batch
     *
     * @param batchSize    [in] count of buffer to be allocated
     * @param addresses    [in] addresses of buffers
     */
    void BatchFree(uint16_t batchSize, uintptr_t *addresses) noexcept;

private:
    bool AllocateFromGC() noexcept;
    void FreeToGC() noexcept;
    void FreeAllToGC() noexcept;

private:
    /* make the size of variables equal to 64 bytes, which is the cache line of CPU */
    uint16_t gc_alloc_batch_size_ = 16;  /* how many mini blocks allocated from global cache */
    uint16_t gc_free_batch_size_ = 16;   /* how many mini blocks freed to global cache */
    uint32_t count_ = 0;                 /* how many mini blocks current thread owned */
    FixedMiniBlock *next_ = nullptr;     /* next free mini block */
    FixedMiniBlock *next_n_ = nullptr;   /* end of first mini block chain (1~n) */
    FixedMiniBlock *next_n_m_ = nullptr; /* end of first mini block chain (1~n*m) */
    uint16_t next_n_count_ = 0;          /* count to next_n */
    uint16_t next_n_m_count_ = 0;        /* count to next_n_m */
    FixedGlobalCache *gc_ = nullptr;     /* point to global cache */
    uint64_t reserved_[2]{0};            /* reserved to make sure the size is 64 bytes */
};

ALWAYS_INLINE bool FixedThreadCache::Allocate(uintptr_t &address) noexcept
{
    /* note: consider performance in this function, we should only access two cache line:
     * 1. this object
     * 2. the address param
     * otherwise, we will have high possibility that cache missing in L3 */

    /* step 1: try to take the first one */
    if (UNLIKELY(count_ != 0 && next_ != nullptr)) {
        /* assign to address with head_ */
        auto out = next_;
        address = reinterpret_cast<uintptr_t>(out);

        --count_;
        /* move to next and decrease count */
        next_ = (count_ == 0) ? nullptr : out->next;

        /* assign next n info */
        if (next_n_count_ == 0) {
            next_n_count_ = FIX_TC_MINI_BLOCK_LINKED_COUNT - 1;
        } else if (--next_n_count_ == 0) {
            next_n_ = (next_ == nullptr) ? nullptr : next_->next_n;
            next_n_count_ = (next_ == nullptr) ? 0 : next_->next_n_count;
        }

        /* assign next n*m info */
        if (next_n_m_count_ == 0) {
            next_n_m_count_ = FIX_GC_BATCH_ALLOC_SIZE - 1;
        } else if (--next_n_m_count_ == 0) {
            next_n_m_ = (next_ == nullptr) ? nullptr : next_->next_n_m;
            next_n_m_count_ = (next_ == nullptr) ? 0 : next_->next_n_m_count;
        }

        return true;
    }

    /* step2: allocate from global cache */
    if (UNLIKELY(!AllocateFromGC())) {
        return false;
    }

    /* step3: take one from head again */
    auto out = next_;
    address = reinterpret_cast<uintptr_t>(out);

    /* move to next and decrease count */
    next_ = out->next;
    --count_;

    /* assign next n info */
    if (next_n_count_ == 0) {
        next_n_count_ = FIX_TC_MINI_BLOCK_LINKED_COUNT - 1;
    } else if (--next_n_count_ == 0) {
        next_n_ = (next_ == nullptr) ? nullptr : next_->next_n;
        next_n_count_ = (next_ == nullptr) ? 0 : next_->next_n_count;
    }

    /* assign next n*m info */
    if (next_n_m_count_ == 0) {
        next_n_m_count_ = FIX_GC_BATCH_ALLOC_SIZE - 1;
    } else if (--next_n_m_count_ == 0) {
        next_n_m_ = (next_ == nullptr) ? nullptr : next_->next_n_m;
        next_n_m_count_ = (next_ == nullptr) ? 0 : next_->next_n_m_count;
    }

    return true;
}

ALWAYS_INLINE void FixedThreadCache::Free(uintptr_t address) noexcept
{
    if (UNLIKELY(address == 0)) {
        return;
    }

    /* step1: insert to head */
    auto tmp = reinterpret_cast<FixedMiniBlock *>(address);
    tmp->next = next_;
    tmp->next_n = nullptr;
    tmp->next_n_count = 0;
    next_ = tmp;
    tmp->next_n_m = nullptr;
    tmp->next_n_m_count = 0;
    ++count_;

    /* step2: set next n related */
    if (++next_n_count_ == FIX_TC_MINI_BLOCK_LINKED_COUNT) {
        /* case1: if there are multiple times of FIX_TC_MINI_BLOCK_LINKED_COUNT */
        tmp->next_n = next_n_;
    } else if (next_n_count_ == 1) {
        next_n_ = tmp;
    }
    next_n_count_ = next_n_count_ % FIX_TC_MINI_BLOCK_LINKED_COUNT;

    /* step3: set next n*m related */
    if (++next_n_m_count_ == gc_free_batch_size_) {
        tmp->next_n_m = next_n_m_;
    } else if (next_n_m_count_ == 1) {
        next_n_m_ = tmp;
    }
    next_n_m_count_ = next_n_m_count_ % gc_free_batch_size_;

    /* step4: check if need to free to global cache */
    if (count_ == FIX_GC_BATCH_FREE_THRESHOLD) {
        FreeToGC();
    }
}

ALWAYS_INLINE bool FixedThreadCache::BatchAllocate(uint16_t batchSize, uintptr_t *&addresses) noexcept
{
    return false;
}

ALWAYS_INLINE void FixedThreadCache::BatchFree(uint16_t batchSize, uintptr_t *addresses) noexcept {}

ALWAYS_INLINE bool FixedThreadCache::AllocateFromGC() noexcept
{
    /* step1: allocate from gc_ */
    /* here gc_alloc_batch_size_ must be multiple times of FIX_GC_BATCH_FREE_SIZE */
    FixedMiniBlock *block_list = nullptr;
    auto allocated = gc_->BatchAllocate(gc_alloc_batch_size_, false, block_list);
    if (UNLIKELY(allocated == 0)) {
        return false;
    }

    /* step2: add after next_n_m,
     * here we only access the next_n_m mini block and member variable of this class
     * to minimize the cache miss of CPU */

    count_ += gc_alloc_batch_size_;
    /* case1: there is no mini block in thread cache */
    if (next_ == nullptr) {
        next_ = block_list;
        next_n_ = block_list->next_n;
        next_n_count_ = block_list->next_n_count;
        next_n_m_ = block_list->next_n_m;
        next_n_m_count_ = block_list->next_n_m_count;
        return true;
    }

    /* case2: there are some mini blocks in thread cache */
    auto tmp = next_n_m_->next;
    next_n_m_->next = block_list;
    if (tmp != nullptr) {
        block_list->next_n_m->next = tmp;
    }

    return true;
}

ALWAYS_INLINE void FixedThreadCache::FreeToGC() noexcept
{
    /* step1: gather batch */
    auto first = next_n_m_->next;
    next_n_m_->next = nullptr;
    count_ -= gc_free_batch_size_;

    /* step2: free to gc_ */
    gc_->BatchFree(first, gc_free_batch_size_);
}

ALWAYS_INLINE void FixedThreadCache::FreeAllToGC() noexcept
{
    if (next_ == nullptr) {
        return;
    }
    /* step1: reset some things */
    auto first = next_;
    next_ = nullptr;
    next_n_ = nullptr;
    next_n_count_ = 0;
    next_n_m_ = nullptr;
    next_n_m_count_ = 0;

    /* step2: free to global cache */
    gc_->BatchFree(first, count_);

    /* step3: set count to 0 */
    count_ = 0;
}
} // namespace allocator
} // namespace ubs
} // namespace ock

#endif //UBSOCKET_ALLOC_FIXED_SIZE_TC_H