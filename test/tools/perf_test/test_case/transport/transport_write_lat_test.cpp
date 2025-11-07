/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "securec.h"

#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "transport_write_lat_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_WRITE_LAT = 3;
int TransportWriteLatTest::NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    mEp = ep;
    isConnect.store(true);
    LOG_DEBUG("new connection from " << ipPort << " !");
    return 0;
}

int TransportWriteLatTest::RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    int result = 0;
    if (memcpy_s(&mPeerMrInfo, sizeof(mPeerMrInfo), ctx.Message()->Data(), ctx.Message()->DataLen()) != 0) {
        LOG_ERROR("memcpy_s failed");
        return -1;
    }
    if (!mCfg.GetIsServer()) {
        // client
        sem_post(&mSem);
        return result;
    }
    // server
    UBSHcomNetTransRequest rsp((void *)(&mPollMrInfo), sizeof(mPollMrInfo), 0);
    if ((result = mEp->PostSend(OP_CODE_WRITE_LAT, rsp)) != 0) {
        LOG_ERROR("Failed to post message to data to server, result " << result);
    }
    sem_post(&mSem);
    return result;
}

int TransportWriteLatTest::OneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    ccnt.fetch_add(1);
    return 0;
}

int TransportWriteLatTest::EpBroken(const ock::hcom::UBSHcomNetEndpointPtr &ep)
{
    isConnect.store(false);
    return 0;
}

bool TransportWriteLatTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create UBSHcomNetDriver
    NewEpHandler funcNewEndpoint = bind(&TransportWriteLatTest::NewEndPoint, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);
    ReqRecvHandler funcReqReceived = bind(&TransportWriteLatTest::RequestReceived, this, std::placeholders::_1);
    OneSideDoneHandler funcOneSide = bind(&TransportWriteLatTest::OneSideDone, this, std::placeholders::_1);
    EpBrokenHandler funcBrokenEp = bind(&TransportWriteLatTest::EpBroken, this, std::placeholders::_1);

    mHelper.RegisterNewEPHandler(funcNewEndpoint);
    mHelper.RegisterReqRecvHandler(funcReqReceived);
    mHelper.RegisterOneSideDoneHandler(funcOneSide);
    mHelper.RegisterEpBrokenHandler(funcBrokenEp);
    if (!mHelper.CreateNetDriver()) {
        goto ERROR_HANDLE;
    }

    if (!RegMemory()) {
        LOG_ERROR("register memory failed");
        goto ERROR_HANDLE;
    }

    // client connect to server
    if (!mCfg.GetIsServer()) {
        if (!Connect()) {
            LOG_ERROR("client connect failed");
            goto ERROR_HANDLE;
        }
        if (!ExchangeAddress()) {
            LOG_ERROR("client exchange address failed");
            goto ERROR_HANDLE;
        }
    }

    return true;

ERROR_HANDLE:
    mHelper.DestroyNetDriver();
    sem_destroy(&mSem);
    return false;
}

bool TransportWriteLatTest::RegMemory()
{
    if (!mHelper.CreateMemoryRegion(mPostMrInfo)) {
        LOG_ERROR("Create send memoryRegion failed");
        return false;
    }
    if (!mHelper.CreateMemoryRegion(mPollMrInfo)) {
        LOG_ERROR("Create receive memoryRegion failed");
        return false;
    }
    return true;
}

bool TransportWriteLatTest::ExchangeAddress()
{
    if (mEp == nullptr) {
        LOG_ERROR("Exchange address failed, ep is nullptr!");
        return false;
    }
    UBSHcomNetTransRequest req((void *)(&mPollMrInfo), sizeof(mPollMrInfo), 0);
    if (mEp->PostSend(OP_CODE_WRITE_LAT, req) != 0) {
        LOG_ERROR("Failed to exchange address to data to server");
        return false;
    }
    sem_wait(&mSem);
    return true;
}

void TransportWriteLatTest::UnInitialize()
{
    if (mEp != nullptr) {
        mEp->Close();
        mEp.Set(nullptr);
    }

    mHelper.DestroyNetDriver();
    sem_destroy(&mSem);
}

bool TransportWriteLatTest::Connect()
{
    auto driver = mHelper.GetNetDriver();
    if (driver == nullptr) {
        LOG_ERROR("connect failed, net driver is nullptr!");
        return false;
    }
    int res = driver->Connect(mCfg.GetOobIp(), mCfg.GetOobPort(), "write_test", mEp, 0);
    if (res != 0) {
        LOG_ERROR("connect failed, error code: " << res);
        return false;
    }
    return true;
}

bool TransportWriteLatTest::RunTest(PerfTestContext *ctx)
{
    // ctx会记录测试中每个Iteration耗时，故每次使用不同的ctx
    SetPerfTestContext(ctx);
    if (mCfg.GetIsServer() && !isConnect.load()) {
        // server等到地址交换结束
        sem_wait(&mSem);
    }

    mReq.lAddress = mPostMrInfo.lAddress;
    mReq.rAddress = mPeerMrInfo.lAddress;
    mReq.lKey = mPostMrInfo.lKey;
    mReq.rKey = mPeerMrInfo.lKey;
    mReq.size = ctx->mSize;

    DoPostWrite();
    // 等待测试结束
    sem_wait(&mSem);
    return true;
}

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::TRANSPORT_WRITE_LAT, TransportWriteLatTest);
}
}
