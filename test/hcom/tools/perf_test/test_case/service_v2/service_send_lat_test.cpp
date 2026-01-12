/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "test_case/service_v2/service_send_lat_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_SEND_LAT = 200;

int ServiceSendLatTest::DoPostSend()
{
    PerfTestContext *ctx = GetPerfTestContext();
    int res = 0;
    // ctx->tposted[i+1] - ctx->tposted[i] 为一次RTT（Round-Trip Time，往返时间）
    if (ctx->cnt < ctx->mIterations) {
        ctx->tposted[ctx->cnt] = MONOTONIC_TIME_NS();
        UBSHcomRequest req(mDataAddr, ctx->mSize, OP_CODE_SEND_LAT);
        Callback *newCallback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
        if (newCallback == nullptr) {
            LOG_ERROR("Create callback failed");
            return -1;
        }
        res = mCh->Send(req, newCallback);
        if (res != 0) {
            if (newCallback != nullptr) {
                delete newCallback;
            }
            LOG_ERROR("Failed to send to server");
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

int ServiceSendLatTest::NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
    const std::string &payload)
{
    mCh = ch;
    LOG_DEBUG("New connection from " << ipPort << " !");
    return 0;
}

int ServiceSendLatTest::RequestReceived(const ock::hcom::UBSHcomServiceContext &ctx)
{
    int res = 0;
    if (mCfg.GetIsServer()) {
        // server 直接回复相同大小的消息即可
        UBSHcomRequest req(ctx.MessageData(), ctx.MessageDataLen(), OP_CODE_SEND_LAT);
        Callback *newCallback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
        if (newCallback == nullptr) {
            LOG_ERROR("Create callback failed");
            return -1;
        }
        res = mCh->Send(req, newCallback);
        if (res != 0) {
            if (newCallback != nullptr) {
                delete newCallback;
            }
            LOG_ERROR("UBSHcomResponse meaasge error");
            return -1;
        }
    } else {
        DoPostSend();
    }
    return 0;
}

bool ServiceSendLatTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create NetService
    std::function<int(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch, const std::string &payload)>
        funcNewChannel = bind(&ServiceSendLatTest::NewChannel, this, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3);
    std::function<int(const ock::hcom::UBSHcomServiceContext &ctx)> funcReqReceived =
        bind(&ServiceSendLatTest::RequestReceived, this, std::placeholders::_1);
    mHelper.RegisterNewChHandler(funcNewChannel);
    mHelper.RegisterRecvHandler(funcReqReceived);
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
            return false;
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

void ServiceSendLatTest::UnInitialize()
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

bool ServiceSendLatTest::Connect()
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

bool ServiceSendLatTest::RunTest(PerfTestContext *ctx)
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

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::SERVICE_SEND_LAT, ServiceSendLatTest);
}
}
