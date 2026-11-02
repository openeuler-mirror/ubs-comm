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
#ifndef HCOM_SERVICE_V2_SERVICE_CTX_STORE_H_
#define HCOM_SERVICE_V2_SERVICE_CTX_STORE_H_

#include <algorithm>
#include <mutex>
#include <vector>

#include "common/net_mem_pool_fixed.h"
#include "hcom_def.h"
#include "hcom_ref.h"
#include "service_common.h"
#include "service_timer_trace.h"

namespace ock {
namespace hcom {

constexpr int32_t MIN_FLAT_CAPACITY = 128;
constexpr int32_t MAX_FLAT_CAPACITY = 16 * 1024 * 1024;
constexpr int32_t HASH_BUCKET_SIZE = 1024;
constexpr int32_t VERSION_SHIFT = 58;
constexpr int32_t BITS_PER_INT = 32;

class HcomServiceCtxStore {
public:
    HcomServiceCtxStore(uint32_t flatCapacity, const NetMemPoolFixedPtr &ctxPool, UBSHcomNetDriverProtocol protocol)
        : mFlatCapacity(flatCapacity),
          mCtxMemPool(ctxPool),
          mProtocol(protocol)
    {
        TraceRegister(this);
        OBJ_GC_INCREASE(HcomServiceCtxStore);
    }

    ~HcomServiceCtxStore()
    {
        /* 必须先摘出注册表，再释放内部资源，否则 census 线程可能读到已释放的 flat buckets */
        TraceUnregister(this);
        UnInitialize();
        OBJ_GC_DECREASE(HcomServiceCtxStore);
    }

    /*
     * @brief Initialize the ctx store
     *
     * @return 0 return if successful
     */
    NResult Initialize()
    {
        if (mCtxMemPool.Get() == nullptr) {
            NN_LOG_ERROR("Failed to initialize as mem pool for service context store is null");
            return SER_INVALID_PARAM;
        }

        /* validate the capacity */
        if (mFlatCapacity < MIN_FLAT_CAPACITY) {
            mFlatCapacity = MIN_FLAT_CAPACITY;
        } else if (mFlatCapacity > MAX_FLAT_CAPACITY) {
            mFlatCapacity = MAX_FLAT_CAPACITY; /* each bucket is an uint64_t, 128MB is occupied */
        }

        /* get aligned capacity */
        mFlatCapacity = 1 << (BITS_PER_INT - __builtin_clz(mFlatCapacity) - 1);
        /* get seqNo mask */
        mSeqNoMask = mFlatCapacity - 1;
        /* get version shift for move right */
        mVersionShift = __builtin_popcount(mSeqNoMask);
        /* get version and seqNo mask, as version occupied 6 bits */
        mSeqNoAndVersionMask = (1 << (mVersionShift + VERSION_BIT_WIDTH)) - 1;

        mFlatCtxBucks = new (std::nothrow) uint64_t[mFlatCapacity];
        if (mFlatCtxBucks == nullptr) {
            NN_LOG_ERROR("Failed to new service flat context buckets, probably out of memory");
            return SER_NEW_OBJECT_FAILED;
        }

        /* make physical memory allocated and set them to 0 */
        bzero(mFlatCtxBucks, sizeof(uint64_t) * mFlatCapacity);

        /* reserved hash bucket for unordered map */
        for (auto &i : mHashCtxMap) {
            i.reserve(HASH_BUCKET_SIZE);
        }

        NN_LOG_INFO("Initialized context store, flatten capacity "
                    << mFlatCapacity << ", versionAndSeqMask " << mSeqNoAndVersionMask << ", seqNoMask " << mSeqNoMask
                    << ", seqNoAndVersionIndex " << mSeqNoAndVersionIndex);

        return SER_OK;
    }

    void UnInitialize()
    {
        if (mFlatCtxBucks != nullptr) {
            delete[] mFlatCtxBucks;
            mFlatCtxBucks = nullptr;
        }
    }

    /*
     * @brief Create a seq no, and store it
     *
     * @param ctx          [in] ctx ptr to store
     * @param output       [out] seqNo created
     *
     * @return SER_OK if successful
     * SER_INVALID_PARAM if param is invalid
     * SER_STORE_SEQ_DUP if seq is duplicated in map
     */
    template <typename T>
    NResult PutAndGetSeqNo(T *ctx, uint32_t &output)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SER_INVALID_PARAM;
        }

        auto value = reinterpret_cast<uint64_t>(ctx);
        /* pre-defined variables because of goto */
        HcomSeqNo sn(0);
        uint32_t mapIndex = 0;

        /*
         * Try to get empty flat bucket 3 times,
         * if got emtpy bucket, store it in that flat bucket,
         * if not got, store it into hash map
         *
         * Note: don't do this in a loop (i.e. while), expanded code has better performance than loop
         *
         * step1: first time to get free flat bucket according to index
         */

        /* get the seqNo with increasing and mask. If the seqNo is 0, increase again */
        auto newSeqAndVersion = __sync_fetch_and_add(&mSeqNoAndVersionIndex, 1);
        if (NN_UNLIKELY(newSeqAndVersion & mSeqNoMask) == 0) {
            newSeqAndVersion = __sync_fetch_and_add(&mSeqNoAndVersionIndex, 1);
        }

        /* get seqNo and version, and mixed value with version and ctx ptr for CAS */
        auto seqNo = newSeqAndVersion & mSeqNoMask;
        uint64_t version = (newSeqAndVersion >> mVersionShift) & VERSION_MASK;
        value = (version << VERSION_SHIFT) | value;
        if (__sync_bool_compare_and_swap(&mFlatCtxBucks[seqNo], 0, value)) {
            goto STORE_IN_FLAT;
        }

        /*
         * step2: second time to get free flat bucket according to index.
         */
        newSeqAndVersion = __sync_fetch_and_add(&mSeqNoAndVersionIndex, 1);
        if (NN_UNLIKELY(newSeqAndVersion & mSeqNoMask) == 0) {
            newSeqAndVersion = __sync_fetch_and_add(&mSeqNoAndVersionIndex, 1);
        }
        seqNo = newSeqAndVersion & mSeqNoMask;
        version = (newSeqAndVersion >> mVersionShift) & VERSION_MASK;
        value = (version << VERSION_SHIFT) | value;
        if (__sync_bool_compare_and_swap(&mFlatCtxBucks[seqNo], 0, value)) {
            goto STORE_IN_FLAT;
        }

        /*
         * step3: third time to get free flat bucket according to index.
         */
        newSeqAndVersion = __sync_fetch_and_add(&mSeqNoAndVersionIndex, 1);
        if (NN_UNLIKELY(newSeqAndVersion & mSeqNoMask) == 0) {
            newSeqAndVersion = __sync_fetch_and_add(&mSeqNoAndVersionIndex, 1);
        }
        seqNo = newSeqAndVersion & mSeqNoMask;
        version = (newSeqAndVersion >> mVersionShift) & VERSION_MASK;
        value = (version << VERSION_SHIFT) | value;
        if (__sync_bool_compare_and_swap(&mFlatCtxBucks[seqNo], 0, value)) {
            goto STORE_IN_FLAT;
        }

        /* step 4: tried 3 times no luck to get an empty bucket, store in hash map. */
        mapIndex = seqNo % HASH_COUNT;
        sn.SetValue(0, static_cast<uint32_t>(version), seqNo);
        output = sn.wholeSeq;
        {
            std::lock_guard<std::mutex> guard(mHashCtxMutex[mapIndex]);
            if (NN_UNLIKELY(!mHashCtxMap[mapIndex].emplace(sn.wholeSeq, value).second)) {
                return SER_STORE_SEQ_DUP;
            }
        }
        /* [TIMER-TRACE] 在途 seqNo +1，census 用它反推"注册了却没人来取"的 timer 数量 */
        mSeqInflight.fetch_add(1, std::memory_order_relaxed);
        return SER_OK;

        /* if occupied one flat bucket within 3 times try. */
    STORE_IN_FLAT:
        sn.SetValue(1, static_cast<uint32_t>(version), seqNo);
        output = sn.wholeSeq;
        mSeqInflight.fetch_add(1, std::memory_order_relaxed);
        return SER_OK;
    }

    /*
     * @brief Store a context pointer at a given seqNo (caller already knows the seqNo)
     *
     * @param ctx     [in] context pointer to store (must not be null)
     * @param seqNo   [in] pre-assigned sequence number
     *
     * @return SER_OK if stored successfully
     *         SER_INVALID_PARAM if ctx is null or seqNo is invalid
     *         SER_STORE_SEQ_DUP if the slot is already occupied (flat) or key exists (hash)
     */
    template <typename T>
    NResult PutBySeqNo(T *ctx, uint32_t seqNo)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SER_INVALID_PARAM;
        }

        HcomSeqNo sn(0);
        sn.wholeSeq = seqNo;

        auto value = reinterpret_cast<uint64_t>(ctx);

        if (NN_LIKELY(sn.fromFlat == 1)) {
            // Flat path: must fit in capacity
            if (NN_UNLIKELY(sn.realSeq >= mFlatCapacity)) {
                return SER_INVALID_PARAM; // seqNo out of flat range
            }

            // Reconstruct the stored value: version from seqNo, ptr from ctx
            uint64_t storedValue = (static_cast<uint64_t>(sn.version) << NN_NO58) | (value & PTR_MASK);

            // Try to CAS into the bucket only if it's currently 0 (empty)
            if (__sync_bool_compare_and_swap(&mFlatCtxBucks[sn.realSeq], 0ULL, storedValue)) {
                mSeqInflight.fetch_add(1, std::memory_order_relaxed);
                return SER_OK;
            } else {
                // Slot already occupied
                return SER_STORE_SEQ_DUP;
            }
        }

        // Hash path
        uint32_t mapIndex = sn.realSeq % HASH_COUNT;
        sn.isResp = 0; // ensure consistent key

        std::lock_guard<std::mutex> guard(mHashCtxMutex[mapIndex]);
        auto result = mHashCtxMap[mapIndex].emplace(sn.wholeSeq, value);
        if (NN_LIKELY(result.second)) {
            mSeqInflight.fetch_add(1, std::memory_order_relaxed);
            return SER_OK;
        }
        return SER_STORE_SEQ_DUP;
    }

    /*
     * @brief Get the pointer of ctx with seqNo
     *
     * @param seqNo        [in] seqNo, which whole got from response and timer
     * @param out          [out] ctx ptr
     *
     * @return SER_OK if successful
     * SER_INVALID_PARAM if param is invalid
     * SER_STORE_SEQ_NO_FOUND if seq is not existed, probably removed already
     *
     */
    template <typename T>
    NResult GetBySeqNo(uint32_t seqNo, T *&out)
    {
        HcomSeqNo no(0);
        no.wholeSeq = seqNo;

        if (NN_LIKELY(no.fromFlat == 1)) {
            if (NN_UNLIKELY(no.realSeq >= mFlatCapacity)) {
                return SER_STORE_SEQ_NO_FOUND;
            }

            uint64_t current = __atomic_load_n(&mFlatCtxBucks[no.realSeq], __ATOMIC_ACQUIRE);
            uint64_t value = current & PTR_MASK;

            if (NN_UNLIKELY(value == 0)) {
                return SER_STORE_SEQ_NO_FOUND;
            }

            out = reinterpret_cast<T *>(value);
            return SER_OK;
        }

        // Hash path
        uint32_t mapIndex = no.realSeq % HASH_COUNT;
        no.isResp = 0;

        std::lock_guard<std::mutex> guard(mHashCtxMutex[mapIndex]);
        auto iter = mHashCtxMap[mapIndex].find(no.wholeSeq);
        if (NN_LIKELY(iter != mHashCtxMap[mapIndex].end())) {
            out = reinterpret_cast<T *>(iter->second & PTR_MASK);
            return SER_OK;
        }

        return SER_STORE_SEQ_NO_FOUND;
    }

    /*
     * @brief Get the pointer of ctx with seqNo and clean it
     *
     * @param seqNo        [in] seqNo, which whole got from response and timer
     * @param out          [out] ctx ptr
     *
     * @return SER_OK if successful
     * SER_INVALID_PARAM if param is invalid
     * SER_STORE_SEQ_NO_FOUND if seq is not existed, probably removed already
     *
     */
    template <typename T>
    NResult GetSeqNoAndRemove(uint32_t seqNo, T *&out)
    {
        HcomSeqNo no(0);
        no.wholeSeq = seqNo;

        if (NN_LIKELY(no.fromFlat == 1)) {
            /* create the old pointer and */
            if (NN_UNLIKELY(no.realSeq >= mFlatCapacity)) {
                return SER_STORE_SEQ_NO_FOUND;
            }
            uint64_t value = mFlatCtxBucks[no.realSeq] & PTR_MASK;
            uint64_t tmpVersion = no.version;

            /* if timeout thread already get seq no, next time will
               1、CAS OK, but get value is 0
               2、CAS ERR by version++ */
            // 因为ptr是从内存池拿出来的，所以重复的可能性很大，需要加个version验证一下
            if (__sync_bool_compare_and_swap(&mFlatCtxBucks[no.realSeq], (tmpVersion << VERSION_SHIFT) | value, 0)) {
                if (NN_UNLIKELY(value == 0)) {
                    return SER_STORE_SEQ_NO_FOUND;
                }

                out = reinterpret_cast<T *>(value);
                /* [TIMER-TRACE] 在途 seqNo -1，只有摘除成功才减，和 Put 严格配对 */
                mSeqInflight.fetch_sub(1, std::memory_order_relaxed);
                return SER_OK;
            }

            return SER_STORE_SEQ_NO_FOUND;
        }

        uint32_t mapIndex = no.realSeq % HASH_COUNT;
        no.isResp = 0;
        {
            std::lock_guard<std::mutex> guard(mHashCtxMutex[mapIndex]);
            auto iter = mHashCtxMap[mapIndex].find(no.wholeSeq);
            if (NN_LIKELY(iter != mHashCtxMap[mapIndex].end())) {
                out = reinterpret_cast<T *>(iter->second & PTR_MASK);
                mHashCtxMap[mapIndex].erase(iter);
                mSeqInflight.fetch_sub(1, std::memory_order_relaxed);
                return SER_OK;
            }
        }

        return SER_STORE_SEQ_NO_FOUND;
    }

    inline void RemoveSeqNo(uint32_t seqNo)
    {
        uintptr_t *outPtr = nullptr;
        if (NN_UNLIKELY(GetSeqNoAndRemove(seqNo, outPtr) != SER_OK)) {
            HcomSeqNo dumpSeq(seqNo);
            NN_LOG_ERROR("Failed to remove ctx with seqNo " << dumpSeq.ToString() << "as not found");
            return;
        }
    }

    /*
     * @brief Get ctx obj from mem pool
     *
     * @return ptr of obj if successful
     * nullptr if failure
     */
    template <typename T>
    inline T *GetCtxObj()
    {
        T *ctx = GetOrReturn<T>(nullptr);
        if (NN_LIKELY(ctx != nullptr)) {
            TraceMark(HcomTimerEvent::ALLOC);
        }
        return ctx;
    }

    /*
     * @brief Return ctx obj to mem pool
     *
     * @param obj          [in] ptr of obj get from pool
     */
    template <typename T>
    inline void Return(T *obj)
    {
        /* no need to check obj is nullptr, because is checked in inner function */
        if (NN_LIKELY(obj != nullptr)) {
            TraceMark(HcomTimerEvent::RETURN);
        }
        (void)GetOrReturn(obj, false);
    }

    /* ------------------------------ [TIMER-TRACE] 打点接口 ------------------------------ */

    /*
     * @brief 记录一次 timer 生命周期事件，同时累加到本 store（按通道定位）和进程级汇总
     *
     * @param event        [in] 事件类型
     */
    inline void TraceMark(HcomTimerEvent event)
    {
        /* Defensive: some callers (e.g. unit tests) fire diagnostics on a channel/ctx
           store that was never initialized (mCtxStore == nullptr). A null deref here would
           crash before any real work; in production mCtxStore is always valid so this guard
           never triggers. */
        if (NN_UNLIKELY(this == nullptr)) {
            return;
        }
        if (NN_UNLIKELY(!HcomTimerTrace::Enabled())) {
            return;
        }
        mTrace.Mark(event);
        HcomTimerTrace::Global().Mark(event);
    }

    /*
     * @brief 响应未命中 seqNo 时调用：计数 + 按采样率打印明细，避免刷屏
     *
     * @param event        [in] RESP_MISS 或 POSTED_MISS
     * @param seqNo        [in] 未命中的 seqNo
     * @param channelId    [in] 通道 id
     */
    inline void TraceMiss(HcomTimerEvent event, uint32_t seqNo, uint64_t channelId)
    {
        if (NN_UNLIKELY(this == nullptr)) {
            return;
        }
        if (NN_UNLIKELY(!HcomTimerTrace::Enabled())) {
            return;
        }
        TraceMark(event);
        const uint64_t missCount = mTrace.Get(event);
        if (missCount % HcomTimerTrace::MissSample() != 1 && HcomTimerTrace::MissSample() != 1) {
            return;
        }
        HcomSeqNo dumpSeq(seqNo);
        NN_LOG_WARN("[TIMER-TRACE] " << HcomTimerEventName(static_cast<uint32_t>(event)) << " channel " << channelId
                                     << " store " << this << " " << dumpSeq.ToString() << ", accumulated miss "
                                     << missCount << ", seq-inflight " << SeqInflight() << ", live-timer "
                                     << mTrace.LiveTimer()
                                     << ", the seqNo carried by the response does not exist in the ctx store, "
                                        "the caller reference of its timer will never be released");
    }

    /* 已注册但尚未被取走的 seqNo 数量，等价于"在途未回收的 timer" */
    inline int64_t SeqInflight() const
    {
        return mSeqInflight.load(std::memory_order_relaxed);
    }

    inline const HcomTimerTraceCounters &TraceCounters() const
    {
        return mTrace;
    }

    /* 通道 id，仅用于日志定位，由 HcomChannelImp::Initialize 设置 */
    inline void SetTraceTag(uint64_t channelId)
    {
        mTraceTag = channelId;
    }

    inline uint64_t TraceTag() const
    {
        return mTraceTag;
    }

    std::string TraceToString()
    {
        std::ostringstream oss;
        oss << "channel " << mTraceTag << ", store " << this << ", protocol " << static_cast<int>(mProtocol)
            << ", flat-capacity " << mFlatCapacity << ", seq-inflight " << SeqInflight() << ", " << mTrace.ToString();
        if (mCtxMemPool.Get() != nullptr) {
            oss << " | " << mCtxMemPool.Get()->WaterMark();
        }
        return oss.str();
    }

    /*
     * @brief 由周期线程调用：到达间隔就打印一次全量 census（每个 ctx store 一行）
     *
     * 只在超时线程 0 上调用，间隔由 HCOM_TIMER_TRACE_INTERVAL_SEC 控制（默认 60s）。
     */
    static void MaybeDumpAll()
    {
        if (NN_UNLIKELY(!HcomTimerTrace::Enabled())) {
            return;
        }

        const uint64_t now = NetMonotonic::TimeSec();
        uint64_t last = TraceLastDumpSec().load(std::memory_order_relaxed);
        if (now < last + HcomTimerTrace::IntervalSec()) {
            return;
        }
        /* CAS 保证多线程同时到点时只有一个线程真正打印 */
        if (!TraceLastDumpSec().compare_exchange_strong(last, now, std::memory_order_relaxed)) {
            return;
        }

        DumpAll("periodic");
    }

    /*
     * @brief 打印所有存活 ctx store 的 timer 计数快照
     *
     * @param reason       [in] 触发原因，便于在日志里区分周期打印和主动打印
     */
    static void DumpAll(const char *reason)
    {
        NN_LOG_INFO("[TIMER-TRACE] census (" << reason << ") build=" << HcomTimerDiagVersion()
                                             << " global: " << HcomTimerTrace::Global().ToString());

        std::lock_guard<std::mutex> guard(TraceRegistryLock());
        for (auto *store : TraceRegistry()) {
            if (store == nullptr) {
                continue;
            }
            const std::string line = store->TraceToString();
            if (store->SeqInflight() >= HcomTimerTrace::InflightWarn()) {
                NN_LOG_WARN("[TIMER-TRACE] census (" << reason << ") " << line
                                                     << ", seq-inflight exceeds threshold, suspected timer leak");
            } else {
                NN_LOG_INFO("[TIMER-TRACE] census (" << reason << ") " << line);
            }
        }
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    /*
     * 存活 ctx store 注册表。构造时登记、析构时摘除，census 遍历时持锁，
     * 保证不会读到已经析构的 store。用 leaky 单例避免进程退出时的静态析构顺序问题
     * （store 可能比函数内 static 容器活得更久）。
     */
    static std::mutex &TraceRegistryLock()
    {
        static std::mutex *lock = new std::mutex();
        return *lock;
    }

    static std::vector<HcomServiceCtxStore *> &TraceRegistry()
    {
        static std::vector<HcomServiceCtxStore *> *registry = new std::vector<HcomServiceCtxStore *>();
        return *registry;
    }

    static std::atomic<uint64_t> &TraceLastDumpSec()
    {
        static std::atomic<uint64_t> lastSec(0);
        return lastSec;
    }

    static void TraceRegister(HcomServiceCtxStore *store)
    {
        std::lock_guard<std::mutex> guard(TraceRegistryLock());
        TraceRegistry().emplace_back(store);
    }

    static void TraceUnregister(HcomServiceCtxStore *store)
    {
        std::lock_guard<std::mutex> guard(TraceRegistryLock());
        auto &registry = TraceRegistry();
        auto iter = std::find(registry.begin(), registry.end(), store);
        if (iter != registry.end()) {
            (void)registry.erase(iter);
        }
    }

private:
    /*
     * POLICY-DRIVEN allocation for timer/ctx objects. When the pool's
     * NetMemPoolTlsPolicy::enabled is true, use the per-protocol KeyedThreadLocalCache
     * (UpdateIf binds each protocol to the correct pool, avoiding cross-pool alias);
     * otherwise take the DEFAULT bypass path (TCAllocOne/TCFreeOne straight to the shared
     * free-list). The bypass is the safe default because these objects are allocated on one
     * thread and freed on another (timer/timeout thread); a per-thread cache would pin the
     * blocks forever. The [TIMER-TRACE] census (alloc/return/live-timer + pool
     * outstanding-blk) reports both sides.
     */
    template <typename T>
    inline T *GetOrReturn(T *returnCtx, bool get = true)
    {
        auto *pool = mCtxMemPool.Get();
        if (NN_UNLIKELY(pool == nullptr)) {
            return nullptr;
        }

        if (pool->TlsPolicy().enabled) {
            static thread_local KeyedThreadLocalCache<UBSHcomNetDriverProtocol::UBC> threadCache;
            threadCache.UpdateIf(mProtocol, pool);
            if (get) {
                return threadCache.Allocate<T>(mProtocol);
            }
            threadCache.Free<T>(mProtocol, returnCtx);
            return nullptr;
        }

        /* DEFAULT bypass: ctx allocated on one thread, freed on another (timer thread).
           Go straight to the shared free-list; no per-thread cache, no cross-thread pinning. */
        if (get) {
            return pool->TCAllocOne<T>();
        }
        pool->TCFreeOne<T>(returnCtx);
        return nullptr;
    }

private:
    static constexpr uint32_t VERSION_MASK = 0x3F;           /* mask to reverse version */
    static constexpr uint32_t VERSION_BIT_WIDTH = 6;         /* mask to reverse version */
    static constexpr uint32_t HASH_COUNT = 4;                /* hash map count */
    static constexpr uint64_t PTR_MASK = 0x03FFFFFFFFFFFFFF; /* ptr mask */

private:
    /* Note:
     * 1 make sure those frequently accessed variables are at first place
     * 2 make sure those variables are aligned
     * 3 make sure total size of those variables are less than the size of 1 cache line
     */
    uint32_t mSeqNoAndVersionIndex = 1;       /* atomic increase seqNo and version */
    uint32_t mSeqNoAndVersionMask = 0;        /* mask to reverse the seqNo and version */
    uint32_t mSeqNoMask = 0;                  /* mask to reverse the seqNo */
    uint32_t mVersionShift = 0;               /* move right shift num to get version */
    uint32_t mFlatCapacity = 8192;            /* flat array capacity */
    uint64_t *mFlatCtxBucks = nullptr;        /* actually array to store the ptr */
    NetMemPoolFixedPtr mCtxMemPool = nullptr; /* memory pool of context */

    std::mutex mHashCtxMutex[HASH_COUNT];                           /* mutex to guard unordered_map */
    std::unordered_map<uint32_t, uint64_t> mHashCtxMap[HASH_COUNT]; /* unordered_map to store un-flat */
    UBSHcomNetDriverProtocol mProtocol = UBSHcomNetDriverProtocol::UNKNOWN;

    /* [TIMER-TRACE] 泄漏定位打点，不参与任何业务逻辑 */
    HcomTimerTraceCounters mTrace;        /* 本 store（即本通道）的分事件累计计数 */
    std::atomic<int64_t> mSeqInflight{0}; /* 已注册但尚未取走的 seqNo 数量 */
    uint64_t mTraceTag = 0;               /* 通道 id，仅日志用 */

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
} // namespace hcom
} // namespace ock

#endif // HCOM_SERVICE_V2_SERVICE_CTX_STORE_H_
