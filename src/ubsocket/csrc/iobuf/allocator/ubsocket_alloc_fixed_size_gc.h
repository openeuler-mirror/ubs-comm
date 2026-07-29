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
#ifndef UBS_COMM_UBSOCKET_ALLOC_GLOBAL_CACHE_H
#define UBS_COMM_UBSOCKET_ALLOC_GLOBAL_CACHE_H

#include <condition_variable>

#include "ubsocket_alloc_fixed_size_ra.h"
#include "ubsocket_alloc_fixed_size_types.h"

namespace ock {
namespace ubs {
namespace allocator {
class FixedGlobalCache {
public:
    static Result VerifyOptions(FixedAllocatorOptions &options) noexcept;

public:
    FixedGlobalCache() = default;
    virtual ~FixedGlobalCache() = default;

    /**
     * @brief Initialize global cache
     * Reserved memory space, and allocate one stride
     *
     * @param options      [in] options
     * @return
     */
    Result Init(const FixedAllocatorOptions &options) noexcept;

    /**
     * @brief Un-initialize the global cache
     */
    void UnInit() noexcept;

    /**
     * @brief Allocate batch mini blocks until request size is satisfied
     *
     * @param requestSize    [in] how many mini blocks requested
     * @param partialSatisfy [in] partial satisfy is allowed
     * @param blocks         [out] allocated blocks
     *
     * @return Allocated mini block count
     */
    uint32_t BatchAllocate(uint16_t requestSize, bool partialSatisfy, FixedMiniBlock *&blocks);

    /**
     * @brief Free mini blocks to global cache in batch
     *
     * @param block        [in] head of linked mini blocks
     * @param freeSize     [in] count of mini blocks
     */
    void BatchFree(FixedMiniBlock *block, uint16_t freeSize) noexcept;

    /**
     * @brief Check if the address in range the global cache
     *
     * @param address      [in] address to be checked
     * @param size         [in] size of the mem
     * @return true if in range, false if not in range
     */
    bool InRange(uintptr_t address, uint64_t size) noexcept;

    /**
     * @brief Check if a batch of addresses in range or not
     *
     * @param batchSize    [in] batch of the addresses
     * @param addresses    [in] addresses
     * @return true if all addresses in range
     */
    bool BatchInRange(uint16_t batchSize, uintptr_t *addresses, uint64_t *size) noexcept;

    /**
     * @brief Allocate contiguous memory instead of mini blocks
     * @param size         [in] size of the memory to be allocated
     * @param address      [out] allocated
     *
     * @return true if allocated
     */
    bool DirectAllocate(uint32_t size, uintptr_t &address) noexcept;

    /**
     * @brief Return contiguous memory instead of mini blocks
     * @param address      [in] address of memory to be freed
     */
    void DirectFree(uintptr_t address) noexcept;

    /**
     * @brief Callback function of expand function,
     * called by range allocator, async expand thread etc
     *
     * @param flags        [in] optional flags
     * @param expandedBlock[out] expanded block
     *
     * @return 0 if successful
     */
    Result HandleExpandBlock(bool flags, FixedBlock &expandedBlock) noexcept;

private:
    void UnInitInner() noexcept;
    Result ReserveSpace() noexcept;
    Result ExpandBlock(bool forDirectAllocation) noexcept;
    void ShrinkAllBlocks() noexcept;
    Result MakeMiniBlocks(uint32_t &mini_blocks_created, FixedMiniBlock *&blocks) noexcept;

    uint32_t AlignUpToSingleBatchSize(uint32_t size) noexcept;
    void TidyUnaligned() noexcept;

    uint32_t BatchAllocateInner(uint16_t requestSize, FixedMiniBlock *&blocks) noexcept;
    uint32_t BatchAllocateInnerPartial(uint16_t requestSize, FixedMiniBlock *&blocks) noexcept;

private:
    /* hot used variables, make sure its size is 64 bytes */
    uintptr_t pool_space_start_address_ = 0;          /* actual reserved pool start virtual address */
    uintptr_t pool_space_end_address_ = 0;            /* pool space size desired and reserved */
    FixedMiniBlock *aligned_mini_blocks_ = nullptr;   /* head of mini blocks, all un-aligned with
                                                         FIX_TC_MINI_BLOCK_LINKED_COUNT and
                                                         FIX_GC_BATCH_FREE_SIZE */
    FixedMiniBlock *unaligned_mini_blocks_ = nullptr; /* head of un-aligned */
    uint32_t free_mini_blocks_aligned_ = 0;           /* how many mini blocks freed which is aligned */
    uint32_t free_mini_blocks_unaligned_ = 0;         /* how many mini blocks freed which is un-aligned */
    pthread_spinlock_t spinlock_aligned_;             /* allocate first lock spin lock from aligned */
    pthread_spinlock_t spinlock_unaligned_;           /* allocate first lock spin lock from unaligned */
    bool init_ = false;                               /* inited or not */
    uint64_t reserved[1]{0};                          /* reserved */

    /* not-hot used variables */
    std::mutex mutex_;                            /* mutex for init/un-init, direct allocate/free */
    FixedRangeAllocator direct_allocator_;        /* range based memory allocator for direct alloc and free */
    uintptr_t next_expand_start_address_ = 0;     /* start address for next expand */
    std::condition_variable sync_expand_cv_;      /* cv for sync expanding */
    bool sync_expanding_ = false;                 /* flag for sync expanding */
    std::vector<FixedBlock> direct_alloc_blocks_; /* all blocks for directly alloc */
    std::vector<FixedBlock> tc_alloc_blocks_;     /* blocks for thread cache */
    FixedAllocatorOptions options_;               /* options */
};

ALWAYS_INLINE uint32_t FixedGlobalCache::AlignUpToSingleBatchSize(uint32_t size) noexcept
{
    constexpr uint32_t ALIGN_MASK = ~(static_cast<uint32_t>(FIX_GC_BATCH_FREE_SIZE) - 1);
    return (size + FIX_GC_BATCH_FREE_SIZE - 1UL) & ALIGN_MASK;
}

ALWAYS_INLINE Result FixedGlobalCache::VerifyOptions(FixedAllocatorOptions &options) noexcept
{
    if (options.pool_space_start_tb >= 128L) {
        UBS_VLOG_INFO("Option 'pool_space_start_tb':%ud is invalid, set to default value 2",
                      options.pool_space_start_tb);
        options.pool_space_start_tb = 2;
    }

    if (options.pool_space_size_gb > 128L || options.pool_space_size_gb < 2L) {
        UBS_VLOG_INFO("Option 'pool_space_size_gb':%ud is invalid, set to default value 32",
                      options.pool_space_size_gb);
        options.pool_space_size_gb = 32;
    }

    if (options.block_size_mb > 128L || options.block_size_mb < 2L) {
        UBS_VLOG_INFO("Option 'block_size_mb':%ud is invalid, set to default value 8", options.block_size_mb);
        options.block_size_mb = 8;
    }

    if (options.mini_block_size_kb > 256L || options.mini_block_size_kb < 2L) {
        UBS_VLOG_INFO("Option 'mini_block_size_kb':%ud is invalid, set to default value 8 ",
                      options.mini_block_size_kb);
        options.mini_block_size_kb = 8;
    }

    auto init_mem_size = SIZE_1M * options.block_size_mb * options.init_block_count;
    if (init_mem_size > SIZE_1G * options.pool_space_size_gb) {
        UBS_VLOG_ERR(
            "Option 'init_block_count':%d is too large, result in total init cache size %ld exceed space size %dGB",
            options.init_block_count, init_mem_size, options.pool_space_size_gb);
        return UBS_ERROR;
    }

    if (options.expanded_callback == nullptr) {
        UBS_VLOG_ERR("Option 'expanded_callback' is nullptr'");
        return UBS_ERROR;
    }

    return UBS_OK;
}

ALWAYS_INLINE Result FixedGlobalCache::Init(const FixedAllocatorOptions &options) noexcept
{
    UBS_VLOG_INFO("enter");
    /* step1: check if init */
    std::lock_guard<std::mutex> lock(mutex_);
    if (init_) {
        UBS_VLOG_INFO("Already initialized");
        return UBS_OK;
    }

    /* step2: verify option */
    options_ = options;
    auto result = VerifyOptions(options_);
    if (result != UBS_OK) {
        return result;
    }

    /* step3: Reserve memory space */
    result = ReserveSpace();
    if (result != UBS_OK) {
        UnInitInner();
        return result;
    }

    /* step4: Expand mem */
    result = ExpandBlock(true);
    if (result != UBS_OK) {
        UnInitInner();
        return result;
    }

    for (uint16_t i = 0; i < options_.init_block_count; i++) {
        result = ExpandBlock(false);
        if (result != UBS_OK) {
            UnInitInner();
            return result;
        }
    }

    /* step5: make mini blocks */
    uint32_t mini_block_created_count = 0;
    FixedMiniBlock *mini_blocks_created = nullptr;
    result = MakeMiniBlocks(mini_block_created_count, mini_blocks_created);
    if (result != UBS_OK) {
        UnInitInner();
        return result;
    }

    /* init spin lock */
    pthread_spin_init(&spinlock_aligned_, PTHREAD_PROCESS_PRIVATE);
    pthread_spin_init(&spinlock_unaligned_, PTHREAD_PROCESS_PRIVATE);

    /* attach to aligned list */
    pthread_spin_lock(&spinlock_aligned_);
    aligned_mini_blocks_ = mini_blocks_created;
    free_mini_blocks_aligned_ += mini_block_created_count;
    pthread_spin_unlock(&spinlock_aligned_);

    /* step6: create range allocator */
    result = direct_allocator_.Init(
        direct_alloc_blocks_.at(0),
        std::bind(&FixedGlobalCache::HandleExpandBlock, this, std::placeholders::_1, std::placeholders::_2));
    if (result != UBS_OK) {
        UnInitInner();
        return result;
    }

    /* step7: create thread for dynamic expand for thread cache in background */

    init_ = true;
    UBS_VLOG_INFO("Initialized");
    return UBS_OK;
}

ALWAYS_INLINE void FixedGlobalCache::UnInit() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!init_) {
        UBS_VLOG_DEBUG("Uninitialized");
        return;
    }

    UnInitInner();
    init_ = false;
}

ALWAYS_INLINE Result FixedGlobalCache::ReserveSpace() noexcept
{
    /* try to serve memory space using mmap */
    const uint64_t space_size_bytes = SIZE_1G * options_.pool_space_size_gb;

    /* try to next 1TB to reserve*/
    for (uint64_t i = options_.pool_space_start_tb; i < 128; i++) {
        /* get start memory address */
        uintptr_t spase_start_bytes = SIZE_1T * options_.pool_space_start_tb + i * space_size_bytes;

        /* reserve space */
        auto result = mmap(reinterpret_cast<void *>(spase_start_bytes), space_size_bytes, PROT_READ | PROT_WRITE,
                           MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

        /* break if successfully reserved */
        if (result != MAP_FAILED) {
            pool_space_start_address_ = spase_start_bytes;
            break;
        }
    }

    if (pool_space_start_address_ == 0) {
        UBS_VLOG_ERR("Failed to reserve pool space from %dTB~128TB with size %dGB", options_.pool_space_start_tb,
                     options_.pool_space_size_gb);
        return UBS_ERROR;
    }

    pool_space_end_address_ = pool_space_start_address_ + space_size_bytes;
    next_expand_start_address_ = pool_space_start_address_;

    /* reserve vector memory */
    auto vector_size = space_size_bytes / SIZE_1M / options_.block_size_mb;
    tc_alloc_blocks_.reserve(vector_size);
    direct_alloc_blocks_.reserve(vector_size);

    return UBS_OK;
}

ALWAYS_INLINE Result FixedGlobalCache::ExpandBlock(bool forDirectAllocation) noexcept
{
    /* check if there is more space */
    if (next_expand_start_address_ >= pool_space_end_address_) {
        UBS_VLOG_ERR("Failed to expand block as run out of space, start %x-end %x, current %x",
                     pool_space_start_address_, pool_space_end_address_, next_expand_start_address_);
        return UBS_ERROR;
    }

    /* get the size of expand in bytes */
    const uint64_t expand_stride_bytes = SIZE_1M * options_.block_size_mb;

    auto result = options_.expanded_callback(next_expand_start_address_, expand_stride_bytes, ADD);
    if (result != UBS_OK) {
        UBS_VLOG_INFO("Failed to invoke callback when expand block at %x with size %dMB,",
                      reinterpret_cast<void *>(next_expand_start_address_), options_.block_size_mb);
        return result;
    }

    /* add to blocks vector */
    if (forDirectAllocation) {
        direct_alloc_blocks_.emplace_back(next_expand_start_address_, expand_stride_bytes / SIZE_1K);
    } else {
        tc_alloc_blocks_.emplace_back(next_expand_start_address_, expand_stride_bytes / SIZE_1K);
    }
    next_expand_start_address_ += expand_stride_bytes;
    return UBS_OK;
}

ALWAYS_INLINE void FixedGlobalCache::ShrinkAllBlocks() noexcept
{
    if (options_.expanded_callback == nullptr) {
        UBS_VLOG_WARN("Option 'expanded_callback' is nullptr'");
        return;
    }

    for (auto &block : direct_alloc_blocks_) {
        auto result = options_.expanded_callback(block.address, SIZE_1K * block.size_kb, BlockExpandFlag::REMOVE);
        if (result != UBS_OK) {
            UBS_VLOG_INFO("Failed to invoke callback when shrink block at %x with size %dMB,",
                          reinterpret_cast<void *>(block.address), options_.block_size_mb);
        }
    }

    for (auto &block : tc_alloc_blocks_) {
        auto result = options_.expanded_callback(block.address, SIZE_1K * block.size_kb, BlockExpandFlag::REMOVE);
        if (result != UBS_OK) {
            UBS_VLOG_INFO("Failed to invoke callback when shrink block at %x with size %dMB,",
                          reinterpret_cast<void *>(block.address), options_.block_size_mb);
        }
    }
}

ALWAYS_INLINE Result FixedGlobalCache::MakeMiniBlocks(uint32_t &mini_blocks_created, FixedMiniBlock *&blocks) noexcept
{
    const uint64_t n_m_stride = SIZE_1K * options_.mini_block_size_kb * (FIX_GC_BATCH_FREE_SIZE - 1);
    const uint64_t n_stride = SIZE_1K * options_.mini_block_size_kb * (FIX_TC_MINI_BLOCK_LINKED_COUNT - 1);
    const uint64_t stride = SIZE_1K * options_.mini_block_size_kb;

    auto iter = tc_alloc_blocks_.end();
    while (iter != tc_alloc_blocks_.begin()) {
        --iter;
        auto &block = *iter;
        if (block.mini_block_created) {
            /* if already made mini block, then stop */
            return UBS_OK;
        }

        /* double check if the block is valid */
        if (UNLIKELY(block.address == 0)) {
            UBS_VLOG_ERR("Invalid block address, which should not happen");
            return UBS_ERROR;
        }

        if (UNLIKELY(block.size_kb / SIZE_1K != options_.block_size_mb)) {
            UBS_VLOG_ERR("Invalid block size, which should not happene");
            return UBS_ERROR;
        }

        if (UNLIKELY(block.size_kb != block.free_size_kb)) {
            UBS_VLOG_ERR("Invalid block size, which should not happen");
            return UBS_ERROR;
        }

        if (UNLIKELY(block.size_kb % options_.mini_block_size_kb % FIX_GC_BATCH_FREE_SIZE != 0)) {
            UBS_VLOG_ERR("Invalid block size, which should not happen");
            return UBS_ERROR;
        }

        /* get loop count, which mini block count minus 1 */
        uint32_t loop_count = static_cast<uint32_t>(options_.block_size_mb) * SIZE_1K / options_.mini_block_size_kb - 1;

        mini_blocks_created += loop_count + 1;
        blocks = reinterpret_cast<FixedMiniBlock *>(block.address);
        blocks->tail = reinterpret_cast<FixedMiniBlock *>(block.address + stride * loop_count);

        /* split to mini blocks and link them */
        uintptr_t address = block.address;
        for (uint32_t i = 0; i < loop_count; i++) {
            FixedMiniBlock *tmp_mini_block = reinterpret_cast<FixedMiniBlock *>(address);
            /* link next_n_m */
            if (i % FIX_GC_BATCH_FREE_SIZE == 0) {
                tmp_mini_block->next_n_m = reinterpret_cast<FixedMiniBlock *>(address + n_m_stride);
                tmp_mini_block->next_n_m_count = FIX_GC_BATCH_FREE_SIZE;
            }

            /* link next_n */
            if (i % FIX_TC_MINI_BLOCK_LINKED_COUNT == 0) {
                tmp_mini_block->next_n = reinterpret_cast<FixedMiniBlock *>(address + n_stride);
                tmp_mini_block->next_n_count = FIX_TC_MINI_BLOCK_LINKED_COUNT;
            }

            /* move to next and link next */
            address += stride;
            tmp_mini_block->next = reinterpret_cast<FixedMiniBlock *>(address);
        }
        block.mini_block_created = true;
    }

    return UBS_OK;
}

ALWAYS_INLINE void FixedGlobalCache::UnInitInner() noexcept
{
    /* shrink all blocks */
    ShrinkAllBlocks();

    /* un-reserve space */
    if (pool_space_start_address_ != 0) {
        munmap(reinterpret_cast<void *>(pool_space_start_address_), SIZE_1G * options_.pool_space_size_gb);
        pool_space_start_address_ = 0;
    }

    pool_space_end_address_ = 0;
}

ALWAYS_INLINE uint32_t FixedGlobalCache::BatchAllocate(uint16_t requestSize, bool partialSatisfy,
                                                       FixedMiniBlock *&blocks)
{
    if (UNLIKELY(requestSize == 0)) {
        UBS_VLOG_ERR("Invalid request size, which should not happen");
        return 0;
    }

    /* partially satisfy allocated */
    if (partialSatisfy) {
        return BatchAllocateInnerPartial(requestSize, blocks);
    }

    /* fully satisfy */
    return BatchAllocateInner(requestSize, blocks);
}

ALWAYS_INLINE uint32_t FixedGlobalCache::BatchAllocateInner(uint16_t requestSize, FixedMiniBlock *&blocks) noexcept
{
    /* align to one free batch size */
    const auto aligned_request_size = AlignUpToSingleBatchSize(requestSize);
    uint32_t allocated = 0;
    FixedMiniBlock *head = nullptr;

    while (allocated < aligned_request_size) {
        /* step1: try to allocate from aligned mini block list */
        FixedMiniBlock *tmp_mini_blocks = nullptr;
        auto allocated_one_time = BatchAllocateInnerPartial(aligned_request_size - allocated, tmp_mini_blocks);
        allocated += allocated_one_time;
        /* if allocated some n*m blocks */
        if (allocated_one_time > 0) {
            if (head == nullptr) {
                head = tmp_mini_blocks;
            } else {
                head->tail->next = tmp_mini_blocks;
                head->tail = tmp_mini_blocks->tail;
            }
        }

        /* step2: if satisfied */
        if (allocated == aligned_request_size) {
            break;
        }

        /* step3: need to expand or wait for expand */
        std::unique_lock<std::mutex> lock(mutex_);
        /* wait if already expand by another thread */
        sync_expand_cv_.wait(lock, [&]() { return !sync_expanding_; });
        if (free_mini_blocks_aligned_ < FIX_GC_BATCH_FREE_SIZE) {
            continue;
        }
        sync_expanding_ = true;

        auto result = ExpandBlock(false);
        if (result == UBS_OK) {
            /* make mini blocks */
            uint32_t mini_blocks_created = 0;
            FixedMiniBlock *tmp_mini_blocks_created = nullptr;
            auto make_mini_block_result = MakeMiniBlocks(mini_blocks_created, tmp_mini_blocks_created);
            if (make_mini_block_result == UBS_OK) {
                /* attach to aligned list */
                pthread_spin_lock(&spinlock_aligned_);
                tmp_mini_blocks_created->tail = aligned_mini_blocks_;
                aligned_mini_blocks_ = tmp_mini_blocks_created;
                free_mini_blocks_aligned_ += mini_blocks_created;
                pthread_spin_unlock(&spinlock_aligned_);
            }
        }

        sync_expanding_ = false;
        sync_expand_cv_.notify_all();
        if (result != UBS_OK) {
            break;
        }
    }

    blocks = head;
    return allocated;
}

ALWAYS_INLINE uint32_t FixedGlobalCache::BatchAllocateInnerPartial(uint16_t requestSize,
                                                                   FixedMiniBlock *&blocks) noexcept
{
    /* align to one free batch size */
    const auto aligned_request_size = AlignUpToSingleBatchSize(requestSize);
    uint32_t allocated = 0;

    /* we only try to allocate from aligned list */
    pthread_spin_lock(&spinlock_aligned_);
    /* if less than one batch, just return */
    if (free_mini_blocks_aligned_ < FIX_GC_BATCH_FREE_SIZE) {
        pthread_spin_unlock(&spinlock_aligned_);
        return 0;
    }

    /* we can at least allocate one batch */
    FixedMiniBlock *iter = aligned_mini_blocks_;
    while (allocated < aligned_request_size && iter != nullptr) {
        allocated += FIX_GC_BATCH_FREE_SIZE;
        /* assign tail with last block of this n*m */
        aligned_mini_blocks_->tail = iter->next_n_m;
        /* move the iter to next block of this n*m block */
        iter = iter->next_n_m->next;
    }
    free_mini_blocks_aligned_ -= allocated;

    /* if allocated, assign to out blocks variable and change align mini blocks to new one */
    if (allocated > 0) {
        blocks = aligned_mini_blocks_;
        aligned_mini_blocks_ = iter;
    }
    blocks->tail->next = nullptr;
    pthread_spin_unlock(&spinlock_aligned_);

    return allocated;
}

ALWAYS_INLINE void FixedGlobalCache::TidyUnaligned() noexcept
{
    /* if there are no enough mini blocks, just return */
    if (free_mini_blocks_unaligned_ < FIX_GC_BATCH_FREE_SIZE) {
        return;
    }

    /* tidy to create one batch mini blocks n*m */
    uint32_t total_unaligned_free = free_mini_blocks_unaligned_;
    uint32_t process_index = 0;

    auto block_n_head = unaligned_mini_blocks_;
    auto iter = unaligned_mini_blocks_;

    while (total_unaligned_free >= FIX_GC_BATCH_FREE_SIZE) {
        auto pos_mode_n = process_index % FIX_TC_MINI_BLOCK_LINKED_COUNT;
        auto pos_mode_n_m = process_index % FIX_GC_BATCH_FREE_SIZE;
        if (pos_mode_n == 0) {
            /* if head of next n */
            block_n_head = iter;
        } else if (pos_mode_n == (FIX_TC_MINI_BLOCK_LINKED_COUNT - 1)) {
            /* if tail of next n */
            block_n_head->next_n = iter;
            block_n_head->next_n_count = FIX_TC_MINI_BLOCK_LINKED_COUNT;
        }

        if (pos_mode_n_m == FIX_GC_BATCH_FREE_SIZE - 1) {
            /* set n*m linked info */
            unaligned_mini_blocks_->next_n_m = iter;
            unaligned_mini_blocks_->next_n_m_count = FIX_GC_BATCH_FREE_SIZE;

            /* move to aligned list with lock
             * note here, here we already acquired spin lock of unaligned list,
             * i.e. we firstly acquired 'spinlock_unaligned_' then we acquired 'spinlock_aligned_',
             * then we should NOT do reverse operation, i.e. acquire 'spinlock_aligned_' then 'spinlock_unaligned_',
             * otherwise we probably encounter deadlock
             */
            auto tmp_iter = iter;
            iter = iter->next;

            pthread_spin_lock(&spinlock_aligned_);
            tmp_iter->next = aligned_mini_blocks_;
            aligned_mini_blocks_ = unaligned_mini_blocks_;
            free_mini_blocks_aligned_ += FIX_GC_BATCH_FREE_SIZE;
            pthread_spin_unlock(&spinlock_aligned_);

            /* set unaligned_mini_blocks */
            unaligned_mini_blocks_ = iter;
        } else {
            iter = iter->next;
        }

        --total_unaligned_free;
        ++process_index;
    }
}

ALWAYS_INLINE void FixedGlobalCache::BatchFree(FixedMiniBlock *block, uint16_t freeSize) noexcept
{
    if (UNLIKELY(freeSize == 0)) {
        UBS_VLOG_DEBUG("Un-reachable path, count of mini blocks to be freed is 0");
        return;
    }

    if (UNLIKELY(block == nullptr)) {
        UBS_VLOG_DEBUG("Un-reachable path, pointer of mini blocks to be freed is nullptr");
        return;
    }

    /* step1: if aligned or not */
    bool aligned = ((freeSize % FIX_GC_BATCH_FREE_SIZE) == 0);

    /* step2: free aligned */
    if (LIKELY(aligned)) {
        /* get the tail of block to be freed */
        auto tail = block->next_n_m;
        while (tail->next != nullptr) {
            tail = tail->next->next_n_m;
        }

        /* link to aligned list */
        pthread_spin_lock(&spinlock_aligned_);
        tail->next = aligned_mini_blocks_;
        aligned_mini_blocks_ = block;
        free_mini_blocks_aligned_ += freeSize;
        pthread_spin_unlock(&spinlock_aligned_);
        return;
    }

    /* step2: free un-aligned */
    /* get tail */
    auto unaligned_tail = block;
    while (unaligned_tail != nullptr) {
        if (unaligned_tail->next_n_m != nullptr) {
            unaligned_tail = unaligned_tail->next_n_m;
        } else if (unaligned_tail->next_n != nullptr) {
            unaligned_tail = unaligned_tail->next_n;
        } else if (unaligned_tail->next != nullptr) {
            unaligned_tail = unaligned_tail->next;
        } else {
            break;
        }
    }

    /* link to the head */
    pthread_spin_lock(&spinlock_unaligned_);
    unaligned_tail->next = unaligned_mini_blocks_;
    unaligned_mini_blocks_ = block;
    free_mini_blocks_unaligned_ += freeSize;

    /* tidy */
    TidyUnaligned();
    pthread_spin_unlock(&spinlock_unaligned_);
}

ALWAYS_INLINE bool FixedGlobalCache::InRange(uintptr_t address, uint64_t size) noexcept
{
    return address >= pool_space_start_address_ && (address + size) < pool_space_end_address_;
}

ALWAYS_INLINE bool FixedGlobalCache::BatchInRange(uint16_t batchSize, uintptr_t *addresses, uint64_t *size) noexcept
{
    for (uint16_t i = 0; i < batchSize; i++) {
        if (!InRange(addresses[i], size[i])) {
            return false;
        }
    }

    return true;
}

ALWAYS_INLINE bool FixedGlobalCache::DirectAllocate(uint32_t size, uintptr_t &address) noexcept
{
    if (UNLIKELY(!init_)) {
        UBS_VLOG_ERR("Failed to allocate memory as global cache not initialized");
        return false;
    }

    return direct_allocator_.DirectAllocate(size, address);
}

ALWAYS_INLINE void FixedGlobalCache::DirectFree(uintptr_t address) noexcept
{
    if (UNLIKELY(!init_)) {
        UBS_VLOG_ERR("Failed to free memory as global cache not initialized");
        return;
    }

    direct_allocator_.DirectFree(address);
}

ALWAYS_INLINE Result FixedGlobalCache::HandleExpandBlock(bool flags, FixedBlock &expandedBlock) noexcept
{
    std::lock_guard<std::mutex> locker(mutex_);
    Result ret = ExpandBlock(flags);
    if (ret == UBS_OK) {
        if (flags) {
            expandedBlock = direct_alloc_blocks_.back();
        } else {
            expandedBlock = tc_alloc_blocks_.back();
        }
    }
    return ret;
}
} // namespace allocator
} // namespace ubs
} // namespace ock

#endif //UBS_COMM_UBSOCKET_ALLOC_GLOBAL_CACHE_H
