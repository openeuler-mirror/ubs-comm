/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "test_case/service_v2/service_rndv_lat_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_RNDV_LAT = 300;
static uint32_t g_dataSize = 0;

int ServiceRndvLatTest::DoPostSend()
{
    PerfTestContext *ctx = GetPerfTestContext();
    int res = 0;
    // ctx->tposted[i+1] - ctx->tposted[i] 为一次RTT（Round-Trip Time，往返时间）
    while (ctx->cnt < ctx->mIterations) {
        ctx->tposted[ctx->cnt] = MONOTONIC_TIME_NS();
        UBSHcomRequest req((void *)mPostMrInfo.lAddress, g_dataSize, OP_CODE_RNDV_LAT);
        UBSHcomResponse rsp((void *)mPostMrInfo.lAddress, NN_NO16);
        res = mCh->Call(req, rsp, nullptr);
        if (res != 0) {
            LOG_ERROR("Failed to send to server");
        }
        ++ctx->cnt;
    }

    if (ctx->cnt == ctx->mIterations) {
        ctx->tposted[ctx->cnt] = MONOTONIC_TIME_NS();
        LOG_DEBUG("One Iteration Done!");
        sem_post(&mSem);
    }
    return 0;
}

int ServiceRndvLatTest::NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
    const std::string &payload)
{
    mCh = ch;
    LOG_DEBUG("New connection from " << ipPort << " !");
    return 0;
}

int ServiceRndvLatTest::RequestReceived(const ock::hcom::UBSHcomServiceContext &ctx)
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

                UBSHcomRequest req(ctx.MessageData(), NN_NO16, OP_CODE_RNDV_LAT);

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

bool ServiceRndvLatTest::RegMemory()
{
    if (!mHelper.CreateMemoryRegion(mPostMrInfo)) {
        LOG_ERROR("Create memoryRegion failed");
        return false;
    }
    return true;
}

bool ServiceRndvLatTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create NetService
    std::function<int(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch, const std::string &payload)>
        funcNewChannel = bind(&ServiceRndvLatTest::NewChannel, this, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3);
    std::function<int(const ock::hcom::UBSHcomServiceContext &ctx)> funcReqReceived =
        bind(&ServiceRndvLatTest::RequestReceived, this, std::placeholders::_1);
    mHelper.RegisterNewChHandler(funcNewChannel);
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
            return false;
        }
    }

    return true;

ERROR_HANDLE:
    mHelper.DestroyService();

    sem_destroy(&mSem);
    return false;
}

void ServiceRndvLatTest::UnInitialize()
{
    if (mCh != nullptr) {
        mHelper.GetNetService()->Disconnect(mCh);
        mCh.Set(nullptr);
    }

    mHelper.DestroyService();

    sem_destroy(&mSem);
}

bool ServiceRndvLatTest::Connect()
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

bool ServiceRndvLatTest::RunTest(PerfTestContext *ctx)
{
    // ctx会记录测试中每个Iteration耗时，故每次使用不同的ctx
    g_dataSize = ctx->mSize;

    SetPerfTestContext(ctx);
    if (!mCfg.GetIsServer()) {
        DoPostSend();
        // 等待测试结束
        sem_wait(&mSem);
    }

    return true;
}

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::SERVICE_RNDV_LAT, ServiceRndvLatTest);
}
}
