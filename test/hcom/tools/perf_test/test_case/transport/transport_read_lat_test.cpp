/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <functional>
#include "securec.h"

#include "common/perf_test_logger.h"
#include "test_case/perf_test_factory.h"
#include "transport_read_lat_test.h"

namespace hcom {
namespace perftest {
using namespace ock::hcom;
constexpr uint16_t OP_CODE_READ_LAT = 2;
int TransportReadLatTest::NewEndPoint(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
    const std::string &payload)
{
    mEp = ep;
    LOG_DEBUG("new connection from " << ipPort << " !");
    return 0;
}

int TransportReadLatTest::RequestReceived(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    int result = 0;
    if (!mCfg.GetIsServer()) {
        // client
        if (memcpy_s(&serverMrInfo, sizeof(serverMrInfo), ctx.Message()->Data(), ctx.Message()->DataLen()) != 0) {
            LOG_ERROR("memcpy_s failed");
            return -1;
        }
        if (mCfg.GetUbcMode() == ock::hcom::UBSHcomUbcMode::HighBandwidth &&
            mCfg.GetProtocol() == ock::hcom::UBSHcomNetDriverProtocol::UBC) {
            mHelper.GetNetDriver()->ImportUrmaSeg(serverMrInfo.lAddress, serverMrInfo.size, serverMrInfo.lKey, &mTseg,
                serverMrInfo.eid, sizeof(serverMrInfo.eid));
        }
        sem_post(&mSem);
        return result;
    }
    // server
    UBSHcomNetTransRequest rsp((void *)(&serverMrInfo), sizeof(serverMrInfo), 0);
    if ((result = mEp->PostSend(OP_CODE_READ_LAT, rsp)) != 0) {
        LOG_ERROR("Failed to post message to data to server, result " << result);
    }
    return result;
}

int TransportReadLatTest::OneSideDone(const ock::hcom::UBSHcomNetRequestContext &ctx)
{
    return DoPostRead();
}

bool TransportReadLatTest::Initialize()
{
    sem_init(&mSem, 0, 0);

    // create UBSHcomNetDriver
    NewEpHandler funcNewEndpoint = bind(&TransportReadLatTest::NewEndPoint, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3);
    ReqRecvHandler funcReqReceived = bind(&TransportReadLatTest::RequestReceived, this, std::placeholders::_1);
    OneSideDoneHandler funcOneSide = bind(&TransportReadLatTest::OneSideDone, this, std::placeholders::_1);
    mHelper.RegisterNewEPHandler(funcNewEndpoint);
    mHelper.RegisterReqRecvHandler(funcReqReceived);
    mHelper.RegisterOneSideDoneHandler(funcOneSide);
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

bool TransportReadLatTest::RegMemory()
{
    if (!mCfg.GetIsServer()) {
        // client
        if (!mHelper.CreateMemoryRegion(clientMrInfo)) {
            LOG_ERROR("client create memoryRegion failed");
            return false;
        }
    } else {
        // server
        if (!mHelper.CreateMemoryRegion(serverMrInfo)) {
            LOG_ERROR("server create memoryRegion failed");
            return false;
        }
    }
    return true;
}

bool TransportReadLatTest::ExchangeAddress()
{
    if (mEp == nullptr) {
        LOG_ERROR("Exchange address failed, ep is nullptr!");
        return false;
    }
    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if (mEp->PostSend(OP_CODE_READ_LAT, req) != 0) {
        LOG_ERROR("Failed to exchange address to data to server");
        return false;
    }
    sem_wait(&mSem);
    return true;
}

void TransportReadLatTest::UnInitialize()
{
    if (mEp != nullptr) {
        mEp->Close();
        mEp.Set(nullptr);
    }

    mHelper.DestroyNetDriver();
    sem_destroy(&mSem);
}

bool TransportReadLatTest::Connect()
{
    auto driver = mHelper.GetNetDriver();
    if (driver == nullptr) {
        LOG_ERROR("connect failed, net driver is nullptr!");
        return false;
    }
    int res = driver->Connect(mCfg.GetOobIp(), mCfg.GetOobPort(), "read_test", mEp, 0);
    if (res != 0) {
        LOG_ERROR("connect failed, error code: " << res);
        return false;
    }
    return true;
}

bool TransportReadLatTest::RunTest(PerfTestContext *ctx)
{
    // ctx会记录测试中每个Iteration耗时，故每次使用不同的ctx
    SetPerfTestContext(ctx);
    mReq.lAddress = clientMrInfo.lAddress;
    mReq.rAddress = serverMrInfo.lAddress;
    mReq.lKey = clientMrInfo.lKey;
    mReq.rKey = serverMrInfo.lKey;
    mReq.size = ctx->mSize;
    mReq.dstSeg = mTseg;
    if (!mCfg.GetIsServer()) {
        DoPostRead();
    }
    // 等待测试结束
    sem_wait(&mSem);
    return true;
}

REGIST_PERF_TEST_CREATOR(PERF_TEST_TYPE::TRANSPORT_READ_LAT, TransportReadLatTest);
}
}
