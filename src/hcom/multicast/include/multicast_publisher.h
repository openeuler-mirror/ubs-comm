/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_PUBLISHER_H
#define HCOM_MULTICAST_PUBLISHER_H

#include "hcom.h"
#include "hcom_service.h"
#include "multicast_config.h"
#include "multicast_def.h"
#include "multicast_message.h"
#include "multicast_subscriber.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <type_traits>

namespace ock {
namespace hcom {
class MultiCastPeriodicManager;
class MultiCastServiceTimer;
class MultiCastTimerContext;
class Publisher;
class SubscriptionInfo;
using SubscriptionInfoPtr = NetRef<SubscriptionInfo>;
class SubscriberRspInfo;
using SubscriberRspInfoPtr = NetRef<SubscriberRspInfo>;
static constexpr uint32_t MULTICAST_CACHE_LINE_SIZE = 64;

class MultiCastCallbackPool {
public:
    static constexpr uint32_t CALLBACK_SLOT_SIZE = 256;

    static MultiCastCallbackPool &Instance()
    {
        static MultiCastCallbackPool pool;
        return pool;
    }

    bool Initialize()
    {
        std::call_once(mInitFlag, [this]() { InitializeInner(); });
        return mReady.load(std::memory_order_acquire);
    }

    void *Acquire()
    {
        if (NN_UNLIKELY(!mReady.load(std::memory_order_acquire) && !Initialize())) {
            return nullptr;
        }

        uint32_t preferredShard = GetThreadShard();
        for (uint32_t offset = 0; offset < SHARD_COUNT; ++offset) {
            void *slot = Pop(static_cast<uint32_t>((preferredShard + offset) % SHARD_COUNT));
            if (slot != nullptr) {
                return slot;
            }
        }
        return nullptr;
    }

    void Release(void *ptr)
    {
        Slot *slot = reinterpret_cast<Slot *>(ptr);
        uint32_t index = static_cast<uint32_t>(slot - mSlots);
        PoolShard &shard = mShards[slot->shardId];
        uint64_t head = shard.head.load(std::memory_order_relaxed);
        do {
            slot->next = Index(head);
        } while (!shard.head.compare_exchange_weak(head, MakeHead(Tag(head) + 1, index), std::memory_order_release,
                                                   std::memory_order_relaxed));
    }

private:
    static constexpr uint32_t SHARD_COUNT = 32;
    static constexpr uint32_t SLOT_COUNT = 32768;
    static constexpr uint32_t SLOTS_PER_SHARD = SLOT_COUNT / SHARD_COUNT;
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

    struct alignas(std::max_align_t) Slot {
        uint8_t data[CALLBACK_SLOT_SIZE];
        uint32_t next = INVALID_INDEX;
        uint32_t shardId = 0;
    };
    static_assert(std::is_standard_layout<Slot>::value, "Multicast callback slot must be standard layout");
    static_assert(offsetof(Slot, data) == 0, "Multicast callback storage must be the first slot member");

    struct alignas(MULTICAST_CACHE_LINE_SIZE) PoolShard {
        std::atomic<uint64_t> head = {static_cast<uint64_t>(INVALID_INDEX)};
    };

    MultiCastCallbackPool() = default;
    ~MultiCastCallbackPool()
    {
        delete[] mSlots;
    }

    static uint64_t MakeHead(uint32_t tag, uint32_t index)
    {
        return (static_cast<uint64_t>(tag) << 32) | index;
    }

    static uint32_t Index(uint64_t head)
    {
        return static_cast<uint32_t>(head);
    }

    static uint32_t Tag(uint64_t head)
    {
        return static_cast<uint32_t>(head >> 32);
    }

    uint32_t GetThreadShard()
    {
        struct ThreadShard {
            const MultiCastCallbackPool *pool = nullptr;
            uint32_t shardId = 0;
        };
        static thread_local ThreadShard threadShard;
        if (threadShard.pool != this) {
            threadShard.pool = this;
            threadShard.shardId = mNextShard.fetch_add(1, std::memory_order_relaxed) % SHARD_COUNT;
        }
        return threadShard.shardId;
    }

    void *Pop(uint32_t shardId)
    {
        PoolShard &shard = mShards[shardId];
        uint64_t head = shard.head.load(std::memory_order_acquire);
        while (Index(head) != INVALID_INDEX) {
            Slot &slot = mSlots[Index(head)];
            uint64_t next = MakeHead(Tag(head) + 1, slot.next);
            if (shard.head.compare_exchange_weak(head, next, std::memory_order_acq_rel, std::memory_order_acquire)) {
                return slot.data;
            }
        }
        return nullptr;
    }

    void InitializeInner()
    {
        mSlots = new (std::nothrow) Slot[SLOT_COUNT];
        if (mSlots == nullptr) {
            return;
        }
        for (uint32_t shardId = 0; shardId < SHARD_COUNT; ++shardId) {
            uint32_t first = shardId * SLOTS_PER_SHARD;
            uint32_t end = first + SLOTS_PER_SHARD;
            for (uint32_t index = first; index < end; ++index) {
                mSlots[index].shardId = shardId;
                mSlots[index].next = index + 1 < end ? index + 1 : INVALID_INDEX;
            }
            mShards[shardId].head.store(MakeHead(0, first), std::memory_order_relaxed);
        }
        mReady.store(true, std::memory_order_release);
    }

    Slot *mSlots = nullptr;
    std::once_flag mInitFlag;
    std::atomic<bool> mReady = {false};
    std::atomic<uint32_t> mNextShard = {0};
    PoolShard mShards[SHARD_COUNT];
};

struct SubscriptionGroup {
    std::vector<SubscriptionInfoPtr> subscribers;
    std::atomic<uint32_t> rr{0};
};
using SubscriptionGroupPtr = std::shared_ptr<SubscriptionGroup>;
using SubscriptionSnapshot = std::vector<SubscriptionGroupPtr>;
enum class PublisherState
{
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
    virtual void Destroy()
    {
        delete this;
    }
};

/**
 * @brief Closure MultiCastCallback.
 *
 * @param ClosureFunction
 */
template <typename ClosureFunction>
class MultiCastClosureCallback : public MultiCastCallback {
public:
    explicit MultiCastClosureCallback(ClosureFunction &&function, bool deleteSelf, bool pooled)
        : mFunction(std::move(function)),
          mDeleteSelf(deleteSelf),
          mPooled(pooled)
    {
    }

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
            Destroy();
        }
    }

    bool Permanent() const override
    {
        return !mDeleteSelf;
    }

    void Destroy() override
    {
        if (!mPooled) {
            delete this;
            return;
        }
        this->~MultiCastClosureCallback();
        MultiCastCallbackPool::Instance().Release(this);
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
    bool mPooled = false;
    uint64_t mStartTime = 0;
};

// /  仅用于失败情况下一个 MultiCastCallback 对象没有被扔进超时队列中，需要被清理。当它需要被调用 Run() 时无
// /  需调用此函数。另外需要考虑 permanent callback, 它不需要被清理，常用于回复消息
inline void DestroyCallback(const MultiCastCallback *cb)
{
    if (cb && !cb->Permanent()) {
        const_cast<MultiCastCallback *>(cb)->Destroy();
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
template <typename... Args>
MultiCastCallback *NewMultiCastCallback(Args... args)
{
    auto closure = std::bind(args...);
    using CallbackType = MultiCastClosureCallback<decltype(closure)>;
    static_assert(sizeof(CallbackType) <= MultiCastCallbackPool::CALLBACK_SLOT_SIZE,
                  "Multicast callback exceeds callback pool slot size");
    static_assert(alignof(CallbackType) <= alignof(std::max_align_t),
                  "Multicast callback alignment exceeds callback pool alignment");
    void *storage = MultiCastCallbackPool::Instance().Acquire();
    if (NN_LIKELY(storage != nullptr)) {
        return new (storage) CallbackType(std::move(closure), true, true);
    }
    return new (std::nothrow) CallbackType(std::move(closure), true, false);
}

/**
 * @brief Generate a permanent callback object.
 *
 * @param Args
 * @param args
 * @return MultiCastCallback*
 * @note see @ref NewCallback.
 */
template <typename... Args>
MultiCastCallback *NewPermanentCallback(Args... args)
{
    auto closure = std::bind(args...);
    return new (std::nothrow) MultiCastClosureCallback<decltype(closure)>(std::move(closure), false, false);
}

class SubscriptionInfo {
public:
    SubscriptionInfo() = default;
    SubscriptionInfo(uint64_t id, std::string name, std::string &ip, uint16_t port, UBSHcomNetEndpointPtr ep)
        : mId(id),
          mName(std::move(name)),
          mIp(ip),
          mPort(port),
          mEp(std::move(ep))
    {
    }

    inline uint64_t GetId() const
    {
        return mId;
    }

    inline const std::string &GetName() const
    {
        return mName;
    }

    inline const std::string &GetIp() const
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

enum class SubscriberRspStatus
{
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
        return __atomic_load_n(&mStatus, __ATOMIC_ACQUIRE);
    }

    inline const MultiResponse &GetMultiResponse() const
    {
        return mResponse;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    SubscriptionInfoPtr mSubInfo; // 订阅者信息
    SubscriberRspStatus mStatus;  // 响应状态
    MultiResponse mResponse{};    // 订阅者回复的数据
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

    inline const std::vector<SubscriberRspInfo> &GetSubscriberRspInfo()
    {
        return subscriberRspList;
    }

    inline bool SetResponseStatus(const SubscriptionInfoPtr &sub, UBSHcomNetMessage *message,
                                  SubscriberRspStatus status)
    {
        for (auto &item : subscriberRspList) {
            if (NN_UNLIKELY(item.mSubInfo.Get() == nullptr)) {
                continue;
            }
            if (item.mSubInfo->mId == sub->mId || item.mSubInfo->mIp == sub->mIp) {
                SubscriberRspStatus expected = SubscriberRspStatus::INIT;
                if (!__atomic_compare_exchange_n(&item.mStatus, &expected, status, false, __ATOMIC_ACQUIRE,
                                                 __ATOMIC_RELAXED)) {
                    return false;
                }
                if (message != nullptr) {
                    item.mResponse.data = message->Data();
                    item.mResponse.size = message->DataLen();
                }
                __atomic_thread_fence(__ATOMIC_RELEASE);
                return true;
            }
        }
        NN_LOG_WARN("Subscription is not in list, id " << sub->mId << ".");
        return false;
    }

    // 初始化所以订阅者状态
    inline SerResult InitSubscribers(const std::unordered_map<uint32_t, SubscriptionInfoPtr> &allSubs)
    {
        for (const auto &sub : allSubs) {
            subscriberRspList.emplace_back(sub.second, SubscriberRspStatus::INIT);
        }
        repliedCount = 0;
        return SER_OK;
    }

    inline SerResult InitSubscribers(const std::vector<SubscriptionInfoPtr> &allSubs)
    {
        subscriberRspList.clear();
        for (const auto &sub : allSubs) {
            subscriberRspList.emplace_back(sub, SubscriberRspStatus::INIT);
        }
        repliedCount = 0;
        return SER_OK;
    }

    inline void MarkReplied(SubscriptionInfoPtr &sub, UBSHcomNetMessage *message)
    {
        if (SetResponseStatus(sub, message, SubscriberRspStatus::SUCCESS)) {
            __sync_fetch_and_add(&repliedCount, 1);
        }
    }

    inline uint32_t GetReplyCount() const
    {
        return __atomic_load_n(&repliedCount, __ATOMIC_ACQUIRE);
    }

    inline void SetSendCount(uint32_t count)
    {
        __atomic_store_n(&sendCount, count, __ATOMIC_RELEASE);
    }

    inline uint32_t GetSendCount() const
    {
        return __atomic_load_n(&sendCount, __ATOMIC_ACQUIRE);
    }

    inline void Reset()
    {
        sendCount = 0;
        repliedCount = 0;
        subscriberRspList.clear();
    }

private:
    // 32 byte
    uint32_t sendCount = 0;
    uint32_t repliedCount = 0;
    std::vector<SubscriberRspInfo> subscriberRspList;
    friend class PublisherService;
    friend class MultiCastPeriodicManager;
    friend class MultiCastIoContext;
    friend class Publisher;
};

enum class MultiCastIoState : uint8_t
{
    FREE = 0,
    PREPARING,
    INIT,
    FINISHED,
    TIMEOUT,
    BROKEN,
};

class alignas(MULTICAST_CACHE_LINE_SIZE) MultiCastIoContext {
public:
    MultiCastIoContext() = default;

    inline void Initialize(uint32_t index, uint32_t maxSubscriberNum)
    {
        mIndex = index;
        mPublisherCtx.subscriberRspList.reserve(maxSubscriberNum);
    }

    inline uint32_t SeqNo() const
    {
        return mSeqNo.load(std::memory_order_acquire);
    }

    inline PublisherContext &GetPublisherContext()
    {
        return mPublisherCtx;
    }

    inline bool TryAcquire(uint32_t seqNo)
    {
        uint32_t access = mAccess.load(std::memory_order_acquire);
        while ((access & ACCESS_CLOSED) == 0) {
            if (mAccess.compare_exchange_weak(access, access + 1, std::memory_order_acquire,
                                              std::memory_order_relaxed)) {
                if (mSeqNo.load(std::memory_order_acquire) == seqNo &&
                    mState.load(std::memory_order_acquire) == MultiCastIoState::INIT) {
                    return true;
                }
                ReleaseAccess();
                return false;
            }
        }
        return false;
    }

    inline void ReleaseAccess()
    {
        mAccess.fetch_sub(1, std::memory_order_release);
    }

private:
    static constexpr uint32_t ACCESS_CLOSED = 1U << 31;

    uint32_t mIndex = 0;
    std::atomic<uint32_t> mSeqNo = {0};
    uint32_t mGeneration = 0;
    uint32_t mNextFree = UINT32_MAX;
    uint64_t mExpireTick = 0;
    Publisher *mPublisher = nullptr;
    const MultiCastCallback *mCallback = nullptr;
    PublisherContext mPublisherCtx;
    std::atomic<MultiCastIoState> mState = {MultiCastIoState::FREE};
    std::atomic<uint32_t> mAccess = {ACCESS_CLOSED};
    std::atomic<bool> mSendReady = {false};
    MultiCastIoContext *mWheelPrev = nullptr;
    MultiCastIoContext *mWheelNext = nullptr;
    MultiCastIoContext *mAddNext = nullptr;
    MultiCastIoContext *mCompleteNext = nullptr;
    uint32_t mWheelSlot = 0;
    uint16_t mShardId = 0;
    bool mInWheel = false;

    friend class MultiCastPeriodicManager;
    friend class Publisher;
    friend class PublisherServiceImp;
};

class Publisher {
public:
    Publisher() = default;
    explicit Publisher(const std::string &name);
    ~Publisher()
    {
        mSubscriptionMap.clear();
        mEpMap.clear();
        mSubscriptionGroups.clear();
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
    SerResult Call(const UBSHcomNetTransOpInfo &opInfo, const std::string &remoteIp, const MultiRequest &req,
                   const MultiCastCallback *done);

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

    SerResult Initialize(uintptr_t memPool, uintptr_t periodicMgr, uint32_t ctxStoreCapacity,
                         UBSHcomNetDriverProtocol protocol = UBSHcomNetDriverProtocol::RDMA);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    SerResult PostSendAll(MultiCastIoContext &context, const UBSHcomNetTransRequest &netReq, uint32_t seqNo,
                          SerResult &result);
    SerResult PrepareIoContext(const UBSHcomNetTransOpInfo &opInfo, const MultiCastCallback *done,
                               MultiCastIoContext *&context);
    SerResult InitIoSubscribers(MultiCastIoContext &context);
    SerResult InitIoSubscriber(MultiCastIoContext &context, const std::string &remoteIp);
    void DestroyIoContext(MultiCastIoContext *context);
    void TryCompleteIoContext(MultiCastIoContext *context);
    SerResult FinishIoSend(MultiCastIoContext *context, SerResult result);
    uint32_t AcquireSubscriptionSnapshot();
    void ReleaseSubscriptionSnapshot(uint32_t index);
    void RebuildSubscriptionGroupsLocked();
    static SubscriptionInfoPtr SelectSubscription(SubscriptionGroup &group);
    void ForceUnInitialize();
    void ProcessIoInBroken();

    std::string mName;
    UBSHcomNetAtomicState<PublisherState> mState{};
    std::atomic<uint32_t> mSubCount = {0};
    std::unordered_map<uint32_t, UBSHcomNetEndpointPtr> mEpMap;
    std::unordered_map<uint32_t, SubscriptionInfoPtr> mSubscriptionMap;
    std::unordered_map<std::string, SubscriptionGroupPtr> mSubscriptionGroups;
    SubscriptionSnapshot mSubscriptionSnapshots[2];
    std::atomic<uint32_t> mSubscriptionSnapshotIndex = {0};
    std::atomic<uint32_t> mSubscriptionSnapshotReaders[2] = {};
    NetReadWriteLock mRwLock;

    uintptr_t mCtxMemPool = NN_NO0;
    HcomServiceCtxStore *mCtxStore = nullptr; /* store delayed endpoint timer */
    uintptr_t mPeriodicMgr = NN_NO0;          /* timeout periodic manager */
    UBSHcomNetDriverProtocol mProtocol = UBSHcomNetDriverProtocol::RDMA;
    std::mutex mMgrMutex;

    friend class PublisherServiceImp;
    friend class MultiCastPeriodicManager;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

using PublisherPtr = NetRef<Publisher>;
} // namespace hcom
} // namespace ock

#endif
