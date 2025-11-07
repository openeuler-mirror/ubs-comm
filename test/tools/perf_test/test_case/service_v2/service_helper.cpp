/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "test_case/service_v2/service_helper.h"
#include "common/perf_test_logger.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;

static int NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch, const std::string &payload)
{
    return 0;
}

static void ChannelBroken(const ock::hcom::UBSHcomChannelPtr &ch)
{
    return;
}

static int RequestReceived(ock::hcom::UBSHcomServiceContext &ctx)
{
    return 0;
}

static int RequestPosted(const ock::hcom::UBSHcomServiceContext &ctx)
{
    return 0;
}

static int OneSideDone(const ock::hcom::UBSHcomServiceContext &ctx)
{
    return 0;
}

ServiceHelper::ServiceHelper(const PerfTestConfig &cfg)
{
    mCfg = cfg;
    mService = nullptr;
    // 回调函数提供默认空实现，简化测试用例
    mNewChHandler = NewChannel;
    mChBrokenHandler = ChannelBroken;
    mRecvHandler = RequestReceived;
    mSendHandler = RequestPosted;
    mOneSideDoneHandler = OneSideDone;
}

void ServiceHelper::RegisterNewChHandler(const UBSHcomServiceNewChannelHandler &handler)
{
    mNewChHandler = handler;
}

void ServiceHelper::RegisterChBrokenHandler(const UBSHcomServiceChannelBrokenHandler &handler)
{
    mChBrokenHandler = handler;
}

void ServiceHelper::RegisterRecvHandler(const UBSHcomServiceRecvHandler &handler)
{
    mRecvHandler = handler;
}

void ServiceHelper::RegisterSendHandler(const UBSHcomServiceSendHandler &handler)
{
    mSendHandler = handler;
}

void ServiceHelper::RegisterOneSideDoneHandler(const UBSHcomServiceOneSideDoneHandler &handler)
{
    mOneSideDoneHandler = handler;
}

bool ServiceHelper::CreateService()
{
    if (mService != nullptr) {
        LOG_WARN("UBSHcomNetDriver already created");
        return true;
    }
    std::string name;
    if (mCfg.GetIsServer()) {
        name = "ServicePerfTest_server";
    } else {
        name = "ServicePerfTest_client";
    }

    UBSHcomServiceOptions options;
    options.maxSendRecvDataSize = MAX_MESSAGE_SIZE + HCOM_HEADER_SIZE;
    options.workerGroupMode = ock::hcom::NET_BUSY_POLLING;
    if (mCfg.GetCpuId() != -1) {
        options.workerGroupCpuIdsRange = { mCfg.GetCpuId(), mCfg.GetCpuId() };
    }

    mService = UBSHcomService::Create(mCfg.GetProtocol(), name, options);
    if (mService == nullptr) {
        LOG_ERROR("Failed to create service");
        return false;
    }

    mService->SetDeviceIpMask({ mCfg.GetIpMask() });

    mService->RegisterRecvHandler(mRecvHandler);
    mService->RegisterChannelBrokenHandler(mChBrokenHandler, ock::hcom::UBSHcomChannelBrokenPolicy::BROKEN_ALL);
    mService->RegisterSendHandler(mSendHandler);
    mService->RegisterOneSideHandler(mOneSideDoneHandler);

    if (mCfg.GetIsServer()) {
        mService->Bind("tcp://" + mCfg.GetOobIp() + ":" + std::to_string(mCfg.GetOobPort()), mNewChHandler);
    }

    UBSHcomTlsOptions tlsOptions;
    tlsOptions.enableTls = false;
    mService->SetTlsOptions(tlsOptions);

    int result = 0;

    if ((result = mService->Start()) != 0) {
        LOG_ERROR("Failed to start NetService " << result);
        return false;
    }

    LOG_DEBUG("NetService started");
    return true;
}

void ServiceHelper::DestroyService()
{
    std::string name;
    if (mCfg.GetIsServer()) {
        name = "ServicePerfTest_server";
    } else {
        name = "ServicePerfTest_client";
    }

    if (mService != nullptr) {
        if (!mMrVector.empty()) {
            for (auto mr : mMrVector) {
                mService->DestroyMemoryRegion(mr);
            }
            mMrVector.clear();
        }
        UBSHcomService::Destroy(name);
        mService = nullptr;
    }
}

bool ServiceHelper::CreateMemoryRegion(RegMrInfo &mrInfo)
{
    if (mService == nullptr) {
        return false;
    }

    UBSHcomRegMemoryRegion mr;
    // 按照最大包大小申请内存，以支持同时测试多个不同大小的包
    auto result = mService->RegisterMemoryRegion(MAX_MESSAGE_SIZE, mr);
    if (result != 0) {
        LOG_ERROR("Create memory region failed");
        return false;
    }

    mrInfo.lAddress = mr.GetAddress();
    mr.GetMemoryKey(mrInfo.lKey);
    mrInfo.size = MAX_MESSAGE_SIZE;
    mMrVector.emplace_back(mr);
    return true;
}
}
}
