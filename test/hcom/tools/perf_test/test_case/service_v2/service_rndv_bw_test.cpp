/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "test_case/service_v2/service_rndv_bw_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_RNDV_BW = 301;
static uint32_t g_dataSize = 0;

int ServiceRndvBwTest::DoPostSend()
{
    PerfTestContext *ctx = GetPerfTestContext();
    UBSHcomRequest req((void *)mPostMrInfo.lAddress, g_dataSize, OP_CODE_RNDV_BW);
    UBSHcomResponse rsp((void *)mPostMrInfo.lAddress, NN_NO16);
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
        int res = mCh->Call(req, rsp, newCallback);
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

int ServiceRndvBwTest::NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
    const std::string &payload)
{
    mCh = ch;
    LOG_DEBUG("New connection from " << ipPort << " !");
    return 0;
}

int ServiceRndvBwTest::RequestReceived(const ock::hcom::UBSHcomServiceContext &ctx)
{
    int res = 0;
    if (mCfg.GetIsServer()) {
        uintptr_t contextRsp = ctx.RspCtx();
        const UBSHcomChannelPtr &rspChannel = ctx.Channel();
        Callback *newCallback = UBSHcomNewCallback(
            [contextRsp, rspChannel, ctx](UBSHcomServiceContext &context) {
                if (context.Result() != SER_OK) {
                    LOG_ERROR("Rndv recv callback failed " << context.Result());
                }

                UBSHcomRequest req(ctx.MessageData(), NN_NO16, OP_CODE_RNDV_BW);

                UBSHcomReplyContext replyCtx;
                replyCtx.errorCode = 0;
                replyCtx.rspCtx = contextRsp;
                Callback *cb = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
                if (cb==nullptr) {
                    LOG_ERROR("New callback is nullptr");
                    return;
                }
                if (rspChannel->Reply(replyCtx, req, cb) != 0) {
                    LOG_ERROR("Failed to post message to data to server");
                }
            },
            std::placeholders::_1);
        if (ctx.Channel()->Recv(ctx, mPostMrInfo.lAddress, ctx.MessageDataLen(), newCallback) != 0) {
            LOG_ERROR("Failed to recv rndv data from server");
        }
    }
    return 0;
}

int ServiceRndvBwTest::RequestPosted(const ock::hcom::UBSHcomServiceContext &ctx)
{
    PerfTestContext *testCtx = GetPerfTestContext();
    testCtx->totrcnt++;
    if (testCtx->totrcnt == testCtx->mIterations) {
        testCtx->tposted[testCtx->mIterations] = MONOTONIC_TIME_NS();
        sem_post(&mSem);
    }
    return 0;
}

bool ServiceRndvBwTest::RegMemory()
{
    if (!mHelper.CreateMemoryRegion(mPostMrInfo)) {
        LOG_ERROR("Create memoryRegion failed");
        return false;
    }
    return true;
}

bool ServiceRndvBwTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create NetService
    UBSHcomServiceNewChannelHandler funcNewChannel =
        bind(&ServiceRndvBwTest::NewChannel, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    UBSHcomServiceSendHandler funcReqPosted = bind(&ServiceRndvBwTest::RequestPosted, this, std::placeholders::_1);
    std::function<int(const ock::hcom::UBSHcomServiceContext &ctx)> funcReqReceived =
        bind(&ServiceRndvBwTest::RequestReceived, this, std::placeholders::_1);

    mHelper.RegisterNewChHandler(funcNewChannel);
    mHelper.RegisterSendHandler(funcReqPosted);
    mHelper.RegisterRecvHandler(funcReqReceived);

    if (!mHelper.CreateService()) {
        goto ERROR_HANDLE;
    }

    // register rndv data buffer
    if (!RegMemory()) {
        LOG_ERROR("Register memory faild");
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

    sem_destroy(&mSem);
    return false;
}

void ServiceRndvBwTest::UnInitialize()
{
    if (mCh != nullptr) {
        mHelper.GetNetService()->Disconnect(mCh);
        mCh.Set(nullptr);
    }

    mHelper.DestroyService();

    sem_destroy(&mSem);
}

bool ServiceRndvBwTest::Connect()
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

bool ServiceRndvBwTest::RunTest(PerfTestContext *ctx)
{
    g_dataSize = ctx->mSize;
    // ctx会记录测试中每个Iteration耗时，故每次使用不同的ctx
    SetPerfTestContext(ctx);
    if (!mCfg.GetIsServer()) {
        DoPostSend();
        // 等待测试结束
        sem_wait(&mSem);
    }

    return true;
}

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::SERVICE_RNDV_BW, ServiceRndvBwTest);
}
}
