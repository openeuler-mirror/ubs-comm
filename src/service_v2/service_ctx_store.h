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

#include "hcom_def.h"
#include "hcom_ref.h"
#include "service_common.h"
#include "common/net_mem_pool_fixed.h"

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
        : mFlatCapacity(flatCapacity), mCtxMemPool(ctxPool), mProtocol(protocol)
    {
        OBJ_GC_INCREASE(HcomServiceCtxStore);
    }

    ~HcomServiceCtxStore()
    {
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
            return mHashCtxMap[mapIndex].emplace(sn.wholeSeq, value).second ? SER_OK : SER_STORE_SEQ_DUP;
        }

        /* if occupied one flat bucket within 3 times try. */
    STORE_IN_FLAT:
        sn.SetValue(1, static_cast<uint32_t>(version), seqNo);
        output = sn.wholeSeq;
        return SER_OK;
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
        return GetOrReturn<T>(nullptr);
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
        (void)GetOrReturn(obj, false);
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    /* alloc/free in the same function to make sure use the same thread_local variable */
    template <typename T>
    inline T *GetOrReturn(T *returnCtx, bool get = true)
    {
        static thread_local KeyedThreadLocalCache<UBSHcomNetDriverProtocol::UBC> threadCache;
        // 有 2 种场景需要更新:
        // - 第一次运行，初始值为 `nullptr`, 需要更新成当前在用的内存池
        // - 主线程不退出，开始时先启动了 Service1, 主线程中进行 Send 会使用 Service1 的内存池；而后 Service1 退出、内存
        //   池回收，主线程中的 `thread_local` cache 仍保存的是 Service1 的内存池地址。在新启动 Service2 后，如果在主线
        //   程中进行 Send 会更新 `thread_local` cache 指向的内存池。此时原有 Service1 的内存池才会真正被归还至 OS.
        //
        // 注意：上层应当**禁止同时创建同种协议的 2 个不同 Service 实例**，否则此处仍旧会出现 Service2 引用 Service1 内
        // 存池中的地址。
        threadCache.UpdateIf(mProtocol, mCtxMemPool.Get());

        if (get) {
            return threadCache.Allocate<T>(mProtocol);
        } else {
            threadCache.Free<T>(mProtocol, returnCtx);
            return nullptr;
        }
    }

private:
    static constexpr uint32_t VERSION_MASK = 0x3F;             /* mask to reverse version */
    static constexpr uint32_t VERSION_BIT_WIDTH = 6;            /* mask to reverse version */
    static constexpr uint32_t HASH_COUNT = 4;                  /* hash map count */
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

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}  // namespace hcom
}  // namespace ock

#endif  // HCOM_SERVICE_V2_SERVICE_CTX_STORE_H_
