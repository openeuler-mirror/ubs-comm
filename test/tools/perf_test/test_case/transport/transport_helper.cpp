/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "test_case/transport/transport_helper.h"
#include "common/perf_test_logger.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;

static int NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep, const std::string &payload)
{
    return 0;
}

static void EndPointBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep)
{
    return;
}

static int RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return 0;
}

static int RequestPosted(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return 0;
}

static int OneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return 0;
}

TransportHelper::TransportHelper(const PerfTestConfig &cfg)
{
    mCfg = cfg;
    mDriver = nullptr;
    // 回调函数提供默认空实现，简化测试用例
    mNewEpHandler = NewEndPoint;
    mEpBrokenHandler = EndPointBroken;
    mReqRecvHandler = RequestReceived;
    mOneSideDoneHandler = OneSideDone;
    mReqPostedHandler = RequestPosted;
}

void TransportHelper::RegisterNewEPHandler(const NewEpHandler &handler)
{
    mNewEpHandler = handler;
}

void TransportHelper::RegisterEpBrokenHandler(const EpBrokenHandler &handler)
{
    mEpBrokenHandler = handler;
}

void TransportHelper::RegisterReqRecvHandler(const ReqRecvHandler &handler)
{
    mReqRecvHandler = handler;
}

void TransportHelper::RegisterOneSideDoneHandler(const OneSideDoneHandler &handler)
{
    mOneSideDoneHandler = handler;
}

void TransportHelper::RegisterReqPostedHandler(const ReqPostedHandler &handler)
{
    mReqPostedHandler = handler;
}

bool TransportHelper::CreateNetDriver()
{
    if (mDriver != nullptr) {
        LOG_WARN("UBSHcomNetDriver already created");
        return true;
    }

    mDriver = UBSHcomNetDriver::Instance(mCfg.GetProtocol(), "PerfTest", mCfg.GetIsServer());

    UBSHcomNetDriverOptions options{};
    FillNetDriverOption(options);
    mDriver->RegisterNewEPHandler(mNewEpHandler);
    mDriver->RegisterEPBrokenHandler(mEpBrokenHandler);
    mDriver->RegisterReqPostedHandler(mReqPostedHandler);
    mDriver->RegisterNewReqHandler(mReqRecvHandler);
    mDriver->RegisterOneSideDoneHandler(mOneSideDoneHandler);

    if (mCfg.GetIsServer()) {
        mDriver->OobIpAndPort(mCfg.GetOobIp(), mCfg.GetOobPort());
    }

    int result = 0;
    if ((result = mDriver->Initialize(options)) != 0) {
        LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    LOG_DEBUG("UBSHcomNetDriver initialized");

    if ((result = mDriver->Start()) != 0) {
        LOG_ERROR("failed to start UBSHcomNetDriver " << result);
        return false;
    }

    LOG_DEBUG("UBSHcomNetDriver started");
    return true;
}

void TransportHelper::DestroyNetDriver()
{
    if (mDriver != nullptr) {
        if (!mMrVector.empty()) {
            for (auto mr : mMrVector) {
                mDriver->DestroyMemoryRegion(mr);
            }
            mMrVector.clear();
        }
        mDriver->Stop();
        mDriver->UnInitialize();
        UBSHcomNetDriver::DestroyInstance(mDriver->Name());
        mDriver = nullptr;
    }
}

bool TransportHelper::FillNetDriverOption(ock::hcom::UBSHcomNetDriverOptions &opts)
{
    opts.mode = UBSHcomNetDriverWorkingMode::NET_BUSY_POLLING;
    opts.mrSendReceiveSegSize = MAX_MESSAGE_SIZE + HCOM_HEADER_SIZE;
    opts.mrSendReceiveSegCount = NN_NO2048;
    opts.pollingBatchSize = 16;
    PERF_TEST_TYPE type = mCfg.GetType();
    if (type == PERF_TEST_TYPE::TRANSPORT_SEND_BW) {
        // 为TRANSPORT_SEND_BW模式时，留一个oneSide wr给心跳，剩余都用于send wr
        opts.qpSendQueueSize = 1024;
        opts.qpReceiveQueueSize = 1024;
        opts.prePostReceiveSizePerQP = 1023;
    }
    opts.SetNetDeviceIpMask(mCfg.GetIpMask());
    opts.SetWorkerGroups("1");
    opts.enableTls = 0;
    if (mCfg.GetCpuId() != -1) {
        std::string str = std::to_string(mCfg.GetCpuId()) + "-" + std::to_string(mCfg.GetCpuId());
        opts.SetWorkerGroupsCpuSet(str);
    }
    return true;
}

bool TransportHelper::CreateMemoryRegion(MrInfo &mrInfo)
{
    if (mDriver == nullptr) {
        return false;
    }

    UBSHcomNetMemoryRegionPtr mr;
    // 按照最大包大小申请内存，以支持同时测试多个不同大小的包
    auto result = mDriver->CreateMemoryRegion(MAX_MESSAGE_SIZE, mr);
    if (result != 0) {
        LOG_ERROR("Create memory region failed");
        return false;
    }

    mrInfo.lAddress = mr->GetAddress();
    mrInfo.lKey = mr->GetLKey();
    mrInfo.size = MAX_MESSAGE_SIZE;
    mMrVector.emplace_back(mr);
    LOG_DEBUG("register addr: " << mrInfo.lAddress << ", lKey = " << mrInfo.lKey << ", size = " << MAX_MESSAGE_SIZE);
    return true;
}
}
}