/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <dirent.h>
#include <iostream>
#include "net_shm_sync_endpoint.h"
#include "net_shm_async_endpoint.h"
#include "shm_composed_endpoint.h"
#include "net_oob_secure.h"
#include "net_oob_ssl.h"
#include "shm_validation.h"
#include "net_shm_driver_oob.h"

namespace ock {
namespace hcom {
class TestNetShmDriverOob : public testing::Test {
public:
    TestNetShmDriverOob();
    virtual void SetUp(void);
    virtual void TearDown(void);

    NetDriverShmWithOOB *driver = nullptr;
};

TestNetShmDriverOob::TestNetShmDriverOob() {}

void TestNetShmDriverOob::SetUp()
{
    driver = new (std::nothrow) NetDriverShmWithOOB("ShmDriverClearShmLeftFile", false, SHM);
    ASSERT_NE(driver, nullptr);
}

void TestNetShmDriverOob::TearDown()
{
    if (driver != nullptr) {
        delete driver;
        driver = nullptr;
    }
    GlobalMockObject::verify();
}

TEST_F(TestNetShmDriverOob, ShmDriverClearShmLeftFile)
{
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("ShmDriverClearShmLeftFile", 0, NN_NO128, NN_NO4, ch);
    ch->mUpCtx = 0;

    driver->ProcessEpError(ch);
    driver->ClearShmLeftFile();
    EXPECT_EQ(driver->mClearThreadStarted, true);
}

TEST_F(TestNetShmDriverOob, ShmDriverCreateMemoryRegion)
{
    int ret;
    ASSERT_NE(driver, nullptr);
    UBSHcomNetMemoryRegionPtr mr{};

    ret = driver->CreateMemoryRegion(0, 0, mr);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_OPERATION));
}

TEST_F(TestNetShmDriverOob, DriverInitializeInited)
{
    int ret;
    UBSHcomNetDriverOptions option{};
    driver->mInited = true;

    ret = driver->Initialize(option);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetShmDriverOob, DriverInitializeValidateCommonOptionsFail)
{
    int ret;
    UBSHcomNetDriverOptions option{};
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(1));

    ret = driver->Initialize(option);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, DriverInitializeOutLoggerInstanceFail)
{
    int ret;
    UBSHcomNetDriverOptions option{};
    UBSHcomNetOutLogger *logger = nullptr;
    driver->mInited = false;
    MOCKER_CPP(&NetDriverShmWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&UBSHcomNetOutLogger::Instance).stubs().will(returnValue(logger));

    ret = driver->Initialize(option);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, DriverInitializeLoadSslFail)
{
    int ret;
    UBSHcomNetDriverOptions option{};

    driver->mInited = false;
    option.enableTls = true;
    MOCKER_CPP(&NetDriverShmWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(HcomSsl::Load).stubs().will(returnValue(1));

    ret = driver->Initialize(option);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, DriverInitializeFail)
{
    int ret;
    UBSHcomNetDriverOptions option{};

    driver->mInited = false;
    option.enableTls = false;
    MOCKER_CPP(&NetDriverShmWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverShmWithOOB::CreateWorkerResource).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverShmWithOOB::CreateWorkers).stubs().will(returnValue(1));

    ret = driver->Initialize(option);
    EXPECT_NE(ret, 0);

    ret = driver->Initialize(option);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, DriverInitializeFail2)
{
    int ret;
    UBSHcomNetDriverOptions option{};

    driver->mInited = false;
    option.enableTls = false;
    MOCKER_CPP(&NetDriverShmWithOOB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverShmWithOOB::CreateWorkerResource).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverShmWithOOB::CreateWorkers).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverShmWithOOB::CreateClientLB).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverShmWithOOB::CreateListeners).stubs().will(returnValue(1));

    ret = driver->Initialize(option);
    EXPECT_NE(ret, 0);

    driver->mStartOobSvr = true;
    ret = driver->Initialize(option);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, ValidateOptionsFail)
{
    int ret;
    MOCKER_CPP(&UBSHcomNetDriver::ValidateAndParseOobPortRange)
        .stubs()
        .will(returnValue(static_cast<int>(NN_INVALID_PARAM)))
        .then(returnValue(0));
    MOCKER_CPP(&UBSHcomNetDriver::ValidateOptionsOobType).stubs().will(returnValue(static_cast<int>(NN_INVALID_PARAM)));
    ret = driver->ValidateOptions();
    EXPECT_EQ(ret, NN_INVALID_PARAM);
    ret = driver->ValidateOptions();
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetShmDriverOob, CreateWorkersFail)
{
    int ret;
    UBSHcomNetDriverOptions option{};

    MOCKER_CPP(NetFunc::NN_ParseWorkersGroups).stubs().will(returnValue(false));
    ret = driver->CreateWorkers();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, UnInitializeNotInit)
{
    driver->mInited = false;
    EXPECT_NO_FATAL_FAILURE(driver->UnInitialize());
}

TEST_F(TestNetShmDriverOob, UnInitializeStarted)
{
    driver->mInited = true;
    driver->mStarted = true;
    EXPECT_NO_FATAL_FAILURE(driver->UnInitialize());
}

TEST_F(TestNetShmDriverOob, CreateWorkerResourceOpCompMemPoolFail)
{
    int ret;
    NetMemPoolFixed *testOpCompMemPool = nullptr;
    MOCKER_CPP(&NetRef<NetMemPoolFixed>::Get).stubs().will(returnValue(testOpCompMemPool));
    ret = driver->CreateWorkerResource();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, CreateWorkerResourceMemPoolInitializeFail)
{
    int ret;
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(0)).then(returnValue(1));
    ret = driver->CreateWorkerResource();
    EXPECT_NE(ret, 0);

    ret = driver->CreateWorkerResource();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, CreateWorkerResourceSglCompMemPoolInitializeFail)
{
    int ret;
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(0)).then(returnValue(0)).then(returnValue(1));
    ret = driver->CreateWorkerResource();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, StartNotInited)
{
    int ret;
    driver->mInited = false;
    ret = driver->Start();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, StartChannelKeeperNull)
{
    int ret;
    driver->mInited = true;
    driver->mChannelKeeper = nullptr;
    ret = driver->Start();
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, CreateMemoryRegionNotInited)
{
    int ret;
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    driver->mInited = false;
    ret = driver->CreateMemoryRegion(1, mr);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, CreateMemoryRegionShmMemoryRegionCreateFail)
{
    int ret;
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    driver->mInited = true;
    MOCKER_CPP(ShmMemoryRegion::Create, NResult(const std::string &, uint64_t, ShmMemoryRegion *&))
        .stubs()
        .will(returnValue(1));
    ret = driver->CreateMemoryRegion(1, mr);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, CreateMemoryRegionMemId)
{
    int ret;
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    ret = driver->CreateMemoryRegion(1, mr, 1);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, MultiRailNewConnectionErr)
{
    int ret;
    OOBTCPConnection conn(-1);
    ret = driver->MultiRailNewConnection(conn);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, DestroyEndpointNull)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    EXPECT_NO_FATAL_FAILURE(driver->DestroyEndpoint(ep));
}

TEST_F(TestNetShmDriverOob, DestroyMemoryRegionNull)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    EXPECT_NO_FATAL_FAILURE(driver->DestroyMemoryRegion(mr));
}

TEST_F(TestNetShmDriverOob, MapAndRegVaForUBErr)
{
    uint64_t va = 0;
    void *ret = driver->MapAndRegVaForUB(1, va);
    EXPECT_EQ(ret, nullptr);
}

TEST_F(TestNetShmDriverOob, UnmapVaForUBErr)
{
    int ret;
    uint64_t va = 0;
    ret = driver->UnmapVaForUB(va);
    EXPECT_NE(ret, 0);
}

TEST_F(TestNetShmDriverOob, HandleNewRequestChannelStateFail)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_BROKEN);
    ch->UpContext(1);

    ShmOpContextInfo ctx{ ch.Get(), 1, 1, ShmOpContextInfo::ShmOpType::SH_RECEIVE,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetShmDriverOob, HandleNewRequestValidateHeaderWithDataSizeFail)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uintptr_t>(ep.Get()));

    ShmOpContextInfo ctx{ ch.Get(), 1, 1, ShmOpContextInfo::ShmOpType::SH_RECEIVE,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(100));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, NN_ERROR);
}

TEST_F(TestNetShmDriverOob, HandleNewRequestEpToChildFail)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, nullptr, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    char testData[128] = "Hello, this is a test data.";
    ShmOpContextInfo ctx{ ch.Get(), (uintptr_t)testData, 1, ShmOpContextInfo::ShmOpType::SH_RECEIVE,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, NN_PARAM_INVALID);

    ret = driver->HandleNewRequest(ctx, 1);
    EXPECT_EQ(ret, NN_PARAM_INVALID);
}

TEST_F(TestNetShmDriverOob, HandleNewRequestMallocDecryptFail)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mIsNeedEncrypt = true;
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    char testData[128] = "Hello, this is a test data.";
    ShmOpContextInfo ctx{ ch.Get(), (uintptr_t)testData, 1, ShmOpContextInfo::ShmOpType::SH_RECEIVE,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true))
        .then(returnValue(false))
        .then(returnValue(true));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, NN_MALLOC_FAILED);

    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(false));
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);

    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed).stubs().will(returnValue(false)).then(returnValue(true));
    ret = driver->HandleNewRequest(ctx, 1);
    EXPECT_EQ(ret, NN_MALLOC_FAILED);

    ret = driver->HandleNewRequest(ctx, 1);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);
}

TEST_F(TestNetShmDriverOob, HandleNewRequestValidateDecryptLengthFail)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mIsNeedEncrypt = true;
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    char testData[128] = "Hello, this is a test data.";
    ShmOpContextInfo ctx{ ch.Get(), (uintptr_t)testData, 1, ShmOpContextInfo::ShmOpType::SH_RECEIVE,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed).stubs().will(returnValue(true));
    MOCKER_CPP(&AesGcm128::Decrypt).stubs().will(returnValue(true));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, NN_DECRYPT_FAILED);
}

TEST_F(TestNetShmDriverOob, HandleNewRequestMallocMemcpyFail)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mIsNeedEncrypt = false;
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    char testData[128] = "Hello, this is a test data.";
    ShmOpContextInfo ctx{ ch.Get(), (uintptr_t)testData, 1, ShmOpContextInfo::ShmOpType::SH_RECEIVE,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    MOCKER_CPP(NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ret = driver->HandleNewRequest(ctx, 1);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed).stubs().will(returnValue(false));
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, NN_MALLOC_FAILED);

    ret = driver->HandleNewRequest(ctx, 1);
    EXPECT_EQ(ret, NN_MALLOC_FAILED);
}

TEST_F(TestNetShmDriverOob, HandleNewRequest)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);
    ch->UpContext(1);

    ShmOpContextInfo ctx{ ch.Get(), 1, 1, ShmOpContextInfo::ShmOpType::SH_SEND,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    ret = driver->HandleNewRequest(ctx, 0);
    EXPECT_EQ(ret, 0);
}

TEST_F(TestNetShmDriverOob, HandleReqPostedFail)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *worker =
        new (std::nothrow) ShmWorker("shm", indexWorker, options, opMemPool, opCtxMemPool, sglOpMemPool);
    ch->UpContext1(reinterpret_cast<uintptr_t>(worker));

    ShmOpCompInfo ctx{};
    ctx.channel = ch.Get();
    ctx.opType = ShmOpContextInfo::ShmOpType::SH_RECEIVE;
    driver->mRequestPostedHandler = [](const UBSHcomNetRequestContext &ctx) -> int { return SER_ERROR; };
    ret = driver->HandleReqPosted(ctx);
    EXPECT_EQ(ret, SER_ERROR);
}

TEST_F(TestNetShmDriverOob, ProcessEpError)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    EXPECT_NO_FATAL_FAILURE(driver->ProcessEpError(ch));
}

TEST_F(TestNetShmDriverOob, ProcessEpErrorEPBroken)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mEPBrokenProcessed = true;
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    EXPECT_NO_FATAL_FAILURE(driver->ProcessEpError(ch));
}

TEST_F(TestNetShmDriverOob, ProcessEpErrorEPState)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    EXPECT_NO_FATAL_FAILURE(driver->ProcessEpError(ch));
}

TEST_F(TestNetShmDriverOob, ProcessEpErrorTwoSideRemaining)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    ShmOpCompInfo ctx{};
    ctx.channel = ch.Get();
    ctx.opType = ShmOpContextInfo::ShmOpType::SH_RECEIVE;
    ch.Get()->mCompPosted.next = &ctx;

    EXPECT_NO_FATAL_FAILURE(driver->ProcessEpError(ch));
}

TEST_F(TestNetShmDriverOob, ProcessEpErrorOneSideRemaining)
{
    int ret;
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetShmDriverOob", 0, NN_NO128, NN_NO4, ch);
    ch->mState.Set(ShmChannelState::CH_NEW);

    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), nullptr, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uintptr_t>(ep));

    ShmOpContextInfo ctx{ ch.Get(), 1, 1, ShmOpContextInfo::ShmOpType::SH_RECEIVE,
        ShmOpContextInfo::ShmErrorType::SH_NO_ERROR };
    ch.Get()->mCtxPosted.next = &ctx;

    EXPECT_NO_FATAL_FAILURE(driver->ProcessEpError(ch));
}

TEST_F(TestNetShmDriverOob, Connect)
{
    int ret;
    std::string payload = "Hello, this is a test data.";
    UBSHcomNetEndpointPtr ep;
    ret = driver->Connect(payload, ep, 0, 0, 0);
    EXPECT_NE(ret, NN_OK);

    driver->mOptions.oobType = NET_OOB_UDS;
    ret = driver->Connect(payload, ep, 0, 0, 0);
    EXPECT_NE(ret, NN_OK);
}

TEST_F(TestNetShmDriverOob, Connect2)
{
    int ret;
    std::string payload{};
    UBSHcomNetEndpointPtr ep;
    std::string serverUrl = "tcp://127.0.0.1:9981";
    std::string serverUrl2 = "uds://name";
    std::string badUrl = "unknown://127.0.0.1:9981";
    driver->mInited = true;
    driver->mStarted = false;
    ret = driver->Connect(serverUrl, payload, ep, 0, 0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);

    ret = driver->Connect(serverUrl2, payload, ep, 0, 0, 0, 0);
    EXPECT_EQ(ret, NN_ERROR);

    ret = driver->Connect(badUrl, payload, ep, 0, 0, 0, 0);
    EXPECT_EQ(ret, NN_INVALID_PARAM);
}

TEST_F(TestNetShmDriverOob, ShmDriverHandleNewRequestFail)
{
    int ret;
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("ShmDriverCreateMemoryRegion", false, SHM);
    ASSERT_NE(driver, nullptr);
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    // mWorker create
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *mWorker = new (std::nothrow)
        ShmWorker("NetAsyncEndpointShmPostSend", indexWorker, options, opMemPool, opCtxMemPool, sglOpMemPool);
    ASSERT_NE(mWorker, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), mWorker, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uint64_t>(ep));
    ShmOpContextInfo ctx{};
    ctx.channel = ch.Get();
    ctx.dataAddress = 1;
    ctx.dataSize = NN_NO1024;

    uint32_t immData = 0;
    MOCKER_CPP(&ShmChannel::UpContext, uint64_t(ShmChannel::*)() const)
        .stubs()
        .will(returnValue(static_cast<uint64_t>(1)))
        .then(returnValue(static_cast<uint64_t>(0)));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    ret = driver->HandleNewRequest(ctx, immData);
    EXPECT_EQ(ret, static_cast<int>(NN_PARAM_INVALID));

    delete driver;
    driver = nullptr;
}

TEST_F(TestNetShmDriverOob, ShmDriverHandleNewRequestFailTwo)
{
    int ret;
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("ShmDriverCreateMemoryRegion", false, SHM);
    ASSERT_NE(driver, nullptr);
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    // mWorker create
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *mWorker = new (std::nothrow)
        ShmWorker("NetAsyncEndpointShmPostSend", indexWorker, options, opMemPool, opCtxMemPool, sglOpMemPool);
    ASSERT_NE(mWorker, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), mWorker, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ch->UpContext(reinterpret_cast<uint64_t>(ep));
    ShmOpContextInfo ctx{};
    ctx.channel = ch.Get();
    UBSHcomNetTransHeader header{};
    ctx.dataAddress = reinterpret_cast<uintptr_t>(&header);
    ctx.dataSize = NN_NO1024;

    uint32_t immData = 0;
    MOCKER_CPP(&NetFunc::ValidateHeaderWithDataSize).stubs().will(returnValue(static_cast<int>(NN_PARAM_INVALID)));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    ret = driver->HandleNewRequest(ctx, immData);
    EXPECT_EQ(ret, static_cast<int>(NN_PARAM_INVALID));

    delete driver;
    driver = nullptr;
}

TEST_F(TestNetShmDriverOob, HandleChanelKeeperMsgFail)
{
    int ret;
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("ShmDriverCreateMemoryRegion", false, SHM);
    ASSERT_NE(driver, nullptr);
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    ShmChKeeperMsgHeader header{};
    header.msgType = GET_MR_FD;
    header.dataSize = sizeof(uint32_t);
    MOCKER(::recv).defaults().will(returnValue(1));
    ShmHandlePtr shmHandlePtr = nullptr;
    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap).stubs().will(returnValue(shmHandlePtr));
    driver->HandleChanelKeeperMsg(header, ch);
    EXPECT_EQ(header.msgType, GET_MR_FD);
    delete driver;
    driver = nullptr;
}

TEST_F(TestNetShmDriverOob, HandleChanelKeeperMsgFailTwo)
{
    int ret;
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("ShmDriverCreateMemoryRegion", false, SHM);
    ASSERT_NE(driver, nullptr);
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    ShmChKeeperMsgHeader header{};
    header.msgType = GET_MR_FD;
    header.dataSize = sizeof(uint32_t);
    MOCKER(::recv).defaults().will(returnValue(1));
    ShmHandlePtr shmHandlePtr = new (std::nothrow) ShmHandle("mName", SHM_F_EVENT_QUEUE_PREFIX, 1, NN_NO128, true);
    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap).stubs().will(returnValue(shmHandlePtr));
    MOCKER_CPP(&ShmHandle::Fd).stubs().will(returnValue(0));
    driver->HandleChanelKeeperMsg(header, ch);
    EXPECT_EQ(header.msgType, GET_MR_FD);
    delete driver;
    driver = nullptr;
}

TEST_F(TestNetShmDriverOob, HandleChanelKeeperMsgFailThree)
{
    int ret;
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("ShmDriverCreateMemoryRegion", false, SHM);
    ASSERT_NE(driver, nullptr);
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    ShmChKeeperMsgHeader header{};
    header.msgType = GET_MR_FD;
    driver->HandleChanelKeeperMsg(header, ch);

    header.dataSize = sizeof(uint32_t);
    MOCKER(::recv).defaults().will(returnValue(0));
    driver->HandleChanelKeeperMsg(header, ch);
    EXPECT_EQ(header.msgType, GET_MR_FD);
    delete driver;
    driver = nullptr;
}

TEST_F(TestNetShmDriverOob, HandleChanelKeeperMsgFailFour)
{
    int ret;
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("ShmDriverCreateMemoryRegion", false, SHM);
    ASSERT_NE(driver, nullptr);
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    ShmChKeeperMsgHeader header{};
    header.msgType = GET_MR_FD;
    header.dataSize = sizeof(uint32_t);
    MOCKER(::recv).defaults().will(returnValue(1));
    ShmHandlePtr shmHandlePtr = new (std::nothrow) ShmHandle("mName", SHM_F_EVENT_QUEUE_PREFIX, 1, NN_NO128, true);
    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap).stubs().will(returnValue(shmHandlePtr));
    MOCKER_CPP(&ShmHandle::Fd).stubs().will(returnValue(1));
    MOCKER(::send).defaults().will(returnValue(0)).then(returnValue(1));
    driver->HandleChanelKeeperMsg(header, ch);
    EXPECT_EQ(header.msgType, GET_MR_FD);

    MOCKER_CPP(&ShmHandleFds::SendMsgFds).defaults().will(returnValue(1));
    driver->HandleChanelKeeperMsg(header, ch);
    EXPECT_EQ(header.msgType, GET_MR_FD);
    delete driver;
    driver = nullptr;
}
} // namespace hcom
} // namespace ock