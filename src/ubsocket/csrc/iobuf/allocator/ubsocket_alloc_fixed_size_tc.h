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
     * @brief Allocated a batch of buffer from thread cache, if there are no enough buffer, allocate from global cache
     *
     * @param batchSize    [in] count of buffer to be allocated
     * @param address    [out] address of first buffer
     * @return true
     */
    bool InnerBatchAllocate(uint16_t batchSize, uintptr_t &address) noexcept;

    /**
     * @brief Free buffers to thread cache in batch
     *
     * @param batchSize    [in] count of buffer to be allocated
     * @param addresses    [in] addresses of buffers
     */
    void BatchFree(uint16_t batchSize, uintptr_t *addresses) noexcept;

    /**
     * @brief Free buffers to thread cache in batch
     *
     * @param batchSize    [in] count of buffer to be allocated
     * @param addresses    [in] address of first buffer
     */
    void InnerBatchFree(uint16_t batchSize, uintptr_t addresses) noexcept;

private:
    bool AllocateFromGC(uint16_t requestSize) noexcept;
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

    /* step2: allocate from global cache */
    if (UNLIKELY(!AllocateFromGC(gc_alloc_batch_size_))) {
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

ALWAYS_INLINE bool FixedThreadCache::InnerBatchAllocate(uint16_t batchSize, uintptr_t &address) noexcept
{
    /* step1: Check whether the blocks in TC are sufficient. If not, apply for the required number of batches from the GC. */
    uint32_t req_batch_num = batchSize / FIX_GC_BATCH_FREE_SIZE;
    uint32_t req_left_block_num = batchSize % FIX_GC_BATCH_FREE_SIZE;
    uint32_t cur_batch_num = count_ / FIX_GC_BATCH_FREE_SIZE;
    uint32_t cur_left_block_num = count_ % FIX_GC_BATCH_FREE_SIZE;
    if (req_batch_num > cur_batch_num ||
        (req_batch_num == cur_batch_num && req_left_block_num > cur_left_block_num)) {
        uint32_t expand_batch_num = req_batch_num - cur_batch_num;
        if (req_left_block_num > cur_left_block_num) {
            expand_batch_num += 1;
        }
        if (UNLIKELY(!AllocateFromGC(FIX_GC_BATCH_FREE_SIZE * expand_batch_num))) {
            return false;
        }
    }

    /* step2: Obtain the data from the complete batch. */
    auto out = next_;
    auto out_tail = next_n_m_;
    if (req_batch_num > 0) {
        for (uint32_t i = 0; i < req_batch_num; ++i) {
            if (i == 0) {
                if (cur_left_block_num != 0) {
                    out = out_tail->next;
                    out_tail = out->next_n_m;
                }
                continue;
            }
            out_tail = out_tail->next->next_n_m;
        }
        if (cur_left_block_num == 0) {
            next_ = out_tail->next;
            next_n_ = (next_ == nullptr) ? nullptr : next_->next_n;
            next_n_m_ = (next_ == nullptr) ? nullptr : next_->next_n_m;
            next_n_count_ = (next_ == nullptr) ? 0 : next_->next_n_count;
            next_n_m_count_ = (next_ == nullptr) ? 0 : next_->next_n_m_count;
        } else {
            next_n_m_->next = out_tail->next;
        }
        out_tail->next = nullptr;
        count_ -= req_batch_num * FIX_GC_BATCH_FREE_SIZE;
    }

    /* step3: Remain data is supplemented from next_ and is quickly searched by using next_n_count_ and next_n_m_count_. */
    if (req_left_block_num > 0) {
        out_tail->next = next_;
        uint32_t req_count = req_left_block_num;
        while (out_tail->next != nullptr && req_count > 0) {
            if (next_n_count_ % FIX_TC_MINI_BLOCK_LINKED_COUNT == 0 && req_count >= FIX_TC_MINI_BLOCK_LINKED_COUNT) {
                out_tail = next_->next_n;
                next_ = out_tail->next;
                next_n_ = next_->next_n;
                if (next_n_m_count_ % FIX_GC_BATCH_FREE_SIZE == 0) {
                    next_n_m_ = next_->next_n_m;
                    next_n_m_count_ = FIX_GC_BATCH_FREE_SIZE - FIX_TC_MINI_BLOCK_LINKED_COUNT;
                } else {
                    next_n_m_count_ -= FIX_TC_MINI_BLOCK_LINKED_COUNT;
                }
                req_count -= FIX_TC_MINI_BLOCK_LINKED_COUNT;
            } else {
                out_tail = next_;
                next_ = out_tail->next;
                next_n_count_--;
                next_n_m_count_--;
                req_count--;
            }
        }
        if (req_count != 0) {
            UBS_VLOG_ERR("BatchAllocate error, which should not happen, req_count: %lu, cur_count: %lu.\n", req_left_block_num, req_left_block_num - req_count);
        }
        out_tail->next = nullptr;
        count_ -= (req_left_block_num - req_count);
    }
    address = reinterpret_cast<uintptr_t>(out);
    return true;
}

ALWAYS_INLINE bool FixedThreadCache::BatchAllocate(uint16_t batchSize, uintptr_t *&addresses) noexcept
{
    if (UNLIKELY(batchSize == 0 || addresses == nullptr)) {
        return false;
    }

    /* step1: call InnerBatchAllocate to get the chain head */
    uintptr_t head_addr = 0;
    if (UNLIKELY(!InnerBatchAllocate(batchSize, head_addr))) {
        return false;
    }

    /* step2: fill the externally provided array by walking the chain */
    auto iter = reinterpret_cast<FixedMiniBlock *>(head_addr);
    for (uint16_t i = 0; i < batchSize; ++i) {
        addresses[i] = reinterpret_cast<uintptr_t>(iter);
        iter = iter->next;
    }
    return true;
}

ALWAYS_INLINE void FixedThreadCache::InnerBatchFree(uint16_t batchSize, uintptr_t address) noexcept
{
    auto block_list = reinterpret_cast<FixedMiniBlock *>(address);

    /* step1: Check whether the current TC can expand a complete batch. */
    uint32_t free_batch_num = batchSize / FIX_GC_BATCH_FREE_SIZE;
    uint32_t free_left_block_num = batchSize % FIX_GC_BATCH_FREE_SIZE;
    uint32_t cur_batch_num = count_ / FIX_GC_BATCH_FREE_SIZE;
    uint32_t tc_max_batch_num = FIX_GC_BATCH_FREE_THRESHOLD / FIX_GC_BATCH_FREE_SIZE;
    uint32_t tc_expand_batch_num = (cur_batch_num < tc_max_batch_num) ? (tc_max_batch_num - cur_batch_num) : 0;
    uint32_t batch_to_tc = (free_batch_num < tc_expand_batch_num) ? free_batch_num : tc_expand_batch_num;

    /* step2: Preferentially place the complete batch to the current TC. */
    uint32_t free_count = 0;
    for (int i = 0; i < batch_to_tc; ++i) {
        if (next_ == nullptr) {
            next_ = block_list;
            next_n_ = block_list->next_n;
            next_n_count_ = block_list->next_n_count;
            next_n_m_ = block_list->next_n_m;
            next_n_m_count_ = block_list->next_n_m_count;
            block_list = next_n_m_->next;
            next_n_m_->next = nullptr;
        } else {
            auto tmp = next_n_m_->next;
            next_n_m_->next = block_list;
            auto next_block_list = block_list->next_n_m->next;
            block_list->next_n_m->next = tmp;
            block_list = next_block_list;
        }
        free_count += FIX_GC_BATCH_FREE_SIZE;
    }
    count_ += free_count;

    /* step3: Complete batch are returned to the GC. */
    uint32_t batch_free_count = 0;
    auto tmp_head = block_list;
    while ((batchSize - free_count) > FIX_GC_BATCH_FREE_SIZE) {
        tmp_head = tmp_head->next_n_m->next;
        batch_free_count += FIX_GC_BATCH_FREE_SIZE;
        free_count += FIX_GC_BATCH_FREE_SIZE;
    }
    gc_->BatchFree(block_list, batch_free_count);

    /* step4: Remain blocks are returned to the GC. */
    block_list = tmp_head;
    gc_->BatchFree(block_list, batchSize - free_count);
}

ALWAYS_INLINE void FixedThreadCache::BatchFree(uint16_t batchSize, uintptr_t *addresses) noexcept
{
    if (UNLIKELY(batchSize == 0 || addresses == nullptr)) {
        return;
    }

    /* step1: build chain from addresses array */
    FixedMiniBlock *head = nullptr;
    FixedMiniBlock *local_next_n = nullptr;
    uint16_t local_next_n_count = 0;
    FixedMiniBlock *local_next_n_m = nullptr;
    uint16_t local_next_n_m_count = 0;

    for (uint16_t i = 0; i < batchSize; ++i) {
        auto tmp = reinterpret_cast<FixedMiniBlock *>(addresses[i]);
        tmp->next = head;
        tmp->next_n = nullptr;
        tmp->next_n_count = 0;
        tmp->next_n_m = nullptr;
        tmp->next_n_m_count = 0;
        head = tmp;
        if (++local_next_n_count == FIX_TC_MINI_BLOCK_LINKED_COUNT) {
            tmp->next_n = local_next_n;
        } else if (local_next_n_count == 1) {
            local_next_n = tmp;
        }
        local_next_n_count = local_next_n_count % FIX_TC_MINI_BLOCK_LINKED_COUNT;
        if (++local_next_n_m_count == gc_free_batch_size_) {
            tmp->next_n_m = local_next_n_m;
        } else if (local_next_n_m_count == 1) {
            local_next_n_m = tmp;
        }
        local_next_n_m_count = local_next_n_m_count % gc_free_batch_size_;
    }

    if (local_next_n_m_count != 0) {
        auto remainder_head = head;
        auto remainder_tail = local_next_n_m;
        auto new_head = remainder_tail->next;
        if (new_head != nullptr) {
            auto tail = new_head->next_n_m;
            while (tail->next != nullptr) {
                tail = tail->next_n_m;
            }
            remainder_tail->next = nullptr;
            tail->next = remainder_head;
            head = new_head;
        }
    }

    /* step2: call InnerBatchFree with chain head (complete batches first) */
    InnerBatchFree(batchSize, reinterpret_cast<uintptr_t>(head));
}

ALWAYS_INLINE bool FixedThreadCache::AllocateFromGC(uint16_t requestSize) noexcept
{
    /* step1: allocate from gc_ */
    /* here gc_alloc_batch_size_ must be multiple times of FIX_GC_BATCH_FREE_SIZE */
    FixedMiniBlock *block_list = nullptr;
    auto allocated = gc_->BatchAllocate(requestSize, false, block_list);
    if (UNLIKELY(allocated == 0)) {
        return false;
    }

    /* step2: add after next_n_m,
     * here we only access the next_n_m mini block and member variable of this class
     * to minimize the cache miss of CPU */

    count_ += allocated;
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