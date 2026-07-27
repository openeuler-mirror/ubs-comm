/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <thread>
#include <utility>

#include "multicast_periodic_manager.h"
#include "net_sock_driver_oob.h"

#include "net_sock_async_endpoint.h"
#include "service_ctx_store.h"
#include "utils/multicast_lock_guard.h"

namespace ock {
namespace hcom {
static void PrepareTcpMulticastHeader(uint32_t seqNo, uint32_t dataLength, UBSHcomNetTransHeader &header)
{
    header.immData = NN_NO1;
    header.seqNo = seqNo;
    header.flags = NTH_TWO_SIDE;
    header.dataLength = dataLength;
    header.headerCrc = NetFunc::CalcHeaderCrc32(header);
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

SerResult Publisher::Initialize(uintptr_t memPool, uintptr_t periodicMgr, uint32_t ctxStoreCapacity,
                                UBSHcomNetDriverProtocol protocol)
{
    std::lock_guard<std::mutex> locker(mMgrMutex);
    mState.Set(PublisherState::PUB_NEW);

    auto *ctxStore = CreateAndInitCtxStore(ctxStoreCapacity, memPool, protocol);
    if (NN_UNLIKELY(ctxStore == nullptr)) {
        ForceUnInitialize();
        return SER_NEW_OBJECT_FAILED;
    }
    mCtxStore = ctxStore;
    mCtxMemPool = memPool;
    mProtocol = protocol;

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

    auto ctxMemPool = reinterpret_cast<NetMemPoolFixed *>(mCtxMemPool);
    if (NN_LIKELY(ctxMemPool != nullptr)) {
        ctxMemPool->DecreaseRef();
        mCtxMemPool = 0;
    }

    auto periodicMgrPtr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    if (NN_LIKELY(periodicMgrPtr != nullptr)) {
        periodicMgrPtr->DecreaseRef();
        mPeriodicMgr = 0;
    }

    mState.Set(PublisherState::PUB_DESTROY);
}

SubscriptionInfoPtr Publisher::GetSubscribeByEpId(uint64_t id)
{
    RWLockGuard guard(mRwLock);
    guard.LockRead();
    auto it = mSubscriptionMap.find(id);
    if (it != mSubscriptionMap.end()) {
        return it->second;
    }
    return nullptr;
}

void Publisher::ProcessIoInBroken()
{
    auto *periodicMgr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    if (periodicMgr != nullptr) {
        periodicMgr->ProcessIoInBroken(this);
    }
}

SerResult Publisher::PrepareIoContext(const UBSHcomNetTransOpInfo &opInfo, const MultiCastCallback *done,
                                      MultiCastIoContext *&context)
{
    auto *periodicMgr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    if (NN_UNLIKELY(periodicMgr == nullptr)) {
        DestroyCallback(done);
        NN_LOG_ERROR("Multicast IO manager is null.");
        return SER_TIMER_NOT_WORK;
    }
    context = periodicMgr->AcquireIoContext(this, done, opInfo.timeout);
    if (NN_UNLIKELY(context == nullptr)) {
        DestroyCallback(done);
        NN_LOG_ERROR("Multicast IO context pool is exhausted.");
        return SER_NEW_OBJECT_FAILED;
    }
    return SER_OK;
}

SerResult Publisher::InitIoSubscribers(MultiCastIoContext &context)
{
    PublisherContext &pubCtx = context.mPublisherCtx;
    uint32_t snapshotIndex = AcquireSubscriptionSnapshot();
    for (const auto &group : mSubscriptionSnapshots[snapshotIndex]) {
        SubscriptionInfoPtr selected = SelectSubscription(*group);
        if (selected.Get() != nullptr) {
            pubCtx.subscriberRspList.emplace_back(selected, SubscriberRspStatus::INIT);
        }
    }
    ReleaseSubscriptionSnapshot(snapshotIndex);
    if (NN_UNLIKELY(pubCtx.subscriberRspList.empty())) {
        return SER_INVALID_PARAM;
    }
    pubCtx.repliedCount = 0;
    return SER_OK;
}

SerResult Publisher::InitIoSubscriber(MultiCastIoContext &context, const std::string &remoteIp)
{
    PublisherContext &pubCtx = context.mPublisherCtx;
    uint32_t snapshotIndex = AcquireSubscriptionSnapshot();
    for (const auto &group : mSubscriptionSnapshots[snapshotIndex]) {
        if (group == nullptr || group->subscribers.empty() || group->subscribers.front()->mIp != remoteIp) {
            continue;
        }
        SubscriptionInfoPtr selected = SelectSubscription(*group);
        if (selected.Get() != nullptr) {
            pubCtx.subscriberRspList.emplace_back(selected, SubscriberRspStatus::INIT);
        }
        break;
    }
    ReleaseSubscriptionSnapshot(snapshotIndex);
    if (NN_UNLIKELY(pubCtx.subscriberRspList.empty())) {
        NN_LOG_ERROR("Failed to find multicast subscriber, remote ip:" << remoteIp << ".");
        return SER_INVALID_PARAM;
    }
    pubCtx.repliedCount = 0;
    return SER_OK;
}

uint32_t Publisher::AcquireSubscriptionSnapshot()
{
    for (;;) {
        uint32_t index = mSubscriptionSnapshotIndex.load(std::memory_order_acquire);
        mSubscriptionSnapshotReaders[index].fetch_add(1, std::memory_order_acquire);
        if (index == mSubscriptionSnapshotIndex.load(std::memory_order_acquire)) {
            return index;
        }
        mSubscriptionSnapshotReaders[index].fetch_sub(1, std::memory_order_release);
    }
}

void Publisher::ReleaseSubscriptionSnapshot(uint32_t index)
{
    mSubscriptionSnapshotReaders[index].fetch_sub(1, std::memory_order_release);
}

void Publisher::DestroyIoContext(MultiCastIoContext *context)
{
    auto *periodicMgr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    if (periodicMgr != nullptr) {
        periodicMgr->ReleaseIoContext(context);
    }
}

void Publisher::TryCompleteIoContext(MultiCastIoContext *context)
{
    if (!context->mSendReady.load(std::memory_order_acquire) ||
        context->mPublisherCtx.GetReplyCount() < context->mPublisherCtx.GetSendCount()) {
        return;
    }

    MultiCastIoState expected = MultiCastIoState::INIT;
    if (!context->mState.compare_exchange_strong(expected, MultiCastIoState::FINISHED, std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
        return;
    }

    const MultiCastCallback *callback = context->mCallback;
    context->mCallback = nullptr;
    if (callback != nullptr) {
        const_cast<MultiCastCallback *>(callback)->Run(context->mPublisherCtx);
    }
    auto *periodicMgr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    periodicMgr->CompleteIoContext(context);
}

SerResult Publisher::FinishIoSend(MultiCastIoContext *context, SerResult result)
{
    context->mSendReady.store(true, std::memory_order_release);
    if (NN_LIKELY(result != SER_MULTICAST_SEND_ALL_FAILED)) {
        TryCompleteIoContext(context);
        context->ReleaseAccess();
        return SER_OK;
    }

    MultiCastIoState expected = MultiCastIoState::INIT;
    if (!context->mState.compare_exchange_strong(expected, MultiCastIoState::FINISHED, std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
        context->ReleaseAccess();
        return SER_OK;
    }

    DestroyCallback(context->mCallback);
    context->mCallback = nullptr;
    auto *periodicMgr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    periodicMgr->CompleteIoContext(context);
    context->ReleaseAccess();
    NN_LOG_ERROR("Multicast Call all failed result " << result << ".");
    return result;
}

SerResult Publisher::Call(const UBSHcomNetTransOpInfo &opInfo, const MultiRequest &req, const MultiCastCallback *done)
{
    if (NN_UNLIKELY(GetSubscriberNum() == 0)) {
        DestroyCallback(done);
        NN_LOG_ERROR("Failed to send, no subscriber exist");
        return SER_INVALID_PARAM;
    }
    if (NN_UNLIKELY(done == nullptr)) {
        NN_LOG_ERROR("Multicast callback is null.");
        return SER_INVALID_PARAM;
    }
    MultiCastIoContext *context = nullptr;
    SerResult result = PrepareIoContext(opInfo, done, context);
    if (NN_UNLIKELY(result != SER_OK)) {
        return result;
    }
    result = InitIoSubscribers(*context);
    if (NN_UNLIKELY(result != SER_OK)) {
        DestroyIoContext(context);
        return result;
    }

    auto *periodicMgr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    periodicMgr->SubmitIoContext(context);

    uint32_t seqNo = context->SeqNo();
    UBSHcomNetTransRequest netReq(reinterpret_cast<uintptr_t>(req.data), 0, req.lkey, 0, req.size,
                                  sizeof(SerTransContext));
    SetServiceTransCtx(netReq.upCtxData, seqNo);
    result = PostSendAll(*context, netReq, seqNo, result);
    result = FinishIoSend(context, result);
    return result;
}

SerResult Publisher::Call(const UBSHcomNetTransOpInfo &opInfo, const std::string &remoteIp, const MultiRequest &req,
                          const MultiCastCallback *done)
{
    if (NN_UNLIKELY(remoteIp.empty() || GetSubscriberNum() == 0)) {
        DestroyCallback(done);
        NN_LOG_ERROR("Failed to send, remote ip is empty or no subscriber exist.");
        return SER_INVALID_PARAM;
    }
    if (NN_UNLIKELY(done == nullptr)) {
        NN_LOG_ERROR("Multicast callback is null.");
        return SER_INVALID_PARAM;
    }

    MultiCastIoContext *context = nullptr;
    SerResult result = PrepareIoContext(opInfo, done, context);
    if (NN_UNLIKELY(result != SER_OK)) {
        return result;
    }

    result = InitIoSubscriber(*context, remoteIp);
    if (NN_UNLIKELY(result != SER_OK)) {
        DestroyIoContext(context);
        return result;
    }

    auto *periodicMgr = reinterpret_cast<MultiCastPeriodicManager *>(mPeriodicMgr);
    periodicMgr->SubmitIoContext(context);

    uint32_t seqNo = context->SeqNo();
    UBSHcomNetTransRequest netReq(reinterpret_cast<uintptr_t>(req.data), 0, req.lkey, 0, req.size,
                                  sizeof(SerTransContext));
    SetServiceTransCtx(netReq.upCtxData, seqNo);
    result = PostSendAll(*context, netReq, seqNo, result);
    result = FinishIoSend(context, result);
    return result;
}

SerResult Publisher::PostSendAll(MultiCastIoContext &context, const UBSHcomNetTransRequest &netReq, uint32_t seqNo,
                                 SerResult &result)
{
    uint32_t failedCount = 0;
    PublisherContext &pubCtx = context.mPublisherCtx;
    uint32_t toSendCount = pubCtx.subscriberRspList.size();
    UBSHcomNetTransHeader header{};
    if (mProtocol == UBSHcomNetDriverProtocol::TCP) {
        PrepareTcpMulticastHeader(seqNo, netReq.size, header);
    }

    pubCtx.SetSendCount(toSendCount);
    for (auto &response : pubCtx.subscriberRspList) {
        SubscriptionInfoPtr &sub = response.mSubInfo;
        auto ep = sub.Get() == nullptr ? nullptr : sub->mEp.Get();
        if (NN_UNLIKELY(ep == nullptr)) {
            (void)pubCtx.SetResponseStatus(sub, nullptr, SubscriberRspStatus::SEND_ERROR);
            failedCount++;
            NN_LOG_ERROR("Endpoint is null, failed count " << failedCount << ".");
            continue;
        }
        if (mProtocol == UBSHcomNetDriverProtocol::TCP) {
            auto *sockEp = static_cast<NetAsyncEndpointSock *>(ep);
            result = sockEp->PostSendRawNoCpy(netReq, header);
        } else {
            result = ep->PostSendRawNoCpy(netReq, seqNo);
        }
        if (NN_UNLIKELY(result != SER_OK)) {
            (void)pubCtx.SetResponseStatus(sub, nullptr, SubscriberRspStatus::SEND_ERROR);
            NN_LOG_ERROR("Failed to send to ep " << ep->Id() << ", error: " << result << " set sub info " << sub->mName
                                                 << " to SEND_ERROR");
            failedCount++;
        }
    }

    if (failedCount == 0) {
        return SER_OK;
    }
    // 记录发送成功的数量，回调汇聚的时候用
    pubCtx.SetSendCount(toSendCount - failedCount);
    if (NN_UNLIKELY(failedCount == toSendCount)) {
        return SER_MULTICAST_SEND_ALL_FAILED;
    }
    return SER_OK;
}

void Publisher::RebuildSubscriptionGroupsLocked()
{
    mSubscriptionGroups.clear();
    for (const auto &item : mSubscriptionMap) {
        if (item.second.Get() == nullptr) {
            continue;
        }
        SubscriptionGroupPtr &group = mSubscriptionGroups[item.second->mIp];
        if (group == nullptr) {
            group = std::make_shared<SubscriptionGroup>();
        }
        group->subscribers.emplace_back(item.second);
    }

    uint32_t activeIndex = mSubscriptionSnapshotIndex.load(std::memory_order_acquire);
    uint32_t nextIndex = activeIndex ^ 1U;
    while (mSubscriptionSnapshotReaders[nextIndex].load(std::memory_order_acquire) != 0) {
        std::this_thread::yield();
    }
    SubscriptionSnapshot &snapshot = mSubscriptionSnapshots[nextIndex];
    snapshot.clear();
    snapshot.reserve(mSubscriptionGroups.size());
    for (const auto &item : mSubscriptionGroups) {
        snapshot.emplace_back(item.second);
    }
    mSubscriptionSnapshotIndex.store(nextIndex, std::memory_order_release);
}

SubscriptionInfoPtr Publisher::SelectSubscription(SubscriptionGroup &group)
{
    const auto &subs = group.subscribers;
    if (subs.empty()) {
        return nullptr;
    }
    auto index = group.rr.fetch_add(1, std::memory_order_relaxed) % subs.size();
    return subs[index];
}

bool Publisher::AddSubscription(SubscriptionInfoPtr &info)
{
    if (info == nullptr) {
        return false;
    }
    NN_LOG_DEBUG("begin to add subscribe info id :" << info->mId << " name:" << info->mName);

    RWLockGuard guard(mRwLock);
    guard.LockWrite();
    info->mEp->UpCtx(reinterpret_cast<uint64_t>(info.Get()));

    UBSHcomEpOptions epOptions;
    epOptions.tcpBlockingIo = true;
    epOptions.cbByWorkerInBlocking = false;
    info->mEp->SetEpOption(epOptions);

    mEpMap.emplace(info->mId, info->mEp);
    mSubscriptionMap.emplace(info->mId, info);
    RebuildSubscriptionGroupsLocked();
    mSubCount.fetch_add(1, std::memory_order_release);
    return true;
}

bool Publisher::DelSubscription(SubscriptionInfoPtr &info)
{
    if (info == nullptr || info->mEp == nullptr) {
        NN_LOG_ERROR("Delete subscription failed as info is invalid");
        return false;
    }
    // 此时可能还有ep正在做发送消息操作，避免core，ep通过延迟释放，此处仅仅移除subscription
    RWLockGuard guard(mRwLock);
    guard.LockWrite();
    mEpMap.erase(info->mId);
    mSubscriptionMap.erase(info->mId);
    RebuildSubscriptionGroupsLocked();
    mSubCount.store(static_cast<uint32_t>(mSubscriptionMap.size()), std::memory_order_release);
    return true;
}

uint32_t Publisher::GetSubscriberNum()
{
    uint32_t snapshotIndex = AcquireSubscriptionSnapshot();
    uint32_t count = mSubscriptionSnapshots[snapshotIndex].size();
    ReleaseSubscriptionSnapshot(snapshotIndex);
    return count;
}

std::vector<SubscriptionInfoPtr> Publisher::GetAllSubscriberInfo()
{
    RWLockGuard guard(mRwLock);
    guard.LockRead();
    std::vector<SubscriptionInfoPtr> subscriptionVec;
    subscriptionVec.reserve(mSubscriptionMap.size());
    for (const auto &sub : mSubscriptionMap) {
        if (sub.second.Get() == nullptr) {
            continue;
        }
        subscriptionVec.emplace_back(sub.second);
    }
    return subscriptionVec;
}

Publisher::Publisher(const std::string &name)
{
    mName = name;
}
} // namespace hcom
} // namespace ock
