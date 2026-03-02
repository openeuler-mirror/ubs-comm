/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "multicast_config_imp.h"
#include "net_common.h"
#include "net_oob.h"
#include "utils/multicast_utils.h"
#include "multicast_subscriber_service_imp.h"

namespace ock {
namespace hcom {
static int DefaultNewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    NN_LOG_INFO("new ep request!");
    return 0;
}

int SubscriberServiceImp::ServiceRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    SubscriberContext context(ctx);
    mSubscribeRecvHandler(context);
    return 0;
}

static int DefaultRequestPosted(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return 0;
}

static int DefaultOneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return 0;
}

void SubscriberServiceImp::ServiceEndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    if (mEpBrokenHandler == nullptr) {
        NN_LOG_WARN("ep broken handler is nullptr!");
        return;
    }
    mEpBrokenHandler(ep);
}

SerResult SubscriberServiceImp::InitDriver()
{
    UBSHcomNetDriver *driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA,
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
    mCfg.FillNetDriverOpt(driverOpt);
    uint16_t driverIdx = 0;
    driverOpt.SetWorkerGroupsInfo(mCfg.GetWorkerGroupInfo());
    mDriverPtr->RegisterNewReqHandler(
        std::bind(&SubscriberServiceImp::ServiceRequestReceived, this, std::placeholders::_1));
    mDriverPtr->RegisterReqPostedHandler(DefaultRequestPosted);
    mDriverPtr->RegisterOneSideDoneHandler(DefaultOneSideDone);

    mDriverPtr->RegisterNewEPHandler(DefaultNewEndPoint);
    mDriverPtr->RegisterEPBrokenHandler(
        std::bind(&SubscriberServiceImp::ServiceEndPointBroken, this, std::placeholders::_1));
    
    if (driverOpt.enableTls) {
        mDriverPtr->RegisterTLSCaCallback(mSubTLSCaCallback);
        mDriverPtr->RegisterTLSCertificationCallback(mSubTLSCertificationCallback);
        mDriverPtr->RegisterTLSPrivateKeyCallback(mSubTLSPrivateKeyCallback);
    }

    int32_t res = mDriverPtr->Initialize(driverOpt);
    if (NN_UNLIKELY(res != SER_OK)) {
        Stop();
        return res;
    }

    return SER_OK;
}

SerResult SubscriberServiceImp::Start()
{
    std::lock_guard<std::mutex> locker(mStartMutex);
    int32_t result = SER_OK;
    if (mStarted) {
        return SER_OK;
    }

    if (NN_UNLIKELY((result = mCfg.ValidateMulticastServiceOption()) != SER_OK)) {
        NN_LOG_ERROR("Invalid service info, res:" << result);
        return result;
    }

    if (NN_UNLIKELY((result = InitDriver()) != SER_OK)) {
        NN_LOG_ERROR("Driver init failed, res:" << result);
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

SerResult SubscriberServiceImp::StartDriver()
{
    SerResult result = SER_OK;
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

void SubscriberServiceImp::Stop()
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
    UBSHcomNetDriver::DestroyInstance(mDriverPtr->Name());

    mStarted = false;
}

SubscriberServiceImp::~SubscriberServiceImp()
{
    Stop();
}

SerResult SubscriberServiceImp::CreateSubscriber(const std::string &serverUrl, NetRef<Subscriber> &subscriber)
{
    if (!mStarted) {
        NN_LOG_ERROR("Failed to validate state as service is not started");
        return SER_STOP;
    }

    if (NN_UNLIKELY(NetFunc::NN_ValidateUrl(serverUrl) != NN_OK)) {
        NN_LOG_ERROR("Invalid url");
        return NN_PARAM_INVALID;
    }

    std::string ip;
    uint16_t port = 0;
    if (!MulticastUtils::ParseUrl(serverUrl, ip, port)) {
        NN_LOG_WARN("Invalid url, url:" << serverUrl);
        return NN_INVALID_PARAM;
    }

    UBSHcomNetEndpointPtr ep;
    auto result = mDriverPtr->Connect(serverUrl, "multicast", ep, 0, mCfg.GetPublisherWkrGroupNo(), 0, 0);
    if (NN_UNLIKELY(result != SER_OK)) {
        NN_LOG_ERROR("Connect to " << serverUrl << " failed, errno = " << result);
        return result;
    }

    auto *tmp = new (std::nothrow) Subscriber(ip, port, ep);
    if (NN_UNLIKELY(tmp == nullptr)) {
        NN_LOG_ERROR("Create Subscriber failed!");
        return SER_NEW_OBJECT_FAILED;
    }
    subscriber = tmp;
    return SER_OK;
}

void SubscriberServiceImp::DestroySubscriber(const NetRef<Subscriber> &subscriber)
{
    if (subscriber == nullptr) {
        return;
    }
    subscriber->GetEp()->Close();
    // NetDriver的mEndPoints记录了ep信息，需要做清理
    if (mDriverPtr) {
        mDriverPtr->DestroyEndpoint(subscriber->GetEp());
    }
    subscriber->DecreaseRef();
}

MulticastConfig &SubscriberServiceImp::GetConfig()
{
    return mCfg;
}

void SubscriberServiceImp::RegisterRecvHandler(const MulticastReqRecvHandler &recvHandler)
{
    mSubscribeRecvHandler = recvHandler;
}

void SubscriberServiceImp::RegisterBrokenHandler(const MulticastEpBrokenHandler &handler)
{
    mEpBrokenHandler = handler;
}

void SubscriberServiceImp::RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb)
{
    mSubTLSCaCallback = cb;
}

void SubscriberServiceImp::RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb)
{
    mSubTLSCertificationCallback = cb;
}

void SubscriberServiceImp::RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb)
{
    mSubTLSPrivateKeyCallback = cb;
}
}
}
