/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "test_case/service_v2/service_send_bw_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_SEND_BW = 201;

int ServiceSendBwTest::DoPostSend()
{
    PerfTestContext *ctx = GetPerfTestContext();
    UBSHcomRequest req(mDataAddr, ctx->mSize, OP_CODE_SEND_BW);
    ctx->tposted[0] = MONOTONIC_TIME_NS();
    for (uint64_t i = 0; i < ctx->mIterations; ++i) {
        Callback *newCallback = UBSHcomNewCallback(
            [this](UBSHcomServiceContext &context) {
                PerfTestContext *testCtx = this->GetPerfTestContext();
                testCtx->totrcnt++;
                if (testCtx->totrcnt == testCtx->mIterations) {
                    testCtx->tposted[testCtx->mIterations] = MONOTONIC_TIME_NS();
                    sem_post(&this->mSem);
                }
            },
            std::placeholders::_1);
        if (newCallback == nullptr) {
            LOG_ERROR("Create callback failed");
            return -1;
        }
        int res = mCh->Send(req, newCallback);
        if (res != 0) {
            if (newCallback != nullptr) {
                delete newCallback;
            }
            LOG_ERROR("failed to send to server");
            return res;
        }
    }
    return 0;
}

int ServiceSendBwTest::NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
    const std::string &payload)
{
    mCh = ch;
    LOG_DEBUG("New connection from " << ipPort << " !");
    return 0;
}

int ServiceSendBwTest::RequestPosted(const ock::hcom::UBSHcomServiceContext &ctx)
{
    PerfTestContext *testCtx = GetPerfTestContext();
    testCtx->totrcnt++;
    if (testCtx->totrcnt == testCtx->mIterations) {
        testCtx->tposted[testCtx->mIterations] = MONOTONIC_TIME_NS();
        sem_post(&mSem);
    }
    return 0;
}

bool ServiceSendBwTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create NetService
    UBSHcomServiceNewChannelHandler funcNewChannel =
        bind(&ServiceSendBwTest::NewChannel, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    UBSHcomServiceSendHandler funcReqPosted = bind(&ServiceSendBwTest::RequestPosted, this, std::placeholders::_1);

    mHelper.RegisterNewChHandler(funcNewChannel);
    mHelper.RegisterSendHandler(funcReqPosted);

    if (!mHelper.CreateService()) {
        goto ERROR_HANDLE;
    }

    // init data buffer
    mDataAddr = new (std::nothrow) char[MAX_MESSAGE_SIZE];
    if (mDataAddr == nullptr) {
        LOG_ERROR("Create data buffer failed");
        goto ERROR_HANDLE;
    }

    // client connect to server
    if (!mCfg.GetIsServer()) {
        if (!Connect()) {
            LOG_ERROR("Client connect failed");
            goto ERROR_HANDLE;
        }
    }

    return true;

ERROR_HANDLE:
    mHelper.DestroyService();
    if (mDataAddr != nullptr) {
        delete[] mDataAddr;
        mDataAddr = nullptr;
    }
    sem_destroy(&mSem);
    return false;
}

void ServiceSendBwTest::UnInitialize()
{
    if (mCh != nullptr) {
        mHelper.GetNetService()->Disconnect(mCh);
        mCh.Set(nullptr);
    }

    mHelper.DestroyService();

    if (mDataAddr != nullptr) {
        delete[] mDataAddr;
        mDataAddr = nullptr;
    }
    sem_destroy(&mSem);
}

bool ServiceSendBwTest::Connect()
{
    auto service = mHelper.GetNetService();
    if (service == nullptr) {
        LOG_ERROR("Connect failed, net service is nullptr!");
        return false;
    }
    UBSHcomConnectOptions opt;
    int res = service->Connect("tcp://" + mCfg.GetOobIp() + ":" + std::to_string(mCfg.GetOobPort()), mCh, opt);
    if (res != 0) {
        LOG_ERROR("Connect failed, error code: " << res);
        return false;
    }
    return true;
}

bool ServiceSendBwTest::RunTest(PerfTestContext *ctx)
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

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::SERVICE_SEND_BW, ServiceSendBwTest);
}
}
