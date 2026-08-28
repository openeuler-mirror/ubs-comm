/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef OCK_HCOM_NET_MEM_POOL_H
#define OCK_HCOM_NET_MEM_POOL_H

#include <condition_variable>
#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>

#include "hcom.h"

namespace ock {
namespace hcom {
/*
 * There are two levels of blocks for fixed size of memory pool
 * 1 Super block: which allocated from OS
 * 2 Mini block: which allocated from thread cache to end users
 *
 * So:
 * a) size of super block is multiple times of size of mini block
 * b) size of mini block is min size of allocate unit to user
 *
 * Super block allocated from OS, node list
 */
struct NetMemPoolSuperBlock {
    void *buffer = nullptr; /* point to real buffer */
    uint64_t size = 0;      /* memory size of the super block */

    NetMemPoolSuperBlock(void *buf, uint64_t s) : buffer(buf), size(s) {}
};

/*
 * Mini block allocated to end user
 */
struct NetMemPoolMinBlock {
    NetMemPoolMinBlock *next = nullptr;  /* link to next min block */
    NetMemPoolMinBlock *nextN = nullptr; /* link to next N min block */
    uint32_t count = 0;                  /* current link count */
};

/*
 * Options of fixed size memory pool
 */
struct NetMemPoolFixedOptions {
    uint16_t superBlkSizeMB = NN_NO4;   /* size of each super block, by default 4 MB */
    uint16_t minBlkSize = NN_NO64;      /* size of min block by default is 64 bytes */
    uint16_t tcExpandBlkCnt = NN_NO128; /* count of min block to expand from shared pool at one time */

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "super-blk-size-mb: " << superBlkSizeMB << ", min-blk-size: " << minBlkSize <<
            ", thread-cache-blk-count: " << tcExpandBlkCnt;
        return oss.str();
    }
} __attribute__((packed));

/*
 * Mem pool for fixed size objects
 *
 * NetMemPoolFixed is shared mem pool for all threads
 * NetTCacheFixed is thread local cache
 *
 * Thread-cache policy (NEW, configurable):
 *   By default objects are allocated/freed directly from the SHARED pool via
 *   TCAllocOne/TCFreeOne, i.e. the per-thread cache (NetTCacheFixed /
 *   KeyedThreadLocalCache) is BYPASSED. This is the safe default because some
 *   objects (op context, timer context) are allocated on one thread and freed
 *   on another (e.g. the timeout thread), so a thread-local cache would pin
 *   blocks and inflate the pool. Bypassing also keeps the pool
 *   "outstanding blocks" counter meaningful for leak diagnosis.
 *   Set HCOM_THREAD_CACHE=1 (true/on/yes) to OPT INTO the per-thread cache for
 *   better single-thread throughput. See HcomThreadCacheEnabled() below.
 */
class NetTCacheFixed;
class NetMemPoolFixed;

using NetMemPoolFixedPtr = NetRef<NetMemPoolFixed>;

/*
 * @brief Thread-cache enable switch for the op-context pool (net_ctx_info_pool.h) and the
 *        timer/ctx pool (service_ctx_store.h).
 *
 *        Default OFF (bypass): the pools allocate/free directly via TCAllocOne/TCFreeOne from
 *        the SHARED pool, which avoids per-thread caching and is safe for cross-thread block
 *        reuse (e.g. an op ctx or timer ctx allocated on one thread and freed on another). It
 *        also keeps the pool "outstanding block" counter meaningful for leak diagnosis.
 *
 *        Set HCOM_THREAD_CACHE=1 (or true/on/yes) to opt INTO the per-thread cache
 *        (NetTCacheFixed / KeyedThreadLocalCache) for better single-thread throughput. The
 *        value is read once and cached for the process lifetime.
 */
inline bool HcomThreadCacheEnabled()
{
    static const bool enabled = []() {
        const char *e = std::getenv("HCOM_THREAD_CACHE");
        if (e == nullptr) {
            return false; /* default: bypass (no thread cache) */
        }
        auto toLower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
        std::string v(e);
        for (auto &c : v) {
            c = toLower(c);
        }
        return v == "1" || v == "true" || v == "on" || v == "yes";
    }();
    return enabled;
}

/*
 * Memory pool allocated from OS and shared by thread
 */
class NetMemPoolFixed {
public:
    NetMemPoolFixed(const std::string &name, const NetMemPoolFixedOptions &options);
    ~NetMemPoolFixed()
    {
        UnInitialize();
        OBJ_GC_DECREASE(NetMemPoolFixed);
    }

    NResult Initialize();

    void UnInitialize()
    {
        std::lock_guard<std::mutex> guard(mMutex);
        if (!mInited) {
            return;
        }

        DoUnInitialize();
        mInited = false;
    }

    /*
     * @brief Allocate batch of min block from pool, which called by thread cache
     */
    NResult TCAlloc(NetMemPoolMinBlock &head);

    /*
     * @brief Free batch of min block to pool, which called by thread cache
     */
    NResult TCFree(NetMemPoolMinBlock *head)
    {
        NN_ASSERT_LOG_RETURN(head != nullptr, NN_NOT_INITIALIZED)
        NN_ASSERT_LOG_RETURN(head->nextN != nullptr, NN_NOT_INITIALIZED)

        /* [leak-trace] record before attaching, count may be reused after the block goes back to free list */
        const uint32_t returnedBlks = head->count;

        mTcMutex.Lock();
        (void)AttacheToFreeList(head, head->nextN, head->count);
        mTcMutex.Unlock();

        mTcFreeBlks.fetch_add(returnedBlks, std::memory_order_relaxed);

        return NN_OK;
    }

    /*
     * @brief Allocate ONE min block from the shared pool, bypassing the per-thread
     *        cache. Used when the caller needs cross-thread block reuse (e.g. the
     *        timer ctx store: a block is allocated on the caller thread but freed on
     *        a different worker/timeout thread, so a per-thread cache would pin it
     *        forever and the pool would balloon).
     *
     *        TCAlloc always returns a whole batch of tcExpandBlkCnt blocks, so this
     *        grabs one batch, hands the object out, and returns the remainder to the
     *        shared pool immediately. Every call takes mTcMutex.
     */
    template <typename T>
    T *TCAllocOne()
    {
        NetMemPoolMinBlock head;
        if (NN_UNLIKELY(TCAlloc(head) != NN_OK)) {
            return nullptr;
        }

        T *obj = reinterpret_cast<T *>(head.next);

        /* TCAlloc returns a batch of tcExpandBlkCnt blocks. Hand the rest back to the
         * shared pool so only one block leaves this call. The remainder is a PERSISTENT
         * batch: its header is head.next->next (block[2], a real block in the super
         * block), and its .next chain is already intact from MakeFreeList — only its
         * .nextN (batch tail) and .count need (re)setting. A batch of 1 (previously
         * freed via TCFreeOne) has no remainder, so skip the return in that case. */
        if (head.next->count > 1) {
            NetMemPoolMinBlock *rest = head.next->next; /* block[2], persistent */
            rest->nextN = head.next->nextN;            /* original batch tail */
            rest->count = head.next->count - 1;
            (void)TCFree(rest);
        }

        return obj;
    }

    /*
     * @brief Free ONE min block back to the shared pool directly, bypassing the
     *        per-thread cache. Packs the block as a single-block batch (tail == self)
     *        so TCAlloc can pop it later. Goes through mTcMutex.
     */
    template <typename T>
    void TCFreeOne(T *obj)
    {
        if (NN_UNLIKELY(obj == nullptr)) {
            return;
        }
        NetMemPoolMinBlock *blk = reinterpret_cast<NetMemPoolMinBlock *>(obj);
        blk->next = nullptr;
        blk->nextN = blk; /* single-block batch: tail == head */
        blk->count = 1;
        (void)TCFree(blk);
    }

    /*
     * @brief [leak-trace] one-line water mark of the pool, used to tell "pool is really leaking"
     *        apart from "pool is only fragmented / cached in thread caches"
     *        outstanding == blocks handed out to thread caches but never returned to the shared pool
     */
    inline std::string WaterMark()
    {
        const uint64_t allocBlks = mTcAllocBlks.load(std::memory_order_relaxed);
        const uint64_t freeBlks = mTcFreeBlks.load(std::memory_order_relaxed);

        std::ostringstream oss;
        oss << "pool " << mName << "@" << this << ", min-blk-size " << mOptions.minBlkSize << ", super-blks "
            << mSuperBlocks.size() << ", total-bytes " << mTotalSuperBlkSize << ", total-min-blk " << mTotalMinBlkCount
            << ", free-min-blk " << mFreeCount << ", active-caches "
            << mActiveCacheCount.load(std::memory_order_relaxed) << ", tc-alloc-block " << allocBlks
            << ", tc-free-block " << freeBlks << ", outstanding-blk " << (allocBlks - freeBlks);
        return oss.str();
    }

    /*
     * @brief [leak-trace] blocks handed out to thread caches but never returned to the
     *        shared free list (tc-alloc-block - tc-free-block). Printed by HCOM_OPCTX_TRACE and
     *        any diagnostic to split "destructor called but block pinned in thread cache"
     *        apart from "destructor never called".
     */
    inline uint64_t OutstandingBlocks() const
    {
        return mTcAllocBlks.load(std::memory_order_relaxed) - mTcFreeBlks.load(std::memory_order_relaxed);
    }

    std::string ToString();

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    void DoUnInitialize();

    NResult Validate();

    /*
     * @brief Expand one super block from OS
     */
    NResult ExpandFromOs(bool holdFreeListLock);

    /*
     * @brief Make the super block to min block with linked list
     */
    NResult MakeFreeList(const NetMemPoolSuperBlock &superBlk, NetMemPoolMinBlock *&head, NetMemPoolMinBlock *&tail,
        uint32_t &count) const
    {
        count = superBlk.size / mOptions.minBlkSize;
        auto address = reinterpret_cast<uintptr_t>(superBlk.buffer);
        head = reinterpret_cast<NetMemPoolMinBlock *>(address);
        auto iter = head;

        NetMemPoolMinBlock *batchHeader = nullptr;

        /* make free linked list */
        for (uint32_t i = 1; i <= count; ++i) {
            auto mod = i % mOptions.tcExpandBlkCnt;
            if (mod == 1) {
                /* first min block, remember this as header */
                batchHeader = iter;
            } else if (mod == 0 && batchHeader != nullptr) {
                /* N block, set the nextN of header to this and set count */
                batchHeader->nextN = iter;
                batchHeader->count = mOptions.tcExpandBlkCnt;
            }

            /* set next */
            address += mOptions.minBlkSize;
            iter->next = reinterpret_cast<NetMemPoolMinBlock *>(address);

            /* move to next and skip the last one */
            if (i != count) {
                iter = reinterpret_cast<NetMemPoolMinBlock *>(address);
            }
        }

        tail = iter;
        tail->next = nullptr;
        tail->nextN = nullptr;
        tail->count = 0;

        return NN_OK;
    }

    /*
     * @brief Attach linked min block to free list
     */
    inline NResult AttacheToFreeList(NetMemPoolMinBlock *head, NetMemPoolMinBlock *tail, uint32_t count)
    {
        NN_ASSERT_LOG_RETURN(head != nullptr, NN_INVALID_PARAM);
        NN_ASSERT_LOG_RETURN(tail != nullptr, NN_INVALID_PARAM);
        NN_ASSERT_LOG_RETURN(count != 0, NN_INVALID_PARAM);

        tail->next = mFreeMinBlkList.next;
        mFreeMinBlkList.next = head;
        mFreeCount += count;
        return NN_OK;
    }

private:
    NetSpinLock mTcMutex;
    NetMemPoolMinBlock mFreeMinBlkList {};
    uint64_t mFreeCount = 0;

    NetMemPoolFixedOptions mOptions {};
    std::mutex mMutex;
    std::condition_variable mCondForOs;
    bool mAllocatingFromOs = false;
    std::vector<NetMemPoolSuperBlock> mSuperBlocks;
    uint64_t mTotalSuperBlkSize = 0;

    /* [leak-trace] accumulated min blocks ever created / handed to thread caches / returned by thread caches */
    uint64_t mTotalMinBlkCount = 0;
    std::atomic<uint64_t> mTcAllocBlks{0};
    std::atomic<uint64_t> mTcFreeBlks{0};

    /* [tcache-trace] count of live thread caches (NetTCacheFixed) currently bound to this pool.
       Incremented in NetTCacheFixed ctor, decremented in dtor. Helps tell "many threads each
       pinning a cache" apart from "few caches hoarding unbounded" when reading outstanding-blk. */
    std::atomic<uint32_t> mActiveCacheCount{0};

    std::string mName;
    bool mInited = false;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class NetTCacheFixed;
};

/*
 * Thread cache for fixed size of memory pool, usually for object
 */
class NetTCacheFixed {
public:
    explicit NetTCacheFixed(NetMemPoolFixed *sharePool);
    ~NetTCacheFixed()
    {
        /* [tcache-dtor] Count destructors that actually ran. Compare against the
           per-pool mActiveCacheCount printed by [MEMPOOL-LEAK]: if this logs rarely
           while active-caches climbs into the tens of thousands, transient threads
           are exiting without running their TLS destructor (glibc dlclose / abrupt
           pthread_exit), pinning their 256-block reserve forever. Throttled to every
           1024th dtor to bound log volume. */
        static std::atomic<uint64_t> sDtorCalls{0};
        uint64_t n = sDtorCalls.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n & 0x3FF) == 0) {
            uint32_t live = (mSharedPool != nullptr)
                ? mSharedPool->mActiveCacheCount.load(std::memory_order_relaxed)
                : 0;
            NN_LOG_WARN("[TCACHE-DTOR] pool=" << (mSharedPool != nullptr ? mSharedPool->mName : std::string("?"))
                << " active-caches=" << live
                << " total-dtor=" << n);
        }

        FreeAllToPool();

        if (mSharedPool != nullptr) {
            mSharedPool->mActiveCacheCount.fetch_sub(1, std::memory_order_relaxed);
            mSharedPool->DecreaseRef();
            mSharedPool = nullptr;
        }
    }

    /*
     * @brief Allocate one from thread cache, this is not thread safe
     */
    template <typename T> T *Allocate()
    {
        if (NN_LIKELY(mHead.next != nullptr)) {
            /* allocate from head */
            auto tmp = mHead.next;
            mHead.next = tmp->next;
            /* assign tail to null if it is empty */
            --mCurrentFree;
            ++mDbgAllocCount;
            TraceCache();
            return reinterpret_cast<T *>(tmp);
        }

        /* allocate from shared pool */
        NN_ASSERT_LOG_RETURN(mSharedPool != nullptr, nullptr);

        if (mSharedPool->TCAlloc(mHead) != NN_OK) {
            return nullptr;
        }

        /* set current free */
        mCurrentFree = mHead.next->count - 1;

        NN_LOG_TRACE_INFO(this->ToString());

        /* move head to next and return first */
        auto tmp = mHead.next;
        mHead.next = mHead.next->next;
        ++mDbgAllocCount;
        TraceCache();
        return reinterpret_cast<T *>(tmp);
    }

    /*
     * @brief Free one to thread cache, this is not thread safe
     */
    template <typename T> void Free(T *value)
    {
        if (NN_LIKELY(value == nullptr)) {
            return;
        }

        /* insert into first */
        auto tmp = reinterpret_cast<NetMemPoolMinBlock *>(value);
        tmp->next = mHead.next;
        mHead.next = tmp;

        ++mCurrentFree;
        ++mDbgFreeCount;
        TraceCache();
        /* judge is current free count is 2 times larger than free steps
         * 1 no：just return
         * 2 yes: return many to shared pool, which means return in batch to reduce the cost of mutex in shared pool
         */
        if ((mCurrentFree >> 1) < mFreeSteps) {
            return;
        }

        /* step 1: get first */
        auto head = mHead.next;

        /* step 2: move head forward mFreeSteps and get tail */
        const uint16_t returnCount = mFreeSteps - 1;
        for (uint16_t i = 0; i < returnCount; ++i) {
            mHead.next = mHead.next->next;
        }
        head->nextN = mHead.next;

        /* step 3: move head one more */
        mHead.next = mHead.next->next;

        head->nextN->next = nullptr;
        head->count = mFreeSteps;

        /* step 4: decrease current */
        mCurrentFree -= mFreeSteps;

        NN_ASSERT_LOG_RETURN_VOID(mSharedPool != nullptr);

        /* step 5: return to share pool */
        mSharedPool->TCFree(head);
    }

    /*
     * @brief [leak-trace] the pool this thread cache was bound to at construction time.
     *        A thread_local NetTCacheFixed is constructed ONCE per thread, so this may differ
     *        from the pool the current caller intends to use.
     */
    inline NetMemPoolFixed *SharedPoolForTrace() const
    {
        return mSharedPool;
    }

    std::string ToString();

private:
    /*
     * Free all to pool
     */
    void FreeAllToPool()
    {
        NN_ASSERT_LOG_RETURN_VOID(mSharedPool != nullptr);

        if (mCurrentFree == 0) {
            return;
        }

        /* step 1: get first */
        auto head = mHead.next;

        /* step 2: move head forward mFreeSteps and get tail */
        const uint16_t returnCount = mCurrentFree - 1;
        for (uint16_t i = 0; i < returnCount; ++i) {
            mHead.next = mHead.next->next;
        }
        head->nextN = mHead.next;

        /* step 3: reset */
        mHead.next = nullptr;

        /* step 4: free */
        head->nextN->next = nullptr;
        head->count = mCurrentFree;
        mSharedPool->TCFree(head);

        NN_LOG_TRACE_INFO("Thread cache for fixed size memory pool is deconstructing, returned " << mCurrentFree <<
            " to global pool " << mSharedPool->mName);
        mCurrentFree = 0;
    }

private:
    NetMemPoolMinBlock mHead {};
    NetMemPoolFixed *mSharedPool = nullptr;
    uint16_t mCurrentFree = 0;
    uint16_t mFreeSteps = 0;

    /* [leak-trace] per-cache get/return counters, read by the TCACHE-TRACE trace (TraceCache below).
     * NOTE: the op-context pool (net_ctx_info_pool.h) and ctx store (service_ctx_store.h) go through
     * this per-thread cache ONLY when HCOM_THREAD_CACHE=1 is set (see HcomThreadCacheEnabled); by
     * default they bypass it via TCAllocOne/TCFreeOne. The op-context pool ALSO keeps its own
     * program-wide counters in HCOM_OPCTX_TRACE. These per-cache counters are still used by the
     * [TCACHE-TRACE] diagnostic for any pool that goes through NetTCacheFixed. */
    uint64_t mDbgAllocCount = 0;
    uint64_t mDbgFreeCount = 0;

    /* [tcache-trace] runtime WARN trace of this thread cache, gated by env HCOM_TCACHE_TRACE (default on).
       Reports per-cache get/return/outstanding so that pool outstanding-blk can be split into
       "blocks hoarded by thread caches" vs "blocks in live objects". */
    static bool TcacheTraceEnabled()
    {
        static const bool enabled = []() {
            const char *e = std::getenv("HCOM_TCACHE_TRACE");
            return (e == nullptr) || (e[0] != '0'); /* default ON */
        }();
        return enabled;
    }

    void TraceCache() const
    {
        if (!TcacheTraceEnabled()) {
            return;
        }
        /* throttle: log once every 0x100000 (≈1M) get+free ops on this cache, coarser than HCOM_OPCTX_TRACE */
        if (((mDbgAllocCount + mDbgFreeCount) & 0xFFFFFULL) != 0) {
            return;
        }
        const uint32_t activeCaches =
            (mSharedPool != nullptr) ? mSharedPool->mActiveCacheCount.load(std::memory_order_relaxed) : 0;
        NN_LOG_WARN("[TCACHE-TRACE] pool " << mSharedPool << " cache " << this << " get " << mDbgAllocCount
                    << ", return " << mDbgFreeCount << ", outstanding " << (mDbgAllocCount - mDbgFreeCount)
                    << ", free-steps " << mFreeSteps << ", active-caches " << activeCaches);
    }

    friend class NetMemPoolFixed;
    template<uint8_t KeyMax> friend class KeyedThreadLocalCache;
};

/// NetTCacheFixed 通常与 thread_local 一起使用，即使上层传递的 mempool 是不同的，thread_local 对象仅会初始化一次。在同
/// 一线程下，用户使用 x = Alloc(mempool1) 与 y = Alloc(mempool2), 实际两次分配的对象都会先尝试从 thread_local
/// NetTCacheFixed 的 freelist 中获取，如果没有则从一开始初始化的 mempool1 中分配。而如果后续两者在不同线程内被归还，那
/// 么就会出现 Free(mempool2, y) 被还至 mempool2 但是它实际归属于 mempool1.
///
/// 为了解决跨线程归还的问题，每个 mempool 需要各自提供一个 key 来找到对应的 NetTCacheFixed 对象，保证在每次申请/归还内
/// 存时都使用同一内存池。
///
/// \seealso NetServiceCtxStore::GetOrReturn
/// \seealso HcomServiceCtxStore::GetOrReturn
template<uint8_t KeyMax> class KeyedThreadLocalCache {
public:
    KeyedThreadLocalCache() = default;
    ~KeyedThreadLocalCache() = default;

    template<typename T> T *Allocate(uint8_t key)
    {
        if (key > KeyMax) {
            return nullptr;
        }

        return mTCacheFixeds[key] ? mTCacheFixeds[key]->template Allocate<T>() : nullptr;
    }

    template<typename T> void Free(uint8_t key, T *ctx)
    {
        if (key > KeyMax) {
            return;
        }

        if (mTCacheFixeds[key]) {
            mTCacheFixeds[key]->template Free<T>(ctx);
        }
    }

    void UpdateIf(uint8_t key, NetMemPoolFixed *mempool)
    {
        if (key > KeyMax) {
            return;
        }

        if (!mTCacheFixeds[key] || mTCacheFixeds[key]->mSharedPool != mempool) {
            mTCacheFixeds[key].reset(new (std::nothrow) NetTCacheFixed(mempool));
        }
    }

private:
    std::array<std::unique_ptr<NetTCacheFixed>, KeyMax + 1> mTCacheFixeds;
};
}  // namespace hcom
}  // namespace ock

#endif // OCK_HCOM_NET_MEM_POOL_H
