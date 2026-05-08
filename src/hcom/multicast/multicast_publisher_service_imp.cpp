/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "multicast_publisher_service_imp.h"

#include "include/multicast_publisher_service.h"
#include "multicast_periodic_manager.h"
#include "net_common.h"
#include "net_oob.h"
#include "utils/multicast_utils.h"
#include "utils/multicast_lock_guard.h"

namespace ock {
namespace hcom {
PublisherServiceImp::~PublisherServiceImp()
{
    Stop();
}

static int DefaultOneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return 0;
}

static int DefaultRequestPosted(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return 0;
}

void PublisherServiceImp::RegisterBrokenHandler(const MulticastEpBrokenHandler &handler)
{
    mPubBrokenHandler = handler;
}

void PublisherServiceImp::RegisterPubRecvHandler(const MulticastPubReqRecvHandler &handler)
{
    mPubRecvHandler = handler;
}

void PublisherServiceImp::RegisterSendHandler(const MulticastReqPostedHandler &handler)
{
    mPubSendHandler = handler;
}

void PublisherServiceImp::RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb)
{
    mPubTLSCaCallback = cb;
}

void PublisherServiceImp::RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb)
{
    mPubTLSCertificationCallback = cb;
}

void PublisherServiceImp::RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb)
{
    mPubTLSPrivateKeyCallback = cb;
}

void PublisherServiceImp::AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount,
    const std::pair<uint32_t, uint32_t> &cpuIdsRange, int8_t priority)
{
    UBSHcomWorkerGroupInfo groupInfo;
    groupInfo.threadPriority = priority;
    groupInfo.groupId = workerGroupId;
    groupInfo.threadCount = threadCount;
    groupInfo.cpuIdsRange = cpuIdsRange;
    mCfg.AddWorkerGroup(groupInfo);
}

SerResult PublisherServiceImp::RegisterMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr)
{
    NN_ASSERT_LOG_RETURN(mDriverPtr != nullptr, NN_ERROR);
    return mDriverPtr->CreateMemoryRegion(size, mr);
}

SerResult PublisherServiceImp::RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr)
{
    NN_ASSERT_LOG_RETURN(mDriverPtr != nullptr, NN_ERROR);
    return mDriverPtr->CreateMemoryRegion(address, size, mr);
}

void PublisherServiceImp::DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr)
{
    NN_ASSERT_LOG_RETURN_VOID(mDriverPtr != nullptr);
    return mDriverPtr->DestroyMemoryRegion(mr);
}

SerResult PublisherServiceImp::ServiceRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    HcomSeqNo netSeqNo(ctx.Header().seqNo);

    PublisherContext *pubCtx = nullptr;
    mPublisher->mPubCtxStore->GetBySeqNo(netSeqNo.wholeSeq, pubCtx);
    if (NN_UNLIKELY(pubCtx == nullptr)) {
        NN_LOG_ERROR("Publisher context is nullptr, maybe timeout or broken before handle, ep Id " <<
            ctx.EndPoint()->Id() << " seqNo " << netSeqNo.wholeSeq);
        return SER_ERROR;
    }

    SubscriptionInfoPtr subscriberInfo = reinterpret_cast<SubscriptionInfo *>(ctx.EndPoint()->UpCtx());
    if (NN_UNLIKELY(subscriberInfo == nullptr)) {
        NN_LOG_ERROR("Get subscribe info failed, maybe broken then handle, ep Id " << ctx.EndPoint()->Id());
        return SER_ERROR;
    }

    // ctx和msg都是thread local的变量，在线程销毁（即接收worker线程）的时候会被销毁
    pubCtx->MarkReplied(subscriberInfo, ctx.Message());
    if (pubCtx->GetReplyCount() < pubCtx->GetSendCount()) {
        return SER_OK;
    }

    MultiCastServiceTimer *timer = nullptr;
    if (NN_UNLIKELY(mPublisher->mCtxStore->GetSeqNoAndRemove(netSeqNo.wholeSeq, timer) != SER_OK)) {
        HcomSeqNo dumpSeq(netSeqNo.wholeSeq);
        NN_LOG_WARN("publisher fetch " << dumpSeq.ToString() << " context failed");
        return SER_ERROR;
    }
    timer->RunCallBack(*pubCtx);
    timer->MarkFinished();
    timer->DecreaseRef();

    // 仅在所有请求都到了之后才销毁
    PublisherContext *context = nullptr;
    if (NN_UNLIKELY(mPublisher->mPubCtxStore->GetSeqNoAndRemove(netSeqNo.wholeSeq, context) != SER_OK)) {
        HcomSeqNo dumpSeq(netSeqNo.wholeSeq);
        NN_LOG_ERROR("publisher fetch " << dumpSeq.ToString() << " context failed");
        return SER_ERROR;
    }
    mPublisher->mPubCtxStore->Return(context);
    return SER_OK;
}

MulticastConfig &PublisherServiceImp::GetConfig()
{
    return mCfg;
}

void PublisherServiceImp::RegisterSubscriptionExceptionHandler(const SubscriptionExceptionHandler &handler)
{
    mSubscriptionExceptionHandler = handler;
}

void PublisherServiceImp::DirectEraseEp(UBSHcomNetEndpointPtr ep)
{
    NN_LOG_INFO("erase ep from map " << ep->Id());
    RWLockGuard(mPublisher->mRwLock).LockWrite();
    mPublisher->mEpMap.erase(ep->Id());
    mPublisher->mSubCount--;
}

void PublisherServiceImp::EraseEpCb(PublisherContext &ctx, uintptr_t epPtr)
{
    auto ep = reinterpret_cast<UBSHcomNetEndpoint *>(epPtr);
    if (ep == nullptr) {
        NN_LOG_WARN("Erase ep is null");
        return;
    }

    DirectEraseEp(ep);
}

SerResult PublisherServiceImp::DelayEraseEp(const UBSHcomNetEndpointPtr &ep, uint16_t delayTime)
{
    NN_LOG_INFO("delay erase ep " << ep->Id());
    auto epPtr = reinterpret_cast<uintptr_t>(ep.Get());
    MultiCastCallback *newCallback =
        NewMultiCastCallback(&PublisherServiceImp::EraseEpCb, this, std::placeholders::_1, epPtr);
    if (newCallback == nullptr) {
        NN_LOG_ERROR("Failed to new callback obj.");
        return SER_NEW_OBJECT_FAILED;
    }

    auto ctxStorePtr = mPublisher->mCtxStore;
    if (ctxStorePtr == nullptr) {
        NN_LOG_ERROR("Failed to get channel ctx store.");
        delete newCallback;
        return SER_ERROR;
    }
    auto timerPtr = ctxStorePtr->GetCtxObj<HcomServiceTimer>();
    if (NN_UNLIKELY(timerPtr == nullptr)) {
        NN_LOG_ERROR("Failed to get context object from memory pool.");
        delete newCallback;
        return SER_NEW_OBJECT_FAILED;
    }

    auto timer = new (timerPtr)MultiCastServiceTimer(mPublisher.Get(), ctxStorePtr, delayTime,
        reinterpret_cast<uintptr_t>(newCallback), MultiCastSyncCBType::BROKEN);

    uint32_t seqNo = NN_NO0;
    auto result = ctxStorePtr->PutAndGetSeqNo(timer, seqNo);
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Failed to generate seqNo by context store pool.");
        ctxStorePtr->Return(timerPtr);
        delete newCallback;
        return SER_NEW_OBJECT_FAILED;
    }

    timer->IncreaseRef();
    timer->SeqNo(seqNo);

    result = mPeriodicMgr->AddTimer(timer);
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Failed to add timer in for timeout control.");
        timer->EraseSeqNo();
        ctxStorePtr->Return(timerPtr);
        delete newCallback;
        return result;
    }
    timer->IncreaseRef();
    return SER_OK;
}

SerResult PublisherServiceImp::EpBrokenCallback(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("BEGIN call ep broken callback " << ep->Id());
    if (mPubBrokenHandler == nullptr) {
        NN_LOG_ERROR("Ep broken callback is null");
        return SER_ERROR;
    }
    mPublisher->ProcessIoInBroken();
    mPubBrokenHandler(ep);
    return DelayEraseEp(ep, NN_NO1);
}

SerResult PublisherServiceImp::NewSubscriptionCallback(const std::string &ipPort,
    const ock::hcom::UBSHcomNetEndpointPtr &ep, const std::string &payload)
{
    if (mPublisher.Get() == nullptr) {
        NN_LOG_ERROR("new subscriber, but publisher is not ready");
        return SER_ERROR;
    }

    if (mPublisher->mSubCount == mCfg.GetMaxSubscriberNum()) {
        NN_LOG_ERROR("subscriber num is over limit " << mCfg.GetMaxSubscriberNum());
        return SER_ERROR;
    }

    NN_LOG_INFO("new ep request!" << ipPort);
    std::string ip;
    uint16_t port;
    if (!NetFunc::NN_ConvertIpAndPort(ipPort, ip, port)) {
        NN_LOG_ERROR("parse ip and port failed");
        return SER_INVALID_PARAM;
    }

    SubscriptionInfoPtr info = new (std::nothrow) SubscriptionInfo(ep.Get()->Id(), ipPort, ip, port, ep);
    if (info == nullptr) {
        NN_LOG_ERROR("allocate subscription info failed");
        return SER_MALLOC_FAILED;
    }

    auto res = mNewSubScriptionHandler(info);
    if (NN_UNLIKELY(res != SER_OK)) {
        NN_LOG_ERROR("call NewSubScriptionHandler failed!, res = " << res);
        return res;
    }

    return 0;
}

SerResult PublisherServiceImp::InitDriver()
{
    UBSHcomNetDriver *driver = UBSHcomNetDriver::Instance(mCfg.GetProtocol(),
        mCfg.GetName(), mCfg.GetStartOobServer());
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create driver for service " << mCfg.GetName());
        return SER_ERROR;
    }
    mDriverPtr = driver;

    if (mCfg.GetStartOobServer()) {
        for (auto &option : mCfg.GetOobOption()) {
            driver->AddOobOptions(option.second);
        }
    }

    UBSHcomNetDriverOptions driverOpt;
    if (!mCfg.FillNetDriverOpt(driverOpt)) {
        return SER_ERROR;
    }
    driverOpt.SetWorkerGroupsInfo(mCfg.GetWorkerGroupInfo());
    mDriverPtr->RegisterNewReqHandler(
        std::bind(&PublisherServiceImp::ServiceRequestReceived, this, std::placeholders::_1));
    mDriverPtr->RegisterReqPostedHandler(DefaultRequestPosted);
    mDriverPtr->RegisterOneSideDoneHandler(DefaultOneSideDone);

    mDriverPtr->RegisterNewEPHandler(std::bind(&PublisherServiceImp::NewSubscriptionCallback, this,
        std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    mDriverPtr->RegisterEPBrokenHandler(std::bind(&PublisherServiceImp::EpBrokenCallback, this, std::placeholders::_1));

    if (driverOpt.enableTls) {
        mDriverPtr->RegisterTLSCaCallback(mPubTLSCaCallback);
        mDriverPtr->RegisterTLSCertificationCallback(mPubTLSCertificationCallback);
        mDriverPtr->RegisterTLSPrivateKeyCallback(mPubTLSPrivateKeyCallback);
    }

    int32_t res = mDriverPtr->Initialize(driverOpt);
    if (NN_UNLIKELY(res != SER_OK)) {
        Stop();
        return res;
    }

    return SER_OK;
}

SerResult PublisherServiceImp::CreateResource(uint32_t threadNum)
{
    if (NN_UNLIKELY(threadNum == NN_NO0 || threadNum > NN_NO4)) {
        NN_LOG_ERROR("Invalid periodicThreadNum " << threadNum << ", must range in [1, 4]");
        return SER_INVALID_PARAM;
    }

    int cpuId = -1;
    if (GetConfig().GetPeriodicCpuId() > 0) {
        cpuId = GetConfig().GetPeriodicCpuId();
    }

    MultiCastPeriodicManagerPtr periodicMgr =
        new (std::nothrow) MultiCastPeriodicManager(NN_NO1, GetConfig().GetName(), cpuId);
    if (NN_UNLIKELY(periodicMgr.Get() == nullptr)) {
        NN_LOG_ERROR("Create periodic manager failed");
        return SER_NEW_OBJECT_FAILED;
    }

    if (NN_UNLIKELY(periodicMgr->Start() != SER_OK)) {
        NN_LOG_ERROR("Start periodic manager failed");
        return SER_TIMER_NOT_WORK;
    }

    NetMemPoolFixedOptions options = {};
    options.superBlkSizeMB = NN_NO1;
    options.minBlkSize = NN_NO64;
    options.tcExpandBlkCnt = NN_NO256;
    NetMemPoolFixedPtr ctxMemPool = new (std::nothrow) NetMemPoolFixed("PublisherServiceCtxTimer", options);
    if (NN_UNLIKELY(ctxMemPool.Get() == nullptr)) {
        NN_LOG_ERROR("Create mem pool failed");
        return SER_NEW_OBJECT_FAILED;
    }

    auto ret = ctxMemPool->Initialize();
    if (NN_UNLIKELY(ret != SER_OK)) {
        NN_LOG_ERROR("Init mem pool failed");
        return SER_NEW_OBJECT_FAILED;
    }

    NetMemPoolFixedOptions pubOptions = {};
    pubOptions.superBlkSizeMB = NN_NO1;
    pubOptions.minBlkSize = NN_NO32;
    pubOptions.tcExpandBlkCnt = NN_NO256;
    NetMemPoolFixedPtr pubCtxMemPool = new (std::nothrow) NetMemPoolFixed("PublisherServiceCtxSubInfo", options);
    if (NN_UNLIKELY(pubCtxMemPool.Get() == nullptr)) {
        NN_LOG_ERROR("Create mem pool failed");
        return SER_NEW_OBJECT_FAILED;
    }

    auto result = pubCtxMemPool->Initialize();
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Init mem pool failed");
        return SER_NEW_OBJECT_FAILED;
    }

    mPeriodicMgr = periodicMgr;
    mCtxMemPool = ctxMemPool;
    mPubCtxMemPool = pubCtxMemPool;
    return SER_OK;
}

SerResult PublisherServiceImp::Start()
{
    std::lock_guard<std::mutex> locker(mStartMutex);
    SerResult result = SER_OK;
    if (mStarted) {
        return SER_OK;
    }

    if (NN_UNLIKELY((result = mCfg.ValidateMulticastServiceOption()) != SER_OK)) {
        NN_LOG_ERROR("Invalid service info, res:" << result);
        return result;
    }

    if (NN_UNLIKELY(mNewSubScriptionHandler == nullptr)) {
        NN_LOG_ERROR("new subscription handler is nullptr");
        return SER_INVALID_PARAM;
    }

    mCtxStoreCapacity = NetFunc::NN_GetLongEnv("HCOM_CTX_CAPACITY", NN_NO128, NN_NO16777216, NN_NO2097152);
    result = CreateResource(GetConfig().GetPeriodicThreadNum());
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Create resources failed in service start");
        return result;
    }

    if (NN_UNLIKELY((result = InitDriver()) != SER_OK)) {
        NN_LOG_ERROR("Driver start failed, res:" << result);
        return result;
    }

    if (NN_UNLIKELY((result = StartDriver()) != SER_OK)) {
        NN_LOG_ERROR("Driver start failed, res:" << result);
        return result;
    }

    if (NN_LIKELY(!mCfg.GetDeviceIpMask().empty())) {
        mOobIp = MulticastUtils::GetFilteredDeviceIP(mCfg.GetDeviceIpMask().at(0));
    }

    mStarted = true;
    return result;
}

SerResult PublisherServiceImp::StartDriver()
{
    int32_t result = SER_OK;
    result = mDriverPtr->Start();
    if (NN_UNLIKELY(result != SER_OK)) {
        Stop();
        return result;
    }

    if (!mCfg.GetStartOobServer()) {
        return SER_OK;
    }

    for (uint32_t i = 0; i < mOobServers.size(); i++) {
        if (NN_UNLIKELY(mOobServers[i] == nullptr)) {
            for (uint32_t j = 0; j < i; j++) {
                mOobServers[j]->Stop();
            }
            return result;
        }
        if ((result = mOobServers[i]->Start()) != SER_OK) {
            for (uint32_t j = 0; j < i; j++) {
                mOobServers[j]->Stop();
            }
            return result;
        }
    }
    return SER_OK;
}

void PublisherServiceImp::Stop()
{
    std::lock_guard<std::mutex> locker(mStartMutex);
    if (!mStarted) {
        return;
    }
    for (auto &server : mOobServers) {
        server->Stop();
        server->DecreaseRef();
    }
    mOobServers.clear();

    mDriverPtr->Stop();
    mDriverPtr->UnInitialize();

    if (mPeriodicMgr.Get() != nullptr) {
        mPeriodicMgr->Stop();
        mPeriodicMgr.Set(nullptr);
    }

    if (mCtxMemPool.Get() != nullptr) {
        mCtxMemPool.Set(nullptr);
    }

    if (mPubCtxMemPool.Get() != nullptr) {
        mPubCtxMemPool.Set(nullptr);
    }

    UBSHcomNetDriver::DestroyInstance(mDriverPtr->Name());

    mStarted = false;
}

SerResult PublisherServiceImp::Bind(const std::string &listenerUrl,
                                    const NewSubscriptionHandler &handler, const int cpuId)
{
    if (NN_UNLIKELY(listenerUrl.empty())) {
        NN_LOG_ERROR("Invalid url: " << listenerUrl);
        return SER_INVALID_PARAM;
    }
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("handler is nullptr");
        return SER_INVALID_PARAM;
    }

    NetProtocol protocol;
    std::string url;
    if (NN_UNLIKELY(!NetFunc::NN_SplitProtoUrl(listenerUrl, protocol, url))) {
        NN_LOG_ERROR("Invalid url, should be like tcp://127.0.0.1:9981");
        return SER_INVALID_PARAM;
    }

    if (protocol != NetProtocol::NET_TCP) {
        NN_LOG_ERROR("Invalid protocal, only support tcp, url should be like tcp://127.0.0.1:9981");
        return SER_INVALID_PARAM;
    }

    int listenCpuId = -1;
    if (cpuId > 0) {
        listenCpuId = cpuId;
    }

    mCfg.SetStartOobServer(true);
    mCfg.SetOobType(NET_OOB_TCP);
    mNewSubScriptionHandler = handler;
    return AddTcpOobListener(url, listenCpuId);
}

SerResult PublisherServiceImp::AddTcpOobListener(const std::string &url, int cpuId, uint16_t workerCount)
{
    std::string ip;
    uint16_t port;
    if (NN_UNLIKELY(!NetFunc::NN_ConvertIpAndPort(url, ip, port))) {
        NN_LOG_ERROR("Invalid url, should be like 127.0.0.1:9981");
        return SER_INVALID_PARAM;
    }

    UBSHcomNetOobListenerOptions option;
    if (NN_UNLIKELY(!option.SetWithCpuId(ip, port, workerCount, cpuId))) {
        NN_LOG_ERROR("Oob Tcp listener set failed");
        return SER_INVALID_PARAM;
    }

    if (NN_UNLIKELY(mCfg.GetOobOption().find(url) != mCfg.GetOobOption().end())) {
        NN_LOG_WARN("Duplicated listen ip/port adding to driver Manager " << mCfg.GetName() << ", ignored");
        return SER_INVALID_PARAM;
    }

    mCfg.AddOobOption(url, option);
    return SER_OK;
}

SerResult PublisherServiceImp::CreatePublisher(NetRef<Publisher> &publisher)
{
    auto tmpPub = new (std::nothrow) Publisher(mCfg.GetName());
    if (tmpPub == nullptr) {
        NN_LOG_ERROR("malloc for publisher failed!");
        return SER_NEW_OBJECT_FAILED;
    }

    if (NN_UNLIKELY(tmpPub->Initialize(reinterpret_cast<uintptr_t>(mCtxMemPool.Get()),
        reinterpret_cast<uintptr_t>(mPubCtxMemPool.Get()), reinterpret_cast<uintptr_t>(mPeriodicMgr.Get()),
        mCtxStoreCapacity, mCfg.GetProtocol()))) {
        NN_LOG_ERROR("Failed to initialize publisher");
        delete tmpPub;
        return SER_NEW_OBJECT_FAILED;
    }

    mPublisher = tmpPub;
    publisher = tmpPub;
    return SER_OK;
}

void PublisherServiceImp::DestroyPublisher(NetRef<Publisher> &publisher)
{
    if (publisher != nullptr) {
        publisher.Set(nullptr);
    }
}
}
}
