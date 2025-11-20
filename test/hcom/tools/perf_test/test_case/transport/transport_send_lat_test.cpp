/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>

#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "test_case/transport/transport_send_lat_test.h"


namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_SEND_LAT = 200;

int TransportSendLatTest::DoPostSend()
{
    PerfTestContext *ctx = GetPerfTestContext();
    // ctx->tposted[i+1] - ctx->tposted[i] 为一次RTT（Round-Trip Time，往返时间）
    if (ctx->cnt < ctx->mIterations) {
        ctx->tposted[ctx->cnt] = MONOTONIC_TIME_NS();
        UBSHcomNetTransRequest req(mDataAddr, ctx->mSize, 0);
        int res = mEp->PostSendRaw(req, 1);
        if (res != 0) {
            LOG_ERROR("failed to send to server");
        }
        ++ctx->cnt;
        return 0;
    }

    if (ctx->cnt == ctx->mIterations) {
        ctx->tposted[ctx->cnt] = MONOTONIC_TIME_NS();
        LOG_DEBUG("One Iteration Done!");
        sem_post(&mSem);
    }
    return 0;
}

int TransportSendLatTest::NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    mEp = ep;
    LOG_DEBUG("new connection from " << ipPort << " !");
    return 0;
}

int TransportSendLatTest::RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    if (mCfg.GetIsServer()) {
        // server 直接回复相同大小的消息即可
        UBSHcomNetTransRequest req(mDataAddr, ctx.Header().dataLength, 0);
        int res = mEp->PostSendRaw(req, 1);
    } else {
        DoPostSend();
    }
    return 0;
}

bool TransportSendLatTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create UBSHcomNetDriver
    NewEpHandler funcNewEndpoint = bind(&TransportSendLatTest::NewEndPoint, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);
    ReqRecvHandler funcReqReceived = bind(&TransportSendLatTest::RequestReceived, this, std::placeholders::_1);
    mHelper.RegisterNewEPHandler(funcNewEndpoint);
    mHelper.RegisterReqRecvHandler(funcReqReceived);
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

void TransportSendLatTest::UnInitialize()
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

bool TransportSendLatTest::Connect()
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

bool TransportSendLatTest::RunTest(PerfTestContext *ctx)
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

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::TRANSPORT_SEND_LAT, TransportSendLatTest);
}
}
