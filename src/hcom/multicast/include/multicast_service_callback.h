/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_SERVICE_CALLBACK_H
#define HCOM_MULTICAST_SERVICE_CALLBACK_H

#include "hcom_service.h"
#include "multicast_publisher.h"
#include "net_monotonic.h"
#include "service_ctx_store.h"

namespace ock {
namespace hcom {
class MultiCastTimerListHeader;
enum class MultiCastAsyncCBState
{
    INIT = 0,
    FINISHED = 1,
    TIMEOUT = 2,
};

enum class MultiCastSyncCBType
{
    IO = 0,
    BROKEN = 1,
};

class MultiCastServiceTimer {
public:
    Publisher *mPublisher = nullptr;
    HcomServiceCtxStore *mCtxStore = nullptr;                   /* manager memory and seqNo */
    uint64_t mTimeout = 0;                                      /* absolute timeout compare to current system time */
    uintptr_t mCallback = 0;                                    /* callback obj address */
    uint32_t mSeqNo = 0;                                        /* seq no for find query map */
    MultiCastSyncCBType mType = MultiCastSyncCBType::IO;        /* callback type */
    MultiCastAsyncCBState mState = MultiCastAsyncCBState::INIT; /* accessed through atomic builtins */
public:
    inline void SeqNo(uint32_t seqNo)
    {
        mSeqNo = seqNo;
    }

    inline uint32_t SeqNo() const
    {
        return mSeqNo;
    }

    inline uint64_t Timeout() const
    {
        return mTimeout;
    }

    inline uintptr_t Callback() const
    {
        return mCallback;
    }

    inline MultiCastAsyncCBState State() const
    {
        return __atomic_load_n(&mState, __ATOMIC_ACQUIRE);
    }

    inline void TimeoutDump() const
    {
        if (mType == MultiCastSyncCBType::IO) {
            NN_LOG_WARN("IO timeout, seq no " << mSeqNo);
        }
    }

    inline void BrokenDump() const
    {
        if (mType == MultiCastSyncCBType::IO) {
            NN_LOG_WARN("IO in Broken, seq no " << mSeqNo);
        }
    }

    inline void EraseSeqNo() const
    {
        NN_ASSERT_LOG_RETURN_VOID(mCtxStore != nullptr);

        class MultiCastServiceTimer *timer = nullptr;
        if (NN_UNLIKELY(mCtxStore->GetSeqNoAndRemove(mSeqNo, timer) != SER_OK)) {
            HcomSeqNo dumpSeq(mSeqNo);
            NN_LOG_ERROR("Failed to erase " << dumpSeq.ToString());
            return;
        }

        if (NN_UNLIKELY(timer != this)) {
            HcomSeqNo dumpSeq(mSeqNo);
            NN_LOG_ERROR(dumpSeq.ToString() << " erase wrong timer");
            return;
        }
    }

    inline bool EraseSeqNoWithRet() const
    {
        NN_ASSERT_LOG_RETURN(mCtxStore != nullptr, false)

        class MultiCastServiceTimer *timer = nullptr;
        if (NN_UNLIKELY(mCtxStore->GetSeqNoAndRemove(mSeqNo, timer) != SER_OK)) {
            return false;
        }

        /* first time, before CAS, flat buff = valid address(this); after CAS, flat buff = 0, timer = valid address
           second time, before CAS, flat buff = 0; after CAS, flat buff = 0, timer = 0 */
        if (NN_UNLIKELY(timer != this)) {
            return false;
        }

        return true;
    }

    inline bool IsFinished() const
    {
        return State() == MultiCastAsyncCBState::FINISHED;
    }

    /*
     * @brief Mark the CB wrapper to finished, which should be called by polling thread or user caller thread
     *
     * @return true if mark the state from init to FINISHED state
     * otherwise it is timeout
     */
    inline void MarkFinished()
    {
        __atomic_store_n(&mState, MultiCastAsyncCBState::FINISHED, __ATOMIC_RELEASE);
    }

    inline void RunCallBack(PublisherContext &ctx)
    {
        if (mCallback != 0) {
            auto callback = reinterpret_cast<MultiCastCallback *>(mCallback);
            mCallback = 0;
            callback->Run(ctx);
        }
    }

    inline void DeleteCallBack()
    {
        if (mCallback != 0) {
            auto callback = reinterpret_cast<MultiCastCallback *>(mCallback);
            mCallback = 0;
            callback->Destroy();
        }
    }

    /*
     * @brief Mark the CB wrapper to timeout, which should be called by timeout thread
     *
     * @return true if mark the state from init to FINISHED state
     * otherwise it is timeout
     */
    inline void MarkTimeout()
    {
        __atomic_store_n(&mState, MultiCastAsyncCBState::TIMEOUT, __ATOMIC_RELEASE);
    }

    bool IsTimeOut() const
    {
        // when mTimeout is 0, this timer will never timeout
        if (mTimeout == 0) {
            return false;
        }
        if (NetMonotonic::TimeSec() > mTimeout) {
            return true;
        }

        return false;
    }

    MultiCastServiceTimer(Publisher *publisher, HcomServiceCtxStore *ctxStore, int32_t t, uintptr_t cb,
                          MultiCastSyncCBType type)
        : mPublisher(publisher),
          mCtxStore(ctxStore),
          mCallback(cb),
          mType(type)
    {
        // if t < 0, it means never timeout, so leave mTimeout as 0
        if (t >= 0) {
            mTimeout = NetMonotonic::TimeSec() + static_cast<uint64_t>(t);
        }

        if (mPublisher != nullptr) {
            mPublisher->IncreaseRef();
        }

        OBJ_GC_INCREASE(MultiCastServiceTimer);
    }

    MultiCastServiceTimer()
    {
        OBJ_GC_INCREASE(MultiCastServiceTimer);
    }

    ~MultiCastServiceTimer() = default;

public:
    inline void IncreaseRef()
    {
        __sync_fetch_and_add(&mRefCount, 1);
    }

    inline void DecreaseRef()
    {
        int32_t tmp = __sync_sub_and_fetch(&mRefCount, 1);
        if (tmp == 0) {
            if (mPublisher != nullptr) {
                mPublisher->DecreaseRef();
            }

            if (mCtxStore != nullptr) {
                mCtxStore->Return(this);
            }

            OBJ_GC_DECREASE(MultiCastServiceTimer);
        }
    }

    inline int32_t GetRef()
    {
        return __sync_sub_and_fetch(&mRefCount, 0);
    }

    friend class MultiCastTimerListHeader;
    friend class MultiCastPeriodicManager;

private:
    int32_t mRefCount = 0;
    class MultiCastServiceTimer *mPrev = nullptr;
    class MultiCastServiceTimer *mNext = nullptr;
    class MultiCastServiceTimer *mPeriodicPrev = nullptr;
    class MultiCastServiceTimer *mPeriodicNext = nullptr;
    uint16_t mPeriodicThreadId = 0;
    uint16_t mPeriodicQueueId = 0;
    bool mInPeriodicQueue = false;
};

class MultiCastTimerListHeader {
public:
    MultiCastTimerListHeader() = default;

    /*
     * @brief add timer ctx in linked list
     * @note increase ref
     */
    inline void AddTimerCtx(MultiCastServiceTimer *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return;
        }

        TimerListShard &shard = GetShard(ctx);
        shard.lock.Lock();
        ctx->mPrev = nullptr;
        ctx->mNext = shard.head;
        if (shard.head != nullptr) {
            shard.head->mPrev = ctx;
        }
        shard.head = ctx;
        ++shard.count;
        shard.lock.Unlock();
        ctx->IncreaseRef();
    }

    /*
     * @brief remove timer ctx in linked list
     * @note if remove success, decrease ref
     */
    inline void RemoveTimerCtx(MultiCastServiceTimer *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return;
        }

        TimerListShard &shard = GetShard(ctx);
        shard.lock.Lock();
        if (ctx->mPrev == nullptr) {
            if (shard.head != ctx) {
                shard.lock.Unlock();
                return;
            }
            shard.head = ctx->mNext;
        } else {
            ctx->mPrev->mNext = ctx->mNext;
        }
        if (ctx->mNext != nullptr) {
            ctx->mNext->mPrev = ctx->mPrev;
        }
        --shard.count;
        ctx->mPrev = nullptr;
        ctx->mNext = nullptr;
        shard.lock.Unlock();
        ctx->DecreaseRef();
    }

    /*
     * @brief get timer ctx in linked list
     * @note outside need decrease ref
     */
    inline void GetTimerCtx(std::vector<MultiCastServiceTimer *> &remainCtx)
    {
        remainCtx.clear();
        for (uint32_t index = 0; index < TIMER_LIST_SHARD_NUM; ++index) {
            TimerListShard &shard = mShards[index];
            shard.lock.Lock();
            MultiCastServiceTimer *remain = shard.head;
            shard.head = nullptr;
            shard.count = 0;
            while (remain != nullptr) {
                MultiCastServiceTimer *next = remain->mNext;
                remain->mNext = nullptr;
                remain->mPrev = nullptr;
                remainCtx.emplace_back(remain);
                remain = next;
            }
            shard.lock.Unlock();
        }
    }

    inline uint32_t GetCtxCount()
    {
        uint32_t count = 0;
        for (uint32_t index = 0; index < TIMER_LIST_SHARD_NUM; ++index) {
            TimerListShard &shard = mShards[index];
            shard.lock.Lock();
            count += shard.count;
            shard.lock.Unlock();
        }
        return count;
    }

private:
    static constexpr uint32_t TIMER_LIST_SHARD_NUM = 16;

    struct TimerListShard {
        MultiCastServiceTimer *head = nullptr;
        NetSpinLock lock;
        uint32_t count = 0;
    };

    inline TimerListShard &GetShard(const MultiCastServiceTimer *ctx)
    {
        return mShards[ctx->SeqNo() % TIMER_LIST_SHARD_NUM];
    }

    TimerListShard mShards[TIMER_LIST_SHARD_NUM];
};

struct MultiCastTimerContext {
    uint32_t seqNo = 0;
    MultiCastServiceTimer *timer = nullptr;

    MultiCastTimerContext() = default;
};

class MultiCastServiceTimerCompare {
public:
    bool operator()(MultiCastServiceTimer *&a, MultiCastServiceTimer *&b) const
    {
        if (a->Timeout() > b->Timeout()) {
            return true;
        } else if (a->Timeout() == b->Timeout()) {
            return a->SeqNo() > b->SeqNo();
        } else {
            return false;
        }
    }
};
} // namespace hcom
} // namespace ock

#endif // HCOM_MULTICAST_SERVICE_CALLBACK_H
