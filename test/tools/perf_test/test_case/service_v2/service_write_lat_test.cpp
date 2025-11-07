/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "securec.h"

#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "test_case/service_v2/service_write_lat_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_SERVICE_WRITE_LAT = 205;

int ServiceWriteLatTest::NewChannel(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
    const std::string &payload)
{
    mCh = ch;
    isConnect.store(true);
    LOG_DEBUG("New connection from " << ipPort << " !");
    return 0;
}

int ServiceWriteLatTest::RequestReceived(const ock::hcom::UBSHcomServiceContext &ctx)
{
    int result = 0;
    if (mCfg.GetIsServer()) {
        // server
        if (memcpy_s(&mPeerMrInfo, sizeof(mPeerMrInfo), ctx.MessageData(), ctx.MessageDataLen()) != 0) {
            LOG_ERROR("memcpy_s failed");
            return -1;
        }
        UBSHcomRequest req(&mPollMrInfo, sizeof(mPollMrInfo), OP_SERVICE_WRITE_LAT);
        // NetServiceOpInfo sendOpInfo{};
        Callback *newCallback = UBSHcomNewCallback([](UBSHcomServiceContext &context) {}, std::placeholders::_1);
        if (newCallback == nullptr) {
            LOG_ERROR("Create callback failed");
            sem_post(&mSem);
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
            result = -1;
        }
        sem_post(&mSem);
    }
    return result;
}

void ServiceWriteLatTest::ChannelBroken(const ock::hcom::UBSHcomChannelPtr &ch)
{
    isConnect.store(false);
    return;
}

bool ServiceWriteLatTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create NetService
    UBSHcomServiceNewChannelHandler funcNewChannel = bind(&ServiceWriteLatTest::NewChannel, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);
    UBSHcomServiceRecvHandler funcReqReceived = bind(&ServiceWriteLatTest::RequestReceived, this, std::placeholders::_1);
    UBSHcomServiceChannelBrokenHandler funcChBroken = bind(&ServiceWriteLatTest::ChannelBroken, this,
        std::placeholders::_1);

    mHelper.RegisterRecvHandler(funcReqReceived);
    mHelper.RegisterNewChHandler(funcNewChannel);
    mHelper.RegisterChBrokenHandler(funcChBroken);
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

bool ServiceWriteLatTest::RegMemory()
{
    if (!mHelper.CreateMemoryRegion(mPostMrInfo)) {
        LOG_ERROR("Create memoryRegion failed");
        return false;
    }
    if (!mHelper.CreateMemoryRegion(mPollMrInfo)) {
        LOG_ERROR("Create memoryRegion failed");
        return false;
    }
    return true;
}

bool ServiceWriteLatTest::ExchangeAddress()
{
    if (mCh == nullptr) {
        LOG_ERROR("Exchange address failed, ch is nullptr!");
        return false;
    }

    UBSHcomRequest req(&mPollMrInfo, sizeof(mPollMrInfo), OP_SERVICE_WRITE_LAT);
    UBSHcomResponse rsp(&mPeerMrInfo, sizeof(mPeerMrInfo));

    if ((mCh->Call(req, rsp, nullptr)) != 0) {
        LOG_ERROR("Failed to call message to data to server");
        return false;
    }

    return true;
}

void ServiceWriteLatTest::UnInitialize()
{
    if (mCh != nullptr) {
        mHelper.GetNetService()->Disconnect(mCh);
        mCh.Set(nullptr);
    }

    mHelper.DestroyService();
    sem_destroy(&mSem);
}

bool ServiceWriteLatTest::Connect()
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

bool ServiceWriteLatTest::RunTest(PerfTestContext *ctx)
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

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::SERVICE_WRITE_LAT, ServiceWriteLatTest);
}
}
