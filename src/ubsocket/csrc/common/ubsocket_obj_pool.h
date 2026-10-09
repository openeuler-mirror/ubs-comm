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
#ifndef UBS_OBJ_POOL_H
#define UBS_OBJ_POOL_H

#include <malloc.h>
#include <condition_variable>
#include <memory>

#include "ubsocket_common_includes.h"

namespace ock {
namespace ubs {
/*
 * There are two levels of blocks for fixed size of memory pool
 * 1 Block: which allocated from OS
 * 2 Mini block: which allocated from thread cache to end users
 *
 * So:
 * a) size of block is multiple times of size of mini block
 * b) size of mini block is min size of allocate unit to user
 *
 * Block allocated from OS, node list
 */
struct MemBlock {
    void *buffer = nullptr; /* point to real buffer */
    uint64_t size = 0;      /* memory size of the super block */

    MemBlock(void *buf, uint64_t s) : buffer(buf), size(s) {}
};

/*
 * Mini block allocated to end user
 */
struct MemMiniBlock {
    MemMiniBlock *next = nullptr;   /* link to next mini block */
    MemMiniBlock *next_n = nullptr; /* link to next N mini block */
    uint32_t count = 0;             /* current link count */
};

/*
 * Options of fixed size memory pool
 */
struct ObjectPoolOptions {
    uint16_t block_size_mb = 4;          /* size of each super block, by default 4 MB */
    uint16_t mini_block_size_bytes = 64; /* size of mini block by default is 64 bytes */
    uint16_t tc_batch_alloc_count = 128; /* count of mini block to expand from shared pool at one time */

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "blk-size-mb: " << block_size_mb << ", mini-blk-size: " << mini_block_size_bytes
            << ", tc_batch_alloc_count: " << tc_batch_alloc_count;
        return oss.str();
    }
} __attribute__((packed));

/*
 * Mem pool for fixed size objects
 *
 * ObjectGlobalCache is shared mem pool for all threads
 * ObjectThreadCache is thread local cache
 *
 */
class ObjectThreadCache;
class ObjectGlobalCache;

using ObjectGlobalCachePtr = Ref<ObjectGlobalCache>;

/*
 * Memory pool allocated from OS and shared by thread
 */
class ObjectGlobalCache {
public:
    ObjectGlobalCache(const std::string &name, const ObjectPoolOptions &options);
    ~ObjectGlobalCache()
    {
        UnInitialize();
        (void)pthread_spin_destroy(&tc_alloc_lock_);
    }

    Result Initialize();

    void UnInitialize()
    {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!inited_) {
            return;
        }

        DoUnInitialize();
        inited_ = false;
    }

    /*
     * @brief Allocate batch of mini block from pool, which called by thread cache
     */
    Result BatchAlloc(MemMiniBlock &head);

    /*
     * @brief Free batch of mini block to pool, which called by thread cache
     */
    Result BatchFree(MemMiniBlock *head)
    {
        UBS_ASSERT_RETURN(head != nullptr, UBS_ERROR);
        UBS_ASSERT_RETURN(head->next_n != nullptr, UBS_ERROR);

        pthread_spin_lock(&tc_alloc_lock_);
        (void)AttacheToFreeList(head, head->next_n, head->count);
        pthread_spin_unlock(&tc_alloc_lock_);

        return UBS_OK;
    }

    std::string ToString();

    DEFINE_REF_OPERATION_FUNC

private:
    void DoUnInitialize();

    Result Validate();

    /*
     * @brief Expand one super block from OS
     */
    Result ExpandFromOs(bool holdFreeListLock);

    /*
     * @brief Make the super block to mini block with linked list
     */
    Result MakeFreeList(const MemBlock &block, MemMiniBlock *&head, MemMiniBlock *&tail, uint32_t &count) const
    {
        count = block.size / options_.mini_block_size_bytes;
        auto address = reinterpret_cast<uintptr_t>(block.buffer);
        head = reinterpret_cast<MemMiniBlock *>(address);
        auto iter = head;

        MemMiniBlock *batchHeader = nullptr;

        /* make free linked list */
        for (uint32_t i = 1; i <= count; ++i) {
            auto mod = i % options_.tc_batch_alloc_count;
            if (mod == 1) {
                /* first mini block, remember this as header */
                batchHeader = iter;
            } else if (mod == 0 && batchHeader != nullptr) {
                /* N block, set the next_n of header to this and set count */
                batchHeader->next_n = iter;
                batchHeader->count = options_.tc_batch_alloc_count;
            }

            /* set next */
            address += options_.mini_block_size_bytes;
            iter->next = reinterpret_cast<MemMiniBlock *>(address);

            /* move to next and skip the last one */
            if (i != count) {
                iter = reinterpret_cast<MemMiniBlock *>(address);
            }
        }

        tail = iter;
        tail->next = nullptr;
        tail->next_n = nullptr;
        tail->count = 0;

        return UBS_OK;
    }

    /*
     * @brief Attach linked mini block to free list
     */
    inline Result AttacheToFreeList(MemMiniBlock *head, MemMiniBlock *tail, uint32_t count)
    {
        UBS_ASSERT_RETURN(head != nullptr, UBS_INVALID_PARAM);
        UBS_ASSERT_RETURN(tail != nullptr, UBS_INVALID_PARAM);
        UBS_ASSERT_RETURN(count != 0, UBS_INVALID_PARAM);

        tail->next = free_mini_blocks_.next;
        free_mini_blocks_.next = head;
        free_mini_block_count_ += count;
        return UBS_OK;
    }

private:
    pthread_spinlock_t tc_alloc_lock_;
    MemMiniBlock free_mini_blocks_{};
    uint64_t free_mini_block_count_ = 0;

    ObjectPoolOptions options_{};
    std::mutex mutex_;
    std::condition_variable expand_cv_;
    bool expanding_ = false;
    std::vector<MemBlock> blocks_;
    uint64_t total_block_size_ = 0;

    std::string name_;
    bool inited_ = false;

    DECLARE_REF_COUNT_VARIABLE;

    friend class ObjectThreadCache;
};

/*
 * Thread cache for fixed size of memory pool, usually for object
 */
class ObjectThreadCache {
public:
    ObjectThreadCache() = default;
    explicit ObjectThreadCache(ObjectGlobalCache *sharePool);
    ~ObjectThreadCache()
    {
        if (global_cache_ != nullptr) {
            FreeAllToGlobalCache();
            global_cache_->DecreaseRef();
            global_cache_ = nullptr;
        }
    }

    /*
     * @brief Rebind this thread cache to the given shared pool.
     *
     * GetTc() caches one ObjectThreadCache per thread, but the owning pool can be
     * UnInitialize()d and re-Initialize()d to a brand new ObjectGlobalCache. In that case the
     * thread-local still points at the old (already uninitialized) cache. Rebind drops the
     * stale binding/link list and attaches to the current one, so a later Free() can never
     * write into memory that the old cache already released.
     */
    void Rebind(ObjectGlobalCache *sharePool)
    {
        if (global_cache_ == sharePool) {
            return;
        }

        /* return cached mini blocks to the previous cache only if it is still usable */
        if (global_cache_ != nullptr) {
            FreeAllToGlobalCache();
            global_cache_->DecreaseRef();
        }

        global_cache_ = sharePool;
        head_.next = nullptr;
        free_mini_blocks_count_ = 0;
        batch_free_count_ = 0;

        if (global_cache_ != nullptr) {
            global_cache_->IncreaseRef();
            batch_free_count_ = global_cache_->options_.tc_batch_alloc_count;
        }
    }

    /*
     * @brief Allocate one from thread cache, this is not thread safe
     */
    void *Allocate()
    {
        if (LIKELY(head_.next != nullptr)) {
            /* allocate from head */
            auto tmp = head_.next;
            head_.next = tmp->next;
            /* assign tail to null if it is empty */
            --free_mini_blocks_count_;
            return static_cast<void *>(tmp);
        }

        /* allocate from shared pool */
        UBS_ASSERT_RETURN(global_cache_ != nullptr && global_cache_->inited_, nullptr);

        if (global_cache_->BatchAlloc(head_) != UBS_OK) {
            return nullptr;
        }

        /* set current free */
        free_mini_blocks_count_ = head_.next->count - 1;

        /* move head to next and return first */
        auto tmp = head_.next;
        head_.next = head_.next->next;
        return static_cast<void *>(tmp);
    }

    /*
     * @brief Free one to thread cache, this is not thread safe
     */
    void Free(void *value)
    {
        if (LIKELY(value == nullptr)) {
            return;
        }

        /* the shared pool is gone (uninitialized/destroyed): its blocks were already reclaimed */
        if (UNLIKELY(global_cache_ == nullptr || !global_cache_->inited_)) {
            return;
        }

        /* insert into first */
        auto tmp = static_cast<MemMiniBlock *>(value);
        tmp->next = head_.next;
        head_.next = tmp;

        ++free_mini_blocks_count_;
        /* judge is current free count is 2 times larger than free steps
         * 1 no：just return
         * 2 yes: return many to shared pool, which means return in batch to reduce the cost of mutex in shared pool
         */
        if ((free_mini_blocks_count_ >> 1) < batch_free_count_) {
            return;
        }

        /* step 1: get first */
        auto head = head_.next;

        /* step 2: move head forward batch_free_count_ and get tail */
        const uint16_t returnCount = batch_free_count_ - 1;
        for (uint16_t i = 0; i < returnCount; ++i) {
            head_.next = head_.next->next;
        }
        head->next_n = head_.next;

        /* step 3: move head one more */
        head_.next = head_.next->next;

        head->next_n->next = nullptr;
        head->count = batch_free_count_;

        /* step 4: decrease current */
        free_mini_blocks_count_ -= batch_free_count_;

        /* step 5: return to share pool */
        global_cache_->BatchFree(head);
    }

    std::string ToString();

private:
    /*
     * Free all to pool
     */
    void FreeAllToGlobalCache()
    {
        if (UNLIKELY(global_cache_ == nullptr)) {
            UBS_VLOG_ERR("Un-reachable path, global cache is null");
            return;
        }

        if (free_mini_blocks_count_ == 0) {
            return;
        }

        /* the shared pool was uninitialized: its super blocks are already freed, drop ours */
        if (UNLIKELY(!global_cache_->inited_)) {
            head_.next = nullptr;
            free_mini_blocks_count_ = 0;
            return;
        }

        /* step 1: get first */
        auto head = head_.next;

        /* step 2: move head forward batch_free_count_ and get tail */
        const uint16_t returnCount = free_mini_blocks_count_ - 1;
        for (uint16_t i = 0; i < returnCount; ++i) {
            head_.next = head_.next->next;
        }
        head->next_n = head_.next;

        /* step 3: reset */
        head_.next = nullptr;

        /* step 4: free */
        head->next_n->next = nullptr;
        head->count = free_mini_blocks_count_;
        global_cache_->BatchFree(head);

        free_mini_blocks_count_ = 0;
    }

private:
    MemMiniBlock head_{};
    ObjectGlobalCache *global_cache_ = nullptr;
    uint16_t free_mini_blocks_count_ = 0;
    uint16_t batch_free_count_ = 0;

    friend class ObjectGlobalCache;
};

ALWAYS_INLINE ObjectGlobalCache::ObjectGlobalCache(const std::string &name, const ObjectPoolOptions &options)
    : options_(options),
      name_(name)
{
    (void)pthread_spin_init(&tc_alloc_lock_, PTHREAD_PROCESS_PRIVATE);
}

ALWAYS_INLINE Result ObjectGlobalCache::Initialize()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (inited_) {
        return UBS_OK;
    }

    /* validate options */
    Result result = Validate();
    if (result != UBS_OK) {
        return result;
    }

    /* reserve to avoid reallocate memory when expanding vector */
    blocks_.reserve(1024L);

    /* expand one super block from os */
    if ((result = ExpandFromOs(false)) != UBS_OK) {
        DoUnInitialize();
        return result;
    }

    inited_ = true;
    return UBS_OK;
}

ALWAYS_INLINE void ObjectGlobalCache::DoUnInitialize()
{
    for (auto &iter : blocks_) {
        free(iter.buffer);
        iter.buffer = nullptr;
    }

    blocks_.clear();
    total_block_size_ = 0;
    free_mini_block_count_ = 0;
}

ALWAYS_INLINE Result ObjectGlobalCache::Validate()
{
    constexpr uint32_t N256 = 256;
    constexpr uint32_t N128 = 128;
    constexpr uint32_t N8 = 8;
    constexpr uint32_t N4 = 4;
    /* validate super block size, which must between 1 and 256 MB including 256 MB */
    if (options_.block_size_mb == 0 || options_.block_size_mb > N256) {
        UBS_SLOG_ERR("Invalid block_size_mb " << options_.block_size_mb << " in mem pool " << name_ << ", which be 1~"
                                              << N256 << ", reset to " << N4);
        return UBS_INVALID_PARAM;
    }

    /* validate thread cache expand and shrink steps, which between 8 and 256 MB */
    if (options_.tc_batch_alloc_count < 8L || options_.tc_batch_alloc_count > 256L) {
        UBS_SLOG_ERR("Invalid tc_batch_alloc_count " << options_.tc_batch_alloc_count << " in mem pool " << name_
                                                     << ", which be " << N8 << "~" << N256 << ", reset to " << N128);
        return UBS_INVALID_PARAM;
    }

    /* validate size of min block */
    if (options_.mini_block_size_bytes < sizeof(MemMiniBlock)) {
        UBS_SLOG_ERR("Invalid mini_block_size_bytes " << options_.mini_block_size_bytes << " in mem pool " << name_
                                                      << ", which be larger than " << sizeof(MemMiniBlock));
        return UBS_INVALID_PARAM;
    }

    /* validate relation of min block size and super block size */
    uint64_t tmp = options_.mini_block_size_bytes * options_.tc_batch_alloc_count * 16L;
    tmp = tmp / SIZE_1K / SIZE_1K; /* in MB */
    if (tmp > options_.block_size_mb) {
        UBS_SLOG_ERR("Invalid mini_block_size_bytes " << options_.mini_block_size_bytes << " in mem pool " << name_);
        return UBS_INVALID_PARAM;
    }

    uint64_t superBlkSize = options_.block_size_mb * SIZE_1K * SIZE_1K;
    if (superBlkSize % (options_.mini_block_size_bytes * options_.tc_batch_alloc_count)) {
        UBS_SLOG_ERR("Invalid mini_block_size_bytes "
                     << options_.mini_block_size_bytes << " or tc_batch_alloc_count " << options_.tc_batch_alloc_count
                     << " in mem pool " << name_ << ", super block size is not times of "
                     << options_.mini_block_size_bytes * options_.tc_batch_alloc_count);
        return UBS_INVALID_PARAM;
    }

    return UBS_OK;
}

ALWAYS_INLINE Result ObjectGlobalCache::ExpandFromOs(bool holdFreeListLock)
{
    // uint64_t startTime = NetMonotonic::TimeNs();
    /* allocate memory */
    auto superBlkSize = (total_block_size_ == 0) ? (options_.block_size_mb * SIZE_1K * SIZE_1K) : total_block_size_;
    auto mem = memalign(SIZE_4K, superBlkSize);
    if (mem == nullptr) {
        UBS_SLOG_ERR("Failed to malloc memory for supper block in mem pool " << name_);
        return UBS_MALLOC_FAILED;
    }

    /* get physical memory */
    bzero(mem, superBlkSize);

    /* insert to super block list */
    MemBlock blk(mem, superBlkSize);
    blocks_.emplace_back(blk);

    /* add size and count */
    total_block_size_ += superBlkSize;

    /* make free linked list */
    MemMiniBlock *head = nullptr;
    MemMiniBlock *tail = nullptr;
    uint32_t count = 0;
    Result result = UBS_OK;
    if ((result = MakeFreeList(blk, head, tail, count)) != UBS_OK) {
        return result;
    }

    /* attach free linked list */
    if (holdFreeListLock) {
        pthread_spin_lock(&tc_alloc_lock_);
    }

    if ((result = AttacheToFreeList(head, tail, count)) != UBS_OK) {
        if (holdFreeListLock) {
            pthread_spin_unlock(&tc_alloc_lock_);
        }
        return result;
    }

    if (holdFreeListLock) {
        pthread_spin_unlock(&tc_alloc_lock_);
    }

    UBS_SLOG_DEBUG("Fixed size memory pool "
                   << name_ << " allocated " << options_.block_size_mb << "MB memory from os, total block size "
                   << total_block_size_ << " and split to " << count << " min block with size "
                   << options_.mini_block_size_bytes << " which took " << 0 << "us, current free min block is "
                   << free_mini_block_count_);

    return UBS_OK;
}

ALWAYS_INLINE Result ObjectGlobalCache::BatchAlloc(MemMiniBlock &head)
{
    UBS_ASSERT_RETURN(inited_, UBS_ERROR);
    bool flag = true;
    do {
        /* step 1: allocate from free list */
        pthread_spin_lock(&tc_alloc_lock_);
        if (free_mini_block_count_ > 0) {
            head.next = free_mini_blocks_.next;
            free_mini_block_count_ -= free_mini_blocks_.next->count;
            free_mini_blocks_.next = head.next->next_n->next;
            head.next->next_n->next = nullptr;
            pthread_spin_unlock(&tc_alloc_lock_);
            flag = false;
            return UBS_OK;
        }
        pthread_spin_unlock(&tc_alloc_lock_);

        /* step 2: if there is no free in list, allocate from OS */
        {
            std::unique_lock<std::mutex> locker(mutex_);
            /* wait if already allocating from os by another thread */
            expand_cv_.wait(locker, [&]() { return !expanding_; });
            if (free_mini_block_count_ > 0) {
                continue;
            }
            expanding_ = true;

            Result result = ExpandFromOs(true);
            expanding_ = false;
            expand_cv_.notify_all();
            if (result != UBS_OK) {
                flag = false;
                return result;
            }
        }
    } while (flag);

    UBS_SLOG_ERR("Unreachable code path");
    return UBS_ERROR;
}

ALWAYS_INLINE std::string ObjectGlobalCache::ToString()
{
    std::ostringstream oss;
    oss << "fixed-size-memory-pool [name: " << name_ << ", options: [" << options_.ToString()
        << "], block-count: " << blocks_.size() << ", block-size: " << total_block_size_ / SIZE_1K / SIZE_1K
        << "MB, free-min-block-count: " << free_mini_block_count_;

    uint32_t blkIndex = 0;
    oss << " super-blocks: [";
    for (auto &iter : blocks_) {
        oss << "superBlk" << blkIndex++ << ":" << iter.size << "," << iter.buffer << " ";
    }

    oss << "] min-blocks: [";
    auto iter = free_mini_blocks_.next;
    blkIndex = 0;
    while (iter != nullptr) {
        if (iter->next_n != nullptr) {
            oss << "** " << blkIndex++ << ":" << iter << "," << iter->next_n << "," << iter->count << " ";
        } else {
            oss << blkIndex++ << ":" << iter << " ";
        }
        iter = iter->next;
    }
    oss << "]]";

    return oss.str();
}

/* ObjectThreadCache */
ALWAYS_INLINE ObjectThreadCache::ObjectThreadCache(ObjectGlobalCache *sharePool) : global_cache_(sharePool)
{
    if (UNLIKELY(global_cache_ == nullptr)) {
        return;
    }

    global_cache_->IncreaseRef();

    batch_free_count_ = global_cache_->options_.tc_batch_alloc_count;
}

ALWAYS_INLINE std::string ObjectThreadCache::ToString()
{
    std::ostringstream oss;
    oss << "net-thread-cache: [free-steps: " << batch_free_count_ << ", current-free: " << free_mini_blocks_count_;

    auto iter = head_.next;
    uint16_t minBlkIndex = 0;
    oss << " mini-blocks: [";
    while (iter != nullptr) {
        oss << "blk" << minBlkIndex++ << ":" << iter << " ";
        iter = iter->next;
    }
    oss << "]]";
    return oss.str();
}
} // namespace ubs
} // namespace ock

#endif // UBS_OBJ_POOL_H
