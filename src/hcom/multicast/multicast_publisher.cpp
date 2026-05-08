/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <utility>

#include "multicast_periodic_manager.h"
#include "service_ctx_store.h"
#include "utils/multicast_lock_guard.h"

namespace ock {
namespace hcom {
void Publisher::ProcessIO(const std::vector<MultiCastServiceTimer *> &remainCtx)
{
    for (auto timer : (remainCtx)) {
        if (timer->EraseSeqNoWithRet()) {
            timer->BrokenDump();
            timer->MarkFinished();
            auto callback = reinterpret_cast<MultiCastCallback *>(timer->Callback());

            auto publisher = timer->mPublisher;
            if (publisher == nullptr) {
                NN_LOG_ERROR("publisher is null");
                continue;
            }
            PublisherContext *pubCtx = nullptr;
            publisher->mPubCtxStore->GetBySeqNo(timer->SeqNo(), pubCtx);
            if (NN_UNLIKELY(pubCtx == nullptr)) {
                NN_LOG_ERROR("pubCtx is null");
                continue;
            }

            UpdateSubscriberRsp(pubCtx);

            if (callback == nullptr) {
                NN_LOG_ERROR("publisher is null");
                continue;
            }
            callback->Run(*pubCtx);
            timer->DecreaseRef();
            publisher->mPubCtxStore->RemoveSeqNo(timer->SeqNo());
        }
        timer->DecreaseRef();
    }
}

void Publisher::UpdateSubscriberRsp(PublisherContext *pubCtx)
{
    auto subscriberRsp = pubCtx->GetSubscriberRspInfo();
    for (auto &subRsp : subscriberRsp) {
        if (subRsp.mStatus != SubscriberRspStatus::SUCCESS) {
            subRsp.mStatus = SubscriberRspStatus::BROKEN;
        }
    }
}

HcomServiceCtxStore *CreateAndInitCtxStore(uint32_t capacity, uintptr_t memPoolRaw, UBSHcomNetDriverProtocol protocol)
{
    if (NN_UNLIKELY(memPoolRaw == 0)) {
        NN_LOG_ERROR("Invalid mem Pool ptr " << memPoolRaw);
        return nullptr;
    }

    auto *memPool = reinterpret_cast<NetMemPoolFixed *>(memPoolRaw);
    memPool->IncreaseRef();

    auto *ctxStore = new (std::nothrow) HcomServiceCtxStore(capacity, memPool, protocol);
    if (NN_UNLIKELY(ctxStore == nullptr)) {
        NN_LOG_ERROR("Create ctx store failed");
        return nullptr;
    }

    auto ret = ctxStore->Initialize();
    if (NN_UNLIKELY(ret != SER_OK)) {
        NN_LOG_ERROR("Init ctx store failed " << ret);
        delete ctxStore;
        return nullptr;
    }

    ctxStore->IncreaseRef();
    return ctxStore;
}

SerResult Publisher::Initialize(uintptr_t memPool, uintptr_t pubMemPool, uintptr_t periodicMgr,
    uint32_t ctxStoreCapacity, UBSHcomNetDriverProtocol protocol)
{
    std::lock_guard<std::mutex> locker(mMgrMutex);
    mState.Set(PublisherState::PUB_NEW);

    auto header = new (std::nothrow) MultiCastTimerListHeader;
    if (header == nullptr) {
        NN_LOG_ERROR("Failed to create timer list header");
        return SER_NEW_OBJECT_FAILED;
    }
    mTimerList = reinterpret_cast<uintptr_t>(header);

    // create timer ctx store
    auto *ctxStore = CreateAndInitCtxStore(ctxStoreCapacity, memPool, protocol);
    if (NN_UNLIKELY(ctxStore == nullptr)) {
        ForceUnInitialize();
        return SER_NEW_OBJECT_FAILED;
    }
    mCtxStore = ctxStore;
    mCtxMemPool = memPool;

    // create publisher subscriber rsp inf ctx store
    auto *pubCtxStore = CreateAndInitCtxStore(ctxStoreCapacity, pubMemPool, protocol);
    if (NN_UNLIKELY(pubCtxStore == nullptr)) {
        ForceUnInitialize();
        return SER_NEW_OBJECT_FAILED;
    }
    mPubCtxStore = pubCtxStore;
    mPubCtxMemPool = pubMemPool;

    auto periodicMgrPtr = reinterpret_cast<MultiCastPeriodicManager *>(periodicMgr);
    if (NN_UNLIKELY(periodicMgrPtr == nullptr)) {
        NN_LOG_ERROR("Invalid periodic mgr ptr " << periodicMgr);
        ForceUnInitialize();
        return SER_INVALID_PARAM;
    }
    periodicMgrPtr->IncreaseRef();
    mPeriodicMgr = periodicMgr;

    return SER_OK;
}

void Publisher::ForceUnInitialize()
{
    if (NN_LIKELY(mCtxStore != nullptr)) {
        mCtxStore->DecreaseRef();
        mCtxStore = nullptr;
    }

    if (NN_LIKELY(mPubCtxStore != nullptr)) {
        mPubCtxStore->DecreaseRef();
        mPubCtxStore = nullptr;
    }

    auto ctxMemPool = reinterpret_cast<NetMemPoolFixed *>(mCtxMemPool);
    if (NN_LIKELY(ctxMemPool != nullptr)) {
        ctxMemPool->DecreaseRef();
        mCtxMemPool = 0;
    }

    auto pubCtxMemPool = reinterpret_cast<NetMemPoolFixed *>(mPubCtxMemPool);
    if (NN_LIKELY(pubCtxMemPool != nullptr)) {
        pubCtxMemPool->DecreaseRef();
        mPubCtxMemPool = 0;
    }

    auto periodicMgrPtr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    if (NN_LIKELY(periodicMgrPtr != nullptr)) {
        periodicMgrPtr->DecreaseRef();
        mPeriodicMgr = 0;
    }

    auto timeHeader = reinterpret_cast<MultiCastServiceTimer *>(mTimerList);
    if (NN_LIKELY(timeHeader != nullptr)) {
        delete timeHeader;
        mTimerList = 0;
    }

    mState.Set(PublisherState::PUB_DESTROY);
}

SubscriptionInfoPtr Publisher::GetSubscribeByEpId(uint64_t id)
{
    RWLockGuard(mRwLock).LockRead();
    auto it = mSubscriptionMap.find(id);
    if (it != mSubscriptionMap.end()) {
        return it->second;
    }
    return nullptr;
}

void Publisher::ProcessIoInBroken()
{
    auto header = reinterpret_cast<MultiCastTimerListHeader *>(mTimerList);
    if (header == nullptr) {
        NN_LOG_INFO("Publisher " << mName << " mTimerList is null when broken");
        return;
    }
    if (header->GetCtxCount() == 0) {
        NN_LOG_INFO("Publisher " << mName << " timer list header size is 0");
        return;
    }

    std::vector<MultiCastServiceTimer *> remainCtx;
    header->GetTimerCtx(remainCtx);
    if (!remainCtx.empty()) {
        NN_LOG_INFO("Publisher " << mName << " process io broken, size " << remainCtx.size());
        ProcessIO(remainCtx);
    }

    /* try again to handle new add io during process */
    header->GetTimerCtx(remainCtx);
    if (!remainCtx.empty()) {
        NN_LOG_INFO("Publisher " << mName << " process io broken, size " << remainCtx.size());
        ProcessIO(remainCtx);
    }
}

SerResult Publisher::PrepareTimerContext(MultiCastCallback *cb, int16_t timeout, MultiCastTimerContext &context)
{
    auto timerPtr = mCtxStore->GetCtxObj<MultiCastServiceTimer>();
    if (NN_UNLIKELY(timerPtr == nullptr)) {
        NN_LOG_ERROR("Failed to get context object from memory pool.");
        return SER_NEW_OBJECT_FAILED;
    }

    timerPtr->mCtxStore = mCtxStore;
    // if t < 0, it means never timeout, so leave mTimeout as 0
    if (timeout >= 0) {
        timerPtr->mTimeout = NetMonotonic::TimeSec() + static_cast<uint64_t>(timeout);
    }
    timerPtr->mCallback = reinterpret_cast<uintptr_t>(cb);
    timerPtr->mType = MultiCastSyncCBType::IO;
    timerPtr->mPublisher = this;
    timerPtr->mPublisher->IncreaseRef();
    timerPtr->mState = MultiCastAsyncCBState::INIT;
    context.timer = timerPtr;

    auto result = mCtxStore->PutAndGetSeqNo(context.timer, context.seqNo);
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Failed to generate seqNo by context store pool.");
        mCtxStore->Return(timerPtr);
        return SER_NEW_OBJECT_FAILED;
    }

    context.timer->IncreaseRef();
    // timer seqNo is invalid, here need update by EmplaceContext() build seqNo.
    context.timer->SeqNo(context.seqNo);

    MultiCastPeriodicManagerPtr periodicMgrPtr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    result = periodicMgrPtr->AddTimer(context.timer);
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Failed to add timer in for timeout control.");
        context.timer->EraseSeqNo();
        mCtxStore->Return(timerPtr);
        return result;
    }
    context.timer->IncreaseRef();
    return SER_OK;
}

void Publisher::DestroyTimerContext(MultiCastTimerContext &context)
{
    if (NN_LIKELY(context.timer->EraseSeqNoWithRet())) {
        context.timer->DeleteCallBack();
        context.timer->MarkFinished();
        context.timer->DecreaseRef();
    }
}

void Publisher::DestroyPubContext(PublisherContext &context)
{
    context.subscriberRspList.clear();
    mPubCtxStore->Return(&context);
}

SerResult Publisher::Call(const UBSHcomNetTransOpInfo &opInfo, const MultiRequest &req, const MultiCastCallback *done)
{
    if (NN_UNLIKELY(mSubCount == 0)) {
        NN_LOG_ERROR("Failed to send, no subscriber exist");
        return SER_INVALID_PARAM;
    }

    MultiCastTimerContext context {};
    MultiCastCallback *cb = NewMultiCastCallback(
        [this, done](PublisherContext &context) { const_cast<MultiCastCallback *>(done)->Run(context); },
        std::placeholders::_1);
    if (NN_UNLIKELY(!cb)) {
        NN_LOG_ERROR("Publisher Call malloc callback failed");
    }

    SerResult result = PrepareTimerContext(cb, opInfo.timeout, context);
    if (NN_UNLIKELY(result != SER_OK)) {
        return result;
    }

    auto pubCtxPtr = mPubCtxStore->GetCtxObj<PublisherContext>();
    if (NN_UNLIKELY(pubCtxPtr == nullptr)) {
        NN_LOG_ERROR("Failed to get pub context object from memory pool.");
        DestroyTimerContext(context);
        return SER_NEW_OBJECT_FAILED;
    }

    auto ctx = new (pubCtxPtr)PublisherContext(mMaxSubscriberNum);
    result = ctx->InitSubscribers(mSubscriptionMap);
    if (NN_UNLIKELY(result != SER_OK)) {
        DestroyTimerContext(context);
        DestroyPubContext(*ctx);
        return result;
    }

    result = mPubCtxStore->PutBySeqNo(ctx, context.seqNo);
    if (NN_UNLIKELY(result != SER_OK)) {
        DestroyTimerContext(context);
        DestroyPubContext(*ctx);
        return result;
    }

    UBSHcomNetTransRequest netReq(reinterpret_cast<uintptr_t>(req.data), 0, req.lkey, 0, req.size,
        sizeof(SerTransContext));
    SetServiceTransCtx(netReq.upCtxData, context.seqNo);
    result = PostSendAll(context, netReq, ctx, result);
    if (NN_UNLIKELY(result == SER_MULTICAST_SEND_ALL_FAILED)) {
        // 全部失败才清理ctx，否则在收到所有数据，或超时的时候再清理
        NN_LOG_ERROR("Multicast Call all failed result " << result);
        DestroyTimerContext(context);
        DestroyPubContext(*ctx);
        return result;
    }

    return SER_OK;
}

SerResult Publisher::PostSendAll(const MultiCastTimerContext &context, const UBSHcomNetTransRequest &netReq,
    PublisherContext *ctx, SerResult &result)
{
    uint32_t failedCount = 0;
    uint32_t toSendCount = 0;
    std::vector<UBSHcomNetEndpoint *> eps;
    {
        // 1. 加读写锁，拷贝 endpoint 指针
        RWLockGuard(mRwLock).LockRead();
        eps.reserve(mSubCount);
        toSendCount = mSubCount;
        for (const auto &pair : mEpMap) {
            eps.push_back(pair.second.Get());
        }
    }

    // 2. 在无锁状态下发送消息，避免阻塞其他线程
    ctx->SetSendCount(toSendCount);
    for (const auto &ep : eps) {
        if (NN_UNLIKELY(ep == nullptr)) {
            failedCount++;
            NN_LOG_ERROR("ep is null! failedCount " << failedCount);
            continue;
        }

        result = ep->PostSendRawNoCpy(netReq, context.seqNo);
        if (NN_UNLIKELY(result != SER_OK)) {
            auto sub = GetSubscribeByEpId(ep->Id());
            if (NN_UNLIKELY(sub.Get() == nullptr)) {
                NN_LOG_ERROR("Subscription of ep " << ep->Id() << " is nullptr");
                failedCount++;
                continue;
            }
            ctx->SetResponseStatus(sub, nullptr, SubscriberRspStatus::SEND_ERROR);
            NN_LOG_ERROR("Failed to send to ep " << ep->Id() << ", error: " << result << " set sub info " <<
                sub->mName << " to SEND_ERROR");
            failedCount++;
        }
    }

    if (failedCount == 0) {
        return SER_OK;
    }
    // 记录发送成功的数量，回调汇聚的时候用
    ctx->SetSendCount(toSendCount - failedCount);
    if (NN_UNLIKELY(failedCount == toSendCount)) {
        return SER_MULTICAST_SEND_ALL_FAILED;
    }
    return SER_OK;
}

bool Publisher::AddSubscription(SubscriptionInfoPtr &info)
{
    if (info == nullptr) {
        return false;
    }
    NN_LOG_DEBUG("begin to add subscribe info id :" << info->mId << " name:" << info->mName);

    RWLockGuard(mRwLock).LockWrite();
    info->mEp->UpCtx(reinterpret_cast<uint64_t>(info.Get()));

    UBSHcomEpOptions epOptions;
    epOptions.tcpBlockingIo = true;
    epOptions.cbByWorkerInBlocking = false;
    info->mEp->SetEpOption(epOptions);

    mEpMap.emplace(info->mId, info->mEp);
    mSubscriptionMap.emplace(info->mId, info);
    mSubCount++;
    return true;
}

bool Publisher::DelSubscription(SubscriptionInfoPtr &info)
{
    if (info == nullptr || info->mEp == nullptr) {
        NN_LOG_ERROR("Delete subscription failed as info is invalid");
        return false;
    }
    // 此时可能还有ep正在做发送消息操作，避免core，ep通过延迟释放，此处仅仅移除subscription
    RWLockGuard(mRwLock).LockWrite();
    mSubscriptionMap.erase(info->mId);
    return true;
}

uint32_t Publisher::GetSubscriberNum()
{
    RWLockGuard(mRwLock).LockRead();
    return mSubscriptionMap.size();
}

std::vector<SubscriptionInfoPtr> Publisher::GetAllSubscriberInfo()
{
    RWLockGuard(mRwLock).LockRead();
    std::vector<SubscriptionInfoPtr> subscriptionVec;
    for (const auto &sub : mSubscriptionMap) {
        if (sub.second.Get() == nullptr) {
            continue;
        }
        subscriptionVec.push_back(sub.second);
    }
    return subscriptionVec;
}

Publisher::Publisher(const std::string &name)
{
    mName = name;
}
}
}
