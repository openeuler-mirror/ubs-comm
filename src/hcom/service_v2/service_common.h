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
#ifndef HCOM_SERVICE_V2_SERVICE_COMMON_H_
#define HCOM_SERVICE_V2_SERVICE_COMMON_H_
#include <condition_variable>
#include "securec.h"

#include "hcom_service_channel.h"
#include "hcom_service_def.h"
#include "hcom_service_context.h"
#include "net_common.h"
#include "net_crc32.h"
#include "net_trace.h"
#include "net_monotonic.h"

namespace ock {
namespace hcom {
constexpr uint32_t CHANNEL_EP_MAX_NUM = 64;

enum ServiceV2PrivateOpcode : uint16_t {
    RNDV_CALL_OP_V2 = 1001,
    EXCHANGE_TIMESTAMP_OP = 1002,
};

struct SerTransContext {
    uint32_t seqNo = 0;
    bool invokeCallback = true; // call() message is no need to invoke callback.

    Callback *callback = nullptr; /* record for response message quick handle */
    SerTransContext() = default;
} __attribute__((packed));

inline void SetServiceTransCtx(char *ctxData, uint32_t seqNo)
{
    auto ctx = reinterpret_cast<SerTransContext *>(ctxData);
    ctx->seqNo = seqNo;
    ctx->invokeCallback = true;
}

inline void SetServiceTransCtx(char *ctxData, Callback *callback)
{
    auto ctx = reinterpret_cast<SerTransContext *>(ctxData);
    ctx->callback = callback;
    ctx->invokeCallback = true;
}

inline void SetServiceTransCtx(char *ctxData, uint32_t seqNo, bool invokeCallback)
{
    auto ctx = reinterpret_cast<SerTransContext *>(ctxData);
    ctx->seqNo = seqNo;
    ctx->invokeCallback = invokeCallback;
}

inline Callback *GetServiceTransCb(char *ctxData)
{
    auto ctx = reinterpret_cast<SerTransContext *>(ctxData);
    return ctx->callback;
}

inline uint32_t GetServiceTransSeqNo(char *ctxData)
{
    auto context = reinterpret_cast<SerTransContext *>(ctxData);
    return context->seqNo;
}

inline bool GetServiceTransNeedPostedCall(char *ctxData)
{
    auto context = reinterpret_cast<SerTransContext *>(ctxData);
    return context->invokeCallback;
}

/* if result is OK, there is no need to invoke callback in Call() method, waiting for remote rsp */
inline bool IsNeedInvokeCallback(const UBSHcomRequestContext &ctx)
{
    if (ctx.Result() != NN_OK) {
        return true;
    }

    if (ctx.OpType() == UBSHcomRequestContext::NN_SENT ||
        ctx.OpType() == UBSHcomRequestContext::NN_SENT_RAW ||
        ctx.OpType() == UBSHcomRequestContext::NN_WRITTEN ||
        ctx.OpType() == UBSHcomRequestContext::NN_READ) {
        return GetServiceTransNeedPostedCall(const_cast<char *>(ctx.OriginalRequest().upCtxData));
    } else if (ctx.OpType() == UBSHcomRequestContext::NN_SENT_RAW_SGL ||
               ctx.OpType() == UBSHcomRequestContext::NN_SGL_WRITTEN ||
               ctx.OpType() == UBSHcomRequestContext::NN_SGL_READ) {
        return GetServiceTransNeedPostedCall(const_cast<char *>(ctx.OriginalSgeRequest().upCtxData));
    } else {
        NN_LOG_ERROR("Invalid op type " << ctx.OpType() << " for request posted");
        return false;
    }
}

/**
 * @brief Generate a permanent callback object.
 *
 * @param Args
 * @param args
 * @return Callback*
 * @note see @ref NewCallback.
 */
template <typename... Args> Callback *NewPermanentCallback(Args... args)
{
    auto closure = std::bind(args...);
    return new (std::nothrow) InnerClosureCallback<decltype(closure)>(std::move(closure), false);
}

class AsyncClosureCallback : public Callback {
public:
    explicit AsyncClosureCallback(Callback *function, uint16_t mFinishCnt)
        : mFunction(function), mTotalTime(mFinishCnt)
    {}

    void Run(UBSHcomServiceContext &context) override
    {
        __sync_fetch_and_add(&mRunTime, 1);
        if (mRunTime < mTotalTime) {
            return;
        }
        if (mFunction != nullptr) {
            mFunction->Run(context);
        }
        delete this;
    }

private:
    uint64_t GetTime() override
    {
        return mStartTime;
    }

    void SetTime(uint64_t time) override
    {
        mStartTime = time;
    }

private:
    Callback *mFunction = nullptr;
    uint16_t mRunTime = 0;
    uint16_t mTotalTime = 0;
    uint64_t mStartTime = 0;
};

enum class ConnectingEpState {
    NEW_EP,
    NEW_CHANNEL,
    EP_BROKEN,
};

struct InnerConnectOptions {
    uint16_t clientGroupId;
    uint16_t serverGroupId;
    uint8_t linkCount;
    UBSHcomClientPollingMode mode;
    UBSHcomChannelCallBackType cbType;
};

struct SerConnInfo {
    uint32_t crc = 0;
    uint32_t version = 0;
    uint64_t channelId = 0;
    uint64_t multiRailId = 0;
    uint16_t index = 0;
    uint16_t driverIndex = 0;
    uint16_t driverSize = 0;
    uint16_t totalLinkCount = 0;
    UBSHcomChannelBrokenPolicy policy = UBSHcomChannelBrokenPolicy::BROKEN_ALL;
    InnerConnectOptions options;

    SerConnInfo() = default;
    SerConnInfo(uint32_t v, uint64_t id, UBSHcomChannelBrokenPolicy p, const UBSHcomConnectOptions &opt)
        : version(v), channelId(id), policy(p)
    {
        options.clientGroupId = opt.clientGroupId;
        options.serverGroupId = opt.serverGroupId;
        options.linkCount = opt.linkCount;
        totalLinkCount = opt.linkCount;
        options.mode = opt.mode;
        options.cbType = opt.cbType;
    }
    SerConnInfo(uint32_t v, uint64_t id, uint16_t driverSize, UBSHcomChannelBrokenPolicy p,
        const UBSHcomConnectOptions &opt)
        : version(v),
          channelId(id),
          driverSize(driverSize),
          policy(p)
    {
        options.clientGroupId = opt.clientGroupId;
        options.serverGroupId = opt.serverGroupId;
        options.linkCount = opt.linkCount;
        totalLinkCount = opt.linkCount;
        options.mode = opt.mode;
        options.cbType = opt.cbType;
        multiRailId = id;
    }

    inline void SetCrc32()
    {
        auto crcAddress = reinterpret_cast<uint8_t *>(this) + sizeof(uint32_t);
        crc = NetCrc32::CalcCrc32(crcAddress, sizeof(SerConnInfo) - sizeof(uint32_t));
    }

    inline bool Validate()
    {
        auto crcAddress = reinterpret_cast<uint8_t *>(this) + sizeof(uint32_t);
        uint32_t newCrc = NetCrc32::CalcCrc32(crcAddress, sizeof(SerConnInfo) - sizeof(uint32_t));

        return crc == newCrc;
    }

    inline bool ToString(std::string &out)
    {
        return BuffToHexString(this, sizeof(SerConnInfo), out);
    }

    static SerResult Deserialize(const std::string &payload, SerConnInfo &connInfo, std::string &userPayLoad);
    static SerResult Serialize(SerConnInfo &connInfo, const std::string &payload, std::string &out);
};

class ConnectingSecInfo {
public:
    int64_t flag = 0;
    uint32_t secContentLen = 0;
    char *secContent = nullptr;
    UBSHcomNetDriverSecType type = NET_SEC_VALID_ONE_WAY;
    bool needAutoFree = false;
    bool firstCallProvider = true;
    bool firstCallValidator = true;

    ConnectingSecInfo() = default;

    void Initialize(int64_t flg, UBSHcomNetDriverSecType secType, char *output, uint32_t len, bool autoFree)
    {
        flag = flg;
        type = secType;
        secContent = output;
        secContentLen = len;
        needAutoFree = autoFree;
        firstCallProvider = false;
    }
};

class HcomConnectingEpInfo {
public:
    HcomConnectingEpInfo() = default;
    HcomConnectingEpInfo(std::string &id, const UBSHcomNetEndpointPtr &ep, SerConnInfo &info)
        : mConnInfo(info), mUuid(id)
    {
        std::lock_guard<std::mutex> lockerEp(mLock);
        mEpState[0].Set(NEP_ESTABLISHED);
        mEpVector.emplace_back(ep.Get());
        mConnState.Set(ConnectingEpState::NEW_EP);
    }

    inline bool AddEp(const UBSHcomNetEndpointPtr &ep)
    {
        std::lock_guard<std::mutex> lockerEp(mLock);

        if (NN_UNLIKELY(mEpVector.size() >= NN_NO64)) {
            NN_LOG_ERROR("Ep vector is full, ep size now is " << mEpVector.size());
            return false;
        }

        mEpState[mEpVector.size()].Set(NEP_ESTABLISHED);
        mEpVector.emplace_back(ep.Get());
        return mConnState.CAS(ConnectingEpState::NEW_EP, ConnectingEpState::NEW_EP);
    }

    bool AllEPBroken(uint16_t index);

    bool Compare(const SerConnInfo &info) const;

    std::mutex mLock;
    SerConnInfo mConnInfo {};
    UBSHcomNetAtomicState<ConnectingEpState> mConnState {};
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> mEpState[CHANNEL_EP_MAX_NUM] {};
    std::string mUuid {};
    std::vector<UBSHcomNetEndpointPtr> mEpVector {};

public:
    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

class HcomServiceGlobalObject {
public:
    static SerResult Initialize();
    static void UnInitialize();
    static void BuildTimeOutCtx(UBSHcomServiceContext &ctx);
    static void BuildBrokenCtx(UBSHcomServiceContext &ctx);

public:
    static Callback *gEmptyCallback;
    static bool gInited;
};

/**
 * Endpoint to channel upCtx, store ch pointer and endpoint index into uint64_t
 */
union Ep2ChanUpCtx {
    struct {
        uint64_t connected : 1; /* flag for connecting or connected, store different type ptr */
        uint64_t epIdx : 5; /* endpoint index, range [0, 31] */
        uint64_t ptr : 58;  /* pointer to connecting mgr or net channel */
    };
    uint64_t wholeUpCtx = 0; /* whole */

    Ep2ChanUpCtx() = default;
    explicit Ep2ChanUpCtx(uint64_t w) : wholeUpCtx(w) {}
    Ep2ChanUpCtx(uint64_t connect, uint64_t p, uint64_t i) : connected(connect), epIdx(i), ptr(p) {}

    inline UBSHcomChannel *Channel() const
    {
        if (NN_UNLIKELY(connected != 1)) {
            NN_LOG_ERROR("Failed to get channel by not connected");
            return nullptr;
        }
        return reinterpret_cast<UBSHcomChannel *>(ptr);
    }

    inline uint32_t EpIdx() const
    {
        return static_cast<uint32_t>(epIdx);
    }

    inline uint64_t Ptr() const
    {
        return ptr;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "chPtr " << ptr << ", epIdx " << epIdx << ", whole " << wholeUpCtx;
        return oss.str();
    }
};

struct RateLimiter {
    bool triggering = false;                      /* trigger next window flag */
    UBSHcomFlowCtrlLevel level = UBSHcomFlowCtrlLevel::LOW_LEVEL_BLOCK; /* wait level */
    uint16_t intervalTimeMs = 0;                  /* user config interval time ms, range in [1, 1000] */
    uint64_t thresholdByte = 0;                   /* user config threshold byte */

    uint64_t windowEndTimeMs = 0;  /* in interval time window, end time trace */
    uint64_t windowPassedByte = 0; /* in interval time window, passed byte */

    std::mutex nextWindowMutex;             /* mutex for build next window information */
    std::condition_variable nextWindowCond; /* condition variable for build next window information */

    RateLimiter() = default;

    inline bool InvalidateSize(uint32_t size) const
    {
        return size > thresholdByte;
    }

    inline bool AcquireQuota(uint32_t size) const
    {
        if (size > UINT64_MAX - windowPassedByte) {
            return false;
        }
        return (windowPassedByte + size) <= thresholdByte;
    }

    /* wait until next window */
    inline void WaitUntilNextWindow() const
    {
        uint64_t currentTime = NetMonotonic::TimeMs();
        uint64_t endTime = windowEndTimeMs;
        if (currentTime >= endTime) {
            return;
        }

        if (level == UBSHcomFlowCtrlLevel::HIGH_LEVEL_BLOCK) {
            while (NetMonotonic::TimeMs() < endTime) {
            }
        } else {
            usleep((endTime - currentTime) * NN_NO1000);
        }
    }

    inline void BuildNextWindow()
    {
        std::unique_lock<std::mutex> locker(nextWindowMutex);
        nextWindowCond.wait(locker, [&]() {
            return !triggering;
        });

        if (NetMonotonic::TimeMs() < windowEndTimeMs) {
            return;
        }

        triggering = true;
        windowEndTimeMs = NetMonotonic::TimeMs() + intervalTimeMs;
        triggering = false;
        windowPassedByte = 0;

        nextWindowCond.notify_all();
    }
};

struct HcomServiceSelfSyncParam {
    sem_t sem {};
    int result = NN_OK;

    HcomServiceSelfSyncParam()
    {
        sem_init(&sem, 0, 0);
    }

    ~HcomServiceSelfSyncParam()
    {
        sem_destroy(&sem);
    }

    inline void Wait()
    {
        int result;
        do {
            result = sem_wait(&sem);
        } while (result == -1 && errno == EINTR);
        if (result != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Sem wait failed with result " << result << ", errno " << errno << ", reason "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }
    }

    inline void Signal()
    {
        auto result = sem_post(&sem);
        if (NN_UNLIKELY(result != 0)) {
            NN_LOG_ERROR("Sem post failed " << result);
        }
    }

    int inline Result() const
    {
        return result;
    }

    inline void Result(int ret)
    {
        result = ret;
    }
};

struct HcomServiceMessage {
    void *data = nullptr;       /* pointer of data */
    uint32_t size = 0;          /* size of data */
    bool transferOwner = false; /* reserved, transfer data ownership to hcom, it will be freed after transferred */

    HcomServiceMessage() = default;

    /**
     * @brief Constructor
     * @param d                [in] pointer of data to be sent or received
     * @param s                [in] size of data to be sent or received
     */
    HcomServiceMessage(void *d, uint32_t s) : data(d), size(s) {}
} __attribute__((packed));

/**
 * @brief Seq number
 */
union HcomSeqNo {
    struct {
        /* low address */
        uint32_t realSeq : 24; /* real seq no */
        uint32_t version : 6;  /* request version */
        uint32_t fromFlat : 1; /* allocated from flat or hash map */
        uint32_t isResp : 1; /* request or reply, 0 for request, 1 for reply */
        /* high address */
    };
    uint32_t wholeSeq = 0;

    explicit HcomSeqNo(uint32_t whole) : wholeSeq(whole)
    {
    }

    inline void SetValue(uint32_t flat, uint32_t ver, uint32_t seq)
    {
        fromFlat = flat;
        version = ver;
        realSeq = seq;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "HcomSeqNo info=[wholeSeq: " << wholeSeq << ", isResp: " << isResp <<
            ", fromFlat: " << fromFlat << ", version: " << version <<
            ", realSeq: " << realSeq << "]";
        return oss.str();
    }

    inline bool IsResp() const
    {
        return isResp == 1;
    }
};

struct SerUuid {
    uint32_t ip = 0;
    uint64_t channelId = 0;

    SerUuid() = default;
    SerUuid(uint32_t ipAddress, uint64_t id) : ip(ipAddress), channelId(id) {}

    inline bool ToString(std::string &out)
    {
        return BuffToHexString(this, sizeof(SerUuid), out);
    }
} __attribute__((packed));

struct HcomConnectTimestamp {
    uint64_t localTimeUs = 0;  /* local time trace when connecting */
    uint64_t remoteTimeUs = 0; /* remote time trace when connecting */
    uint64_t deltaTimeUs = 0;  /* delta time for exchange info */

    HcomConnectTimestamp() = default;
    HcomConnectTimestamp(uint64_t lTime, uint64_t rTime, uint64_t dTime)
        : localTimeUs(lTime), remoteTimeUs(rTime), deltaTimeUs(dTime)
    {}

    uint64_t GetRemoteTimestamp(int16_t timeOutSecond) const;
};

struct HcomExchangeTimestamp {
    uint64_t timestamp = 0;
    uint64_t deltaTimeStamp = 0;

    HcomExchangeTimestamp() = default;
};

struct HcomServiceRndvMessage {
    uint64_t timestamp = 0;
    UBSHcomRequest request{};

    HcomServiceRndvMessage() = default;
    HcomServiceRndvMessage(uint64_t ts, const UBSHcomRequest &req)
        : timestamp(ts), request(req)
    {}

    bool IsTimeout() const;
};

}
}
#endif // HCOM_SERVICE_V2_SERVICE_COMMON_H_