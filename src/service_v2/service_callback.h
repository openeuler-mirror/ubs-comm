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
#ifndef HCOM_SERVICE_V2_SERVICE_CALLBACK_H_
#define HCOM_SERVICE_V2_SERVICE_CALLBACK_H_

#include "api/hcom_service_channel.h"
#include "service_ctx_store.h"
#include "service_common.h"
#include "net_monotonic.h"

namespace ock {
namespace hcom {
class SerTimerListHeader;

enum class HcomAsyncCBState : uint8_t {
    CBS_INIT = 0,
    CBS_FINISHED = 1,
    CBS_TIMEOUT = 2,
};

enum class HcomAsyncCBType : uint8_t {
    CBS_IO = 0,
    CBS_CHANNEL_BROKEN = 1,
};

class HcomServiceTimer {
public:
    UBSHcomChannel *mChannel = nullptr;          /* used for build UBSHcomServiceContext */
    HcomServiceCtxStore *mCtxStore = nullptr; /* manager memory and seqNo */
    uint64_t mTimeout = 0;                   /* absolute timeout compare to current system time */
    uintptr_t mCallback = 0;                 /* callback obj address */
    uint32_t mSeqNo = 0;                     /* seq no for find query map */
    HcomAsyncCBType mType = HcomAsyncCBType::CBS_IO;      /* callback type */
    HcomAsyncCBState mState = HcomAsyncCBState::CBS_INIT; /* atomic status to handle the trace condition between timeout
                                              * handle thread and polling thread */
public:
    inline uint32_t SeqNo() const
    {
        return mSeqNo;
    }

    inline void SeqNo(uint32_t seqNo)
    {
        mSeqNo = seqNo;
    }

    inline HcomAsyncCBState State() const
    {
        return mState;
    }

    inline uint64_t Timeout() const
    {
        return mTimeout;
    }

    inline uintptr_t Callback() const
    {
        return mCallback;
    }

    inline void TimeoutDump() const
    {
        if (mType == HcomAsyncCBType::CBS_IO) {
            if (mChannel == nullptr) {
                NN_LOG_WARN("IO timeout, seq no " << mSeqNo);
            } else {
                NN_LOG_WARN("IO timeout, seq no " << mSeqNo << " in channel id " << mChannel->GetId());
            }
        }
    }

    inline void EraseSeqNo() const
    {
        NN_ASSERT_LOG_RETURN_VOID(mCtxStore != nullptr);

        class HcomServiceTimer *timer = nullptr;
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
        NN_ASSERT_LOG_RETURN(mCtxStore != nullptr, false);

        class HcomServiceTimer *timer = nullptr;
        if (NN_UNLIKELY(mCtxStore->GetSeqNoAndRemove(mSeqNo, timer) != SER_OK)) {
            return false;
        }

        /* first time: before CAS, flat buff = valid address(this); after CAS, flat buff = 0, timer = valid address
           second time: before CAS, flat buff = 0; after CAS, flat buff = 0, timer = 0 */
        if (NN_UNLIKELY(timer != this)) {
            return false;
        }

        return true;
    }

    inline bool IsFinished() const
    {
        return mState == HcomAsyncCBState::CBS_FINISHED;
    }

    /*
     * @brief Mark the CB wrapper to finished, which should be called by polling thread or user caller thread
     *
     * @return true if mark the state from init to FINISHED state
     * otherwise it is timeout
     */
    inline void MarkFinished()
    {
        mState = HcomAsyncCBState::CBS_FINISHED;
    }

    inline void RunCallBack(UBSHcomServiceContext &ctx)
    {
        if (mCallback != 0) {
            auto callback = reinterpret_cast<ock::hcom::Callback *>(mCallback);
            mCallback = 0;
            callback->Run(ctx);
        }
    }

    inline void DeleteCallBack()
    {
        if (mCallback != 0) {
            auto callback = reinterpret_cast<ock::hcom::Callback *>(mCallback);
            mCallback = 0;
            delete callback;
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
        mState = HcomAsyncCBState::CBS_TIMEOUT;
    }

    bool IsTimeOut() const
    {
        // if mTimeout is 0, this timer will never timeout
        if (mTimeout == 0) {
            return false;
        }
        if (NetMonotonic::TimeSec() > mTimeout) {
            return true;
        }
        return false;
    }

    HcomServiceTimer(UBSHcomChannel *ch, HcomServiceCtxStore *ctxStore, int32_t t, uintptr_t cb, HcomAsyncCBType type)
        : mChannel(ch), mCtxStore(ctxStore), mCallback(cb), mType(type)
    {
        // if t < 0, it means never timeout, so leave mTimeout as 0
        if (t >= 0) {
            mTimeout = NetMonotonic::TimeSec() + static_cast<uint64_t>(t);
        }

        if (mChannel != nullptr) {
            mChannel->IncreaseRef();
        }

        OBJ_GC_INCREASE(HcomServiceTimer);
    }

    HcomServiceTimer()
    {
        OBJ_GC_INCREASE(HcomServiceTimer);
    }

    ~HcomServiceTimer() {}

public:
    inline void IncreaseRef()
    {
        __sync_fetch_and_add(&mRefCount, 1);
    }

    inline void DecreaseRef()
    {
        int32_t tmpCnt = __sync_sub_and_fetch(&mRefCount, 1);
        if (tmpCnt == 0) {
            if (mChannel != nullptr) {
                mChannel->DecreaseRef();
            }

            if (mCtxStore != nullptr) {
                mCtxStore->Return(this);
            }

            OBJ_GC_DECREASE(HcomServiceTimer);
        }
    }

    inline int32_t GetRef()
    {
        return __sync_sub_and_fetch(&mRefCount, 0);
    }

    friend class SerTimerListHeader;

private:
    int32_t mRefCount = 0;
    class HcomServiceTimer *mPrev = nullptr;
    class HcomServiceTimer *mNext = nullptr;
};

class SerTimerListHeader {
public:
    SerTimerListHeader() = default;

    /*
     * @brief add timer ctx in linked list
     * @note increase ref
     */
    inline void AddTimerCtx(HcomServiceTimer *timer)
    {
        if (NN_LIKELY(timer != nullptr)) {
            // bi-direction linked list, 4 step to insert to head
            timer->mPrev = &mTimerCtx;
            mLock.Lock();
            // head -><- first -><- second -><- third -> nullptr
            // insert into the head place
            timer->mNext = mTimerCtx.mNext;
            if (mTimerCtx.mNext != nullptr) {
                mTimerCtx.mNext->mPrev = timer;
            }
            mTimerCtx.mNext = timer;
            ++mCtxCount;
            mLock.Unlock();
            timer->IncreaseRef();
        }
    }

    /*
     * @brief remove timer ctx in linked list
     * @note if remove success, decrease ref
     */
    inline void RemoveTimerCtx(HcomServiceTimer *timer)
    {
        if (NN_LIKELY(timer != nullptr)) {
            // bi-direction linked list, 4 step to remove one
            mLock.Lock();

            // repeat remove
            if (timer->mPrev == nullptr) {
                mLock.Unlock();
                return;
            }

            // head-><- first -><- second -><- third -> nullptr
            timer->mPrev->mNext = timer->mNext;

            if (timer->mNext != nullptr) {
                timer->mNext->mPrev = timer->mPrev;
            }
            --mCtxCount;

            timer->mPrev = nullptr;
            timer->mNext = nullptr;
            mLock.Unlock();
            timer->DecreaseRef();
        }
    }

    /*
     * @brief get timer ctx in linked list
     * @note outside need decrease ref
     */
    inline void GetTimerCtx(std::vector<HcomServiceTimer *> &remainCtx)
    {
        HcomServiceTimer *timer = nullptr;
        HcomServiceTimer *next = nullptr;
        remainCtx.clear();
        remainCtx.reserve(mCtxCount);

        mLock.Lock();
        // head -> first -><- second -><- third -> nullptr
        timer = mTimerCtx.mNext;
        mTimerCtx.mNext = nullptr;
        mCtxCount = 0;

        while (timer != nullptr) {
            next = timer->mNext;
            timer->mNext = nullptr;
            timer->mPrev = nullptr;
            remainCtx.emplace_back(timer);
            timer = next;
        }
        mLock.Unlock();
    }

    inline uint32_t GetCtxCount()
    {
        mLock.Lock();
        auto tmpCnt = mCtxCount;
        mLock.Unlock();
        return tmpCnt;
    }

public:
    HcomServiceTimer mTimerCtx {};
    NetSpinLock mLock;
    uint32_t mCtxCount = 0;
};

class HcomServiceTimerCompare {
public:
    bool operator () (HcomServiceTimer *&a, HcomServiceTimer *&b) const
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

}
}
#endif // HCOM_SERVICE_V2_SERVICE_CALLBACK_H_