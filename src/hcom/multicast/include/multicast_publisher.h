/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_PUBLISHER_H
#define HCOM_MULTICAST_PUBLISHER_H

#include "hcom.h"
#include "multicast_message.h"
#include "multicast_subscriber.h"
#include "hcom_service.h"
#include "multicast_config.h"
#include "multicast_def.h"

namespace ock {
namespace hcom {
class MultiCastServiceTimer;
class MultiCastTimerContext;
class SubscriptionInfo;
using SubscriptionInfoPtr = NetRef<SubscriptionInfo>;
class SubscriberRspInfo;
using SubscriberRspInfoPtr = NetRef<SubscriberRspInfo>;
enum class PublisherState {
    PUB_NEW = 0,
    PUB_ESTABLISHED = 1,
    PUB_CLOSE = 2,
    PUB_DESTROY = 3,
};

class MultiCastCallback {
public:
    MultiCastCallback() = default;
    virtual ~MultiCastCallback() = default;

    virtual void Run(PublisherContext &context) = 0;

    virtual void SetTime(uint64_t time) = 0;
    virtual uint64_t GetTime() = 0;
    virtual bool Permanent() const = 0;
};

/**
 * @brief Closure MultiCastCallback.
 *
 * @param ClosureFunction
 */
template <typename ClosureFunction> class MultiCastClosureCallback : public MultiCastCallback {
public:
    explicit MultiCastClosureCallback(ClosureFunction &&function, bool deleteSelf)
        : mFunction(function), mDeleteSelf(deleteSelf)
    {}

    ~MultiCastClosureCallback() override = default;

    void Run(PublisherContext &context) override
    {
        bool isDeleteSelf = false;
        if (mDeleteSelf) {
            mDeleteSelf = false;
            isDeleteSelf = true;
        }
        mFunction(context);
        if (isDeleteSelf) {
            delete this;
        }
    }

    bool Permanent() const override
    {
        return !mDeleteSelf;
    }

private:
    void SetTime(uint64_t time) override
    {
        mStartTime = time;
    }

    uint64_t GetTime() override
    {
        return mStartTime;
    }

private:
    ClosureFunction mFunction = nullptr;
    bool mDeleteSelf = true;
    uint64_t mStartTime = 0;
};

// /  仅用于失败情况下一个 MultiCastCallback 对象没有被扔进超时队列中，需要被清理。当它需要被调用 Run() 时无
// /  需调用此函数。另外需要考虑 permanent callback, 它不需要被清理，常用于回复消息
inline void DestroyCallback(const MultiCastCallback *cb)
{
    if (cb && !cb->Permanent()) {
        delete cb;
    }
}

/**
 * @brief Generate a self-deleting MultiCastCallback object.
 *
 * @param Args
 * @param args
 * @return MultiCastCallback*
 * @note At present, asynchronous operation is not a hot spot. In order to simplify
 * coding, std::bind is used to implement closure. If the cost of std::bind
 * is found to be high, then optimize it.
 */
template <typename... Args> MultiCastCallback *NewMultiCastCallback(Args... args)
{
    auto closure = std::bind(args...);
    return new (std::nothrow) MultiCastClosureCallback<decltype(closure)>(std::move(closure), true);
}

/**
 * @brief Generate a permanent callback object.
 *
 * @param Args
 * @param args
 * @return MultiCastCallback*
 * @note see @ref NewCallback.
 */
template <typename... Args> MultiCastCallback *NewPermanentCallback(Args... args)
{
    auto closure = std::bind(args...);
    return new (std::nothrow) MultiCastClosureCallback<decltype(closure)>(std::move(closure), false);
}

class SubscriptionInfo {
public:
    SubscriptionInfo() = default;
    SubscriptionInfo(uint64_t id, std::string name, std::string &ip, uint16_t port, UBSHcomNetEndpointPtr ep)
        : mId(id), mName(std::move(name)), mIp(ip), mPort(port), mEp(std::move(ep))
    {}

    inline uint64_t GetId() const
    {
        return mId;
    }

    inline const std::string& GetName() const
    {
        return mName;
    }

    inline const std::string& GetIp() const
    {
        return mIp;
    }

    inline uint16_t GetPort() const
    {
        return mPort;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    uint64_t mId = 0;
    std::string mName;
    std::string mIp;
    uint16_t mPort = 0;
    UBSHcomNetEndpointPtr mEp = nullptr;
    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class PublisherContext;
    friend class Publisher;
    friend class SubscriberRspInfo;
};

enum class SubscriberRspStatus {
    SUCCESS,       // 已回复（成功）
    INIT,          // 初始还未发送
    SEND_ERROR,    // 发送错误
    TIMEOUT,       // 超时未回复
    BROKEN,        // 订阅者离线
    UNKNOWN_ERROR, // 其他未知错误
};

class SubscriberRspInfo {
public:
    SubscriberRspInfo(SubscriptionInfoPtr sub, SubscriberRspStatus s) : mSubInfo(std::move(sub)), mStatus(s) {}

    inline SubscriptionInfoPtr GetSubInfos() const
    {
        return mSubInfo;
    }

    inline SubscriberRspStatus GetStatus() const
    {
        return mStatus;
    }

    inline const MultiResponse& GetMultiResponse() const
    {
        return mResponse;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    SubscriptionInfoPtr mSubInfo; // 订阅者信息
    SubscriberRspStatus mStatus;  // 响应状态
    MultiResponse mResponse {};   // 订阅者回复的数据
    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class PublisherContext;
    friend class Publisher;
    friend class MultiCastPeriodicManager;
};

class PublisherContext {
public:
    PublisherContext() = default;
    explicit PublisherContext(uint32_t maxSubscriberNum)
    {
        subscriberRspList.reserve(maxSubscriberNum);
    }
    ~PublisherContext()
    {
        subscriberRspList.clear();
    }

    inline const std::vector<SubscriberRspInfo>& GetSubscriberRspInfo()
    {
        return subscriberRspList;
    }

    inline void SetResponseStatus(SubscriptionInfoPtr &sub, UBSHcomNetMessage *message, SubscriberRspStatus status)
    {
        for (auto &item : subscriberRspList) {
            if (NN_UNLIKELY(item.mSubInfo.Get() == nullptr)) {
                continue;
            }
            if (item.mSubInfo->mId == sub->mId) {
                if (message != nullptr) {
                    item.mResponse.data = message->Data();
                    item.mResponse.size = message->DataLen();
                }
                item.mStatus = status;
                return;
            }
        }
        NN_LOG_WARN("subscrption not in list id " << sub->mId);
    }

    // 初始化所以订阅者状态
    inline SerResult InitSubscribers(const std::unordered_map<uint32_t, SubscriptionInfoPtr> &allSubs)
    {
        for (const auto &sub : allSubs) {
            subscriberRspList.emplace_back(sub.second, SubscriberRspStatus::INIT);
        }
        repliedCount.store(0, std::memory_order_relaxed);
        return SER_OK;
    }

    inline uint32_t MarkRepliedAndGetReplyCount(SubscriptionInfoPtr &sub, UBSHcomNetMessage *message)
    {
        SetResponseStatus(sub, message, SubscriberRspStatus::SUCCESS);
        return repliedCount.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    inline void SetSendCount(uint32_t count)
    {
        sendCount.store(count, std::memory_order_relaxed);
    }

    inline uint32_t GetSendCount() const
    {
        return sendCount.load(std::memory_order_relaxed);
    }

private:
    // 32 byte
    std::atomic<uint32_t> sendCount{0};
    std::atomic<uint32_t> repliedCount{0};
    std::vector<SubscriberRspInfo> subscriberRspList;
    friend class PublisherService;
    friend class MultiCastPeriodicManager;
    friend class Publisher;
};

class Publisher {
public:
    Publisher() = default;
    explicit Publisher(const std::string &name);
    ~Publisher()
    {
        mSubscriptionMap.clear();
        mEpMap.clear();
        ForceUnInitialize();
    }

    /* *
     * @brief 发布双边消息，需要订阅者回复
     *
     * @param req       [in] 发送组播消息请求
     * @param opInfo    [in] 发送组播消息的opInfo主要设置超时时间
     * @param done      [in] 发送组播完成或超时的回调函数
     *
     * @return 成功返回0，失败返回错误码
     */
    SerResult Call(const UBSHcomNetTransOpInfo &opInfo, const MultiRequest &req, const MultiCastCallback *done);

    bool AddSubscription(SubscriptionInfoPtr &info);

    bool DelSubscription(SubscriptionInfoPtr &info);

    uint32_t GetSubscriberNum();

    /* *
     * @brief 获取所有订阅者信息，该接口仅用于初始化完后调用一次，若频繁调用可能影响发送性能
     *
     * #return 所有订阅者信息
     */
    std::vector<SubscriptionInfoPtr> GetAllSubscriberInfo();

    /* *
     * @brief 查询组播范围
     *
     * @return 订阅者信息的列表
     */
    SubscriptionInfoPtr GetSubscribeByEpId(uint64_t id);

    SerResult Initialize(uintptr_t memPool, uintptr_t pubMemPool, uintptr_t periodicMgr, uint32_t ctxStoreCapacity);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    int PrepareTimerContext(ock::hcom::MultiCastCallback *cb, int16_t timeout,
        ock::hcom::MultiCastTimerContext &context);
    void DestroyTimerContext(MultiCastTimerContext &context);
    void DestroyPubContext(PublisherContext &context);
    SerResult PostSendAll(const MultiCastTimerContext &context, const UBSHcomNetTransRequest &netReq,
        PublisherContext *ctx, SerResult &result);
    void ForceUnInitialize();
    void ProcessIoInBroken();
    static void ProcessIO(const std::vector<MultiCastServiceTimer *> &remainCtx);
    static void UpdateSubscriberRsp(PublisherContext *pubCtx);

    std::string mName;
    UBSHcomNetAtomicState<PublisherState> mState {};
    uint32_t mSubCount = 0;
    std::unordered_map<uint32_t, UBSHcomNetEndpointPtr> mEpMap;
    std::unordered_map<uint32_t, SubscriptionInfoPtr> mSubscriptionMap;
    NetReadWriteLock mRwLock;

    uintptr_t mCtxMemPool = NN_NO0;
    uintptr_t mPubCtxMemPool = NN_NO0;
    HcomServiceCtxStore *mCtxStore = nullptr;    /* store seq no ctx */
    HcomServiceCtxStore *mPubCtxStore = nullptr; /* store seq no pub ctx */
    uintptr_t mPeriodicMgr = NN_NO0;            /* timeout periodic manager */
    uintptr_t mTimerList = NN_NO0;              /* record timer ctx by list */
    uint32_t mMaxSubscriberNum = NN_NO7;
    std::mutex mMgrMutex;

    friend class PublisherServiceImp;
    friend class MultiCastPeriodicManager;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

using PublisherPtr = NetRef<Publisher>;
}
}

#endif