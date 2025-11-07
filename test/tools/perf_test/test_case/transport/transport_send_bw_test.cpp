/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include <unistd.h>
#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "transport_send_bw_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_SEND_BW = 200;

int TransportSendBwTest::DoPostSend()
{
    PerfTestContext *ctx = GetPerfTestContext();
    UBSHcomNetTransRequest req(mDataAddr, ctx->mSize, 0);
    ctx->tposted[0] = MONOTONIC_TIME_NS();
    for (uint64_t i = 0; i < ctx->mIterations; ++i) {
        int res = mEp->PostSend(OP_CODE_SEND_BW, req);
        if (res != 0) {
            LOG_ERROR("failed to send to server");
        }
    }
    return 0;
}

int TransportSendBwTest::NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    mEp = ep;
    LOG_DEBUG("new connection from " << ipPort << " !");
    return 0;
}

int TransportSendBwTest::RequestPosted(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    PerfTestContext *testCtx = GetPerfTestContext();
    testCtx->totrcnt++;
    if (testCtx->totrcnt == testCtx->mIterations) {
        testCtx->tposted[testCtx->mIterations] = MONOTONIC_TIME_NS();
        sem_post(&mSem);
    }
    return 0;
}

bool TransportSendBwTest::Initialize()
{
    sem_init(&mSem, 0, 0);
    // create UBSHcomNetDriver
    NewEpHandler funcNewEndpoint = bind(&TransportSendBwTest::NewEndPoint, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);
    ReqPostedHandler funcReqPosted = bind(&TransportSendBwTest::RequestPosted, this, std::placeholders::_1);
    mHelper.RegisterNewEPHandler(funcNewEndpoint);
    mHelper.RegisterReqPostedHandler(funcReqPosted);
    if (!mHelper.CreateNetDriver()) {
        goto ERROR_HANDLE;
    }

    // init data buffer
    mDataAddr = new char[MAX_MESSAGE_SIZE];
    if (mDataAddr == nullptr) {
        LOG_ERROR("create data buffer failed");
        goto ERROR_HANDLE;
    }

    // client connect to server
    if (!mCfg.GetIsServer()) {
        if (!Connect()) {
            LOG_ERROR("client connect failed");
            goto ERROR_HANDLE;
        }
    }

    return true;

ERROR_HANDLE:
    mHelper.DestroyNetDriver();
    if (mDataAddr != nullptr) {
        delete[] mDataAddr;
        mDataAddr = nullptr;
    }
    sem_destroy(&mSem);
    return false;
}

void TransportSendBwTest::UnInitialize()
{
    if (mEp != nullptr) {
        mEp->Close();
        mEp.Set(nullptr);
    }

    mHelper.DestroyNetDriver();

    if (mDataAddr != nullptr) {
        delete[] mDataAddr;
        mDataAddr = nullptr;
    }
    sem_destroy(&mSem);
}

bool TransportSendBwTest::Connect()
{
    auto driver = mHelper.GetNetDriver();
    if (driver == nullptr) {
        LOG_ERROR("connect failed, net driver is nullptr!");
        return false;
    }
    int res = driver->Connect(mCfg.GetOobIp(), mCfg.GetOobPort(), "xx", mEp, 0);
    if (res != 0) {
        LOG_ERROR("connect failed, error code: " << res);
        return false;
    }
    return true;
}

bool TransportSendBwTest::RunTest(PerfTestContext *ctx)
{
    // ctx会记录测试中每个Iteration耗时，故每次使用不同的ctx
    SetPerfTestContext(ctx);
    if (!mCfg.GetIsServer()) {
        DoPostSend();
    }
    // 等待测试结束
    sem_wait(&mSem);
    return true;
}

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::TRANSPORT_SEND_BW, TransportSendBwTest);
}
}
