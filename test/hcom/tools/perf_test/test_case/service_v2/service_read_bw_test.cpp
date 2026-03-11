/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "test_case/service_v2/service_read_bw_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_SERVICE_READ_BW = 204;

int ServiceReadBwTest::DoPostRead()
{
    int res = 0;
    mCtx->tposted[0] = ock::hcom::MONOTONIC_TIME_NS();
    for (uint64_t i = 0; i < mCtx->mIterations; ++i) {
        ock::hcom::Callback *newCallback = ock::hcom::UBSHcomNewCallback(
            [this](ock::hcom::UBSHcomServiceContext &context) {
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
        res = mCh->Get(mReq, newCallback);
        if (res != 0) {
            if (newCallback != nullptr) {
                delete newCallback;
            }
            LOG_ERROR("failed to send to server");
            return res;
        }
    }
    return res;
}

int ServiceReadBwTest::NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
    const std::string &payload)
{
    mCh = ch;
    LOG_DEBUG("New connection from " << ipPort << " !");
    return 0;
}

int ServiceReadBwTest::RequestReceived(const ock::hcom::UBSHcomServiceContext &ctx)
{
    int result = 0;
    if (mCfg.GetIsServer()) {
        // server
        UBSHcomRequest req(&mPostMrInfo, sizeof(mPostMrInfo), OP_SERVICE_READ_BW);
        Callback *newCallback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
        if (newCallback == nullptr) {
            LOG_ERROR("Create callback failed");
            return -1;
        }
        // post send callback
        UBSHcomReplyContext replyCtx;
        replyCtx.rspCtx = ctx.RspCtx();
        if ((ctx.Channel()->Reply(replyCtx, req, newCallback)) != 0) {
            if (newCallback != nullptr) {
                delete newCallback;
            }
            LOG_ERROR("Failed to post message to data to server");
            return -1;
        }
    }
    return result;
}

bool ServiceReadBwTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create NetService
    UBSHcomServiceNewChannelHandler funcNewChannel =
        bind(&ServiceReadBwTest::NewChannel, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    UBSHcomServiceRecvHandler funcReqReceived = bind(&ServiceReadBwTest::RequestReceived, this, std::placeholders::_1);

    mHelper.RegisterRecvHandler(funcReqReceived);
    mHelper.RegisterNewChHandler(funcNewChannel);
    if (!mHelper.CreateService()) {
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
    mHelper.DestroyService();
    sem_destroy(&mSem);
    return false;
}

bool ServiceReadBwTest::RegMemory()
{
    if (!mHelper.CreateMemoryRegion(mPostMrInfo)) {
        LOG_ERROR("Create memoryRegion failed");
        return false;
    }
    return true;
}

bool ServiceReadBwTest::ExchangeAddress()
{
    if (mCh == nullptr) {
        LOG_ERROR("Exchange address failed, ch is nullptr!");
        return false;
    }

    std::string value = "hello world";
    UBSHcomRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), OP_SERVICE_READ_BW);
    UBSHcomResponse rsp(&mPeerMrInfo, sizeof(mPeerMrInfo));

    if ((mCh->Call(req, rsp, nullptr)) != 0) {
        LOG_ERROR("Failed to call message to data to server");
        return false;
    }

    return true;
}

void ServiceReadBwTest::UnInitialize()
{
    if (mCh != nullptr) {
        mHelper.GetNetService()->Disconnect(mCh);
        mCh.Set(nullptr);
    }

    mHelper.DestroyService();
    sem_destroy(&mSem);
}

bool ServiceReadBwTest::Connect()
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

bool ServiceReadBwTest::RunTest(PerfTestContext *ctx)
{
    // ctx会记录测试中每个Iteration耗时，故每次使用不同的ctx
    SetPerfTestContext(ctx);
    if (!mCfg.GetIsServer()) {
        mReq.lAddress = mPostMrInfo.lAddress;
        mReq.rAddress = mPeerMrInfo.lAddress;
        mReq.lKey = mPostMrInfo.lKey;
        mReq.rKey = mPeerMrInfo.lKey;
        mReq.size = ctx->mSize;

        DoPostRead();
    }
    // 等待测试结束
    sem_wait(&mSem);
    return true;
}

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::SERVICE_READ_BW, ServiceReadBwTest);
}
}
