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

#ifdef UB_BUILD_ENABLED
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom_def.h"
#include "net_ub_driver.h"
#include "net_ub_endpoint.h"
#include "openssl_api_wrapper.h"
#include "under_api/urma/urma_api_wrapper.h"
#include "ub_common.h"
#include "ub_mr_fixed_buf.h"
#include "ub_worker.h"
#include "net_oob_secure.h"

namespace ock {
namespace hcom {
class TestNetDriverUB : public testing::Test {
#define OBMM_SIZE 1 << 27 // 128M
public:
    TestNetDriverUB();
    virtual void SetUp(void);
    virtual void TearDown(void);
    std::string mName = "TestNetDriverUB";
    NetDriverUBWithOob *driver = nullptr;
    UBSHcomNetDriverOptions option{};
    UBContext *ctx = nullptr;
    UBEId eid{};
    urma_context_t mUrmaContext{};
    char mem[NN_NO8]{};
    // worker
    UBWorker *worker = nullptr;
    NetMemPoolFixed *memPool = nullptr;
    NetMemPoolFixed *sglMemPool = nullptr;
    UBWorkerOptions workerOptions{};
    // qp
    UBJetty *qp = nullptr;
    // mDriverSendMR
    UBMemoryRegionFixedBuffer *mDriverSendMR = nullptr;
};

TestNetDriverUB::TestNetDriverUB() {}

void TestNetDriverUB::SetUp()
{
    driver = new (std::nothrow) NetDriverUBWithOob(mName, true, UBSHcomNetDriverProtocol::UBC);
    ASSERT_NE(driver, nullptr);
    ctx = new (std::nothrow) UBContext("ubTest");
    ASSERT_NE(ctx, nullptr);
    ctx->mUrmaContext = &mUrmaContext;
    ctx->protocol = UBSHcomNetDriverProtocol::UBC;
    driver->mContext = ctx;
    worker = new (std::nothrow) UBWorker(mName, ctx, workerOptions, memPool, sglMemPool);
    ASSERT_NE(worker, nullptr);
    qp = new (std::nothrow) UBJetty(mName, 0, nullptr, nullptr);
    ASSERT_NE(qp, nullptr);
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());
}

void TestNetDriverUB::TearDown()
{
    if (ctx != nullptr) {
        ctx->mUrmaContext = nullptr;
        delete ctx;
        ctx = nullptr;
    }
    if (driver != nullptr) {
        driver->mContext = nullptr;
        delete driver;
        driver = nullptr;
    }
    if (worker != nullptr) {
        worker->mUBContext = nullptr;
        delete worker;
        worker = nullptr;
    }
    if (qp != nullptr) {
        delete qp;
        qp = nullptr;
    }
    GlobalMockObject::verify();
}

std::vector<std::string> filters{};

TEST_F(TestNetDriverUB, InitializeParamErr)
{
    UBSHcomNetOutLogger *trueLogger = UBSHcomNetOutLogger::Instance();
    UBSHcomNetOutLogger *logger = nullptr;
    driver->mInited = true;
    EXPECT_EQ(driver->Initialize(option), NN_OK);

    driver->mInited = false;
    MOCKER(UBSHcomNetOutLogger::Instance)
        .stubs()
        .will(returnValue(logger))
        .then(returnValue(trueLogger));
    EXPECT_EQ(driver->Initialize(option), NN_NOT_INITIALIZED);
}

TEST_F(TestNetDriverUB, ValidateOptionsOobTypeErr)
{
    driver->mOptions.oobType = NET_OOB_UB;
    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    driver->mOptions.enableTls = true;
    EXPECT_EQ(driver->ValidateOptionsOobType(), NN_INVALID_PARAM);
}

TEST_F(TestNetDriverUB, InitializeOptErr)
{
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidateOptions).stubs().will(returnValue(1));

    EXPECT_EQ(driver->Initialize(option), 1);
    EXPECT_EQ(driver->Initialize(option), 1);
}

TEST_F(TestNetDriverUB, InitializeLoadOpensslErr)
{
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidateOptions).stubs().will(returnValue(0));
    option.enableTls = true;
    MOCKER_CPP(HcomSsl::Load).stubs().will(returnValue(1));
    EXPECT_EQ(driver->Initialize(option), NN_NOT_INITIALIZED);
    option.enableTls = false;
}

TEST_F(TestNetDriverUB, InitializeSizeErr)
{
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidaQpQueueSizeOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&UBContext::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateContext).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUB::UnInitializeInner).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverUB::CreateWorkerResource).stubs().will(returnValue(1));

    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    option.mrSendReceiveSegSize = OBMM_SIZE;

    EXPECT_EQ(driver->Initialize(option), 1);
    EXPECT_EQ(driver->Initialize(option), 1);
}

TEST_F(TestNetDriverUB, InitializeSizeErrTwo)
{
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&UBContext::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateContext).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::UnInitializeInner).stubs().will(ignoreReturnValue());

    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    option.mrSendReceiveSegSize = OBMM_SIZE;

    EXPECT_EQ(driver->Initialize(option), 100);
}

TEST_F(TestNetDriverUB, InitializeCtxErr)
{
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateContext).stubs().will(returnValue(0));
    MOCKER_CPP(&UBContext::Initialize).stubs().will(returnValue(1));
    MOCKER_CPP(&NetDriverUB::UnInitializeInner).stubs().will(ignoreReturnValue());

    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    option.mrSendReceiveSegSize = OBMM_SIZE;

    EXPECT_EQ(driver->Initialize(option), 1);
}

TEST_F(TestNetDriverUB, InitializeCreateWorkerErr)
{
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidaQpQueueSizeOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&UBContext::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateContext).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::UnInitializeInner).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverUB::CreateWorkerResource).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateWorkers).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateClientLB).stubs().will(returnValue(1));

    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    option.mrSendReceiveSegSize = OBMM_SIZE;

    EXPECT_EQ(driver->Initialize(option), 1);
    EXPECT_EQ(driver->Initialize(option), 1);
}

TEST_F(TestNetDriverUB, InitializeDoInitializeErr)
{
    driver->mInited = false;
    MOCKER_CPP(&UBSHcomNetDriverOptions::ValidateCommonOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidateOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::ValidaQpQueueSizeOptions).stubs().will(returnValue(0));
    MOCKER_CPP(&UBContext::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateContext).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::UnInitializeInner).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&NetDriverUB::CreateWorkerResource).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateWorkers).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateClientLB).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*driver, &NetDriverUBWithOob::DoInitialize).stubs()
        .will(returnValue(1)).then(returnValue(0));

    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    option.mrSendReceiveSegSize = OBMM_SIZE;

    EXPECT_EQ(driver->Initialize(option), 1);
    EXPECT_EQ(driver->Initialize(option), 0);
}

TEST_F(TestNetDriverUB, ValidateOptionsParamErr)
{
    MOCKER_CPP(ValidateArrayOptions).stubs().will(returnValue(true));
    MOCKER_CPP(&UBSHcomNetDriver::ValidateAndParseOobPortRange).stubs().will(returnValue(1)).then(returnValue(0));

    driver->mOptions.prePostReceiveSizePerQP = 0;
    driver->mOptions.maxPostSendCountPerQP = NN_NO2048;
    EXPECT_EQ(driver->ValidateOptions(), NN_INVALID_PARAM);

    // 65535 大于硬件上限，警告，最终因 ValidateAndParseOobPortRange 错误
    driver->mOptions.prePostReceiveSizePerQP = 65535;
    driver->mOptions.maxPostSendCountPerQP = 65535;
    EXPECT_EQ(driver->ValidateOptions(), NN_INVALID_PARAM);

    driver->mOptions.prePostReceiveSizePerQP = NN_NO2048;
    driver->mOptions.maxPostSendCountPerQP = 0;
    EXPECT_EQ(driver->ValidateOptions(), NN_INVALID_PARAM);
}

TEST_F(TestNetDriverUB, ValidateOptions)
{
    MOCKER_CPP(ValidateArrayOptions).stubs().will(returnValue(true));
    MOCKER_CPP(&UBSHcomNetDriver::ValidateAndParseOobPortRange).stubs().will(returnValue(1)).then(returnValue(0));

    MOCKER_CPP(&UBSHcomNetDriver::ValidateOptionsOobType).stubs().will(returnValue(1)).then(returnValue(0));

    driver->mOptions.prePostReceiveSizePerQP = NN_NO2048;
    driver->mOptions.maxPostSendCountPerQP = NN_NO2048;
    EXPECT_EQ(driver->ValidateOptions(), NN_INVALID_PARAM);

    driver->mOptions.prePostReceiveSizePerQP = NN_NO512;
    driver->mOptions.maxPostSendCountPerQP = NN_NO2048;
    EXPECT_EQ(driver->ValidateOptions(), NN_INVALID_PARAM);

    EXPECT_EQ(driver->ValidateOptions(), NN_OK);
}

static void MockSplitStr(const std::string &str, const std::string &separator, std::vector<std::string> &result)
{
    result = filters;
}

UResult MockedGetEnableDeviceCount(std::string ipMask, uint16_t &enableDevCount, std::vector<std::string> &enableIps,
                                   std::string ipGroup)
{
    enableIps.emplace_back("1.1.1.1");
    return UB_OK;
}

TEST_F(TestNetDriverUB, CreateContextParamErr)
{
    EXPECT_EQ(driver->CreateContext(), NN_OK);

    driver->mContext = nullptr;
    filters.clear();
    driver->mProtocol = UBSHcomNetDriverProtocol::TCP;
    MOCKER(NetFunc::NN_SplitStr).stubs().will(invoke(MockSplitStr));
    EXPECT_EQ(driver->CreateContext(), NN_ERROR);
}

TEST_F(TestNetDriverUB, CreateContextIPErr)
{
    std::vector<std::string> matchIps{};
    driver->mContext = nullptr;
    filters.clear();
    filters.emplace_back("192.168.1.1");
    driver->mProtocol = UBSHcomNetDriverProtocol::TCP;
    MOCKER(NetFunc::NN_SplitStr).stubs().will(invoke(MockSplitStr));
    MOCKER(FilterIp).stubs().with(any(), outBound(matchIps)).will(returnValue(0));
    MOCKER(UBDeviceHelper::Initialize).stubs().will(returnValue(1));

    EXPECT_EQ(driver->CreateContext(), NN_ERROR);
}

TEST_F(TestNetDriverUB, CreateContextInitializErr)
{
    std::vector<std::string> matchIps{};
    matchIps.emplace_back("192.168.1.1");
    driver->mContext = nullptr;
    filters.clear();
    filters.emplace_back("192.168.1.1");
    driver->mProtocol = UBSHcomNetDriverProtocol::TCP;
    MOCKER(NetFunc::NN_SplitStr).stubs().will(invoke(MockSplitStr));
    MOCKER(FilterIp).stubs().with(any(), outBound(matchIps)).will(returnValue(0));
    MOCKER(UBDeviceHelper::Initialize).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->CreateContext(), NN_ERROR);
    EXPECT_EQ(driver->CreateContext(), NN_ERROR);
}

TEST_F(TestNetDriverUB, CreateContextCreateErr)
{
    std::vector<std::string> matchIps{};
    matchIps.emplace_back("192.168.1.1");
    driver->mContext = nullptr;
    filters.clear();
    filters.emplace_back("192.168.1.1");
    driver->mProtocol = UBSHcomNetDriverProtocol::TCP;
    MOCKER(NetFunc::NN_SplitStr).stubs().will(invoke(MockSplitStr));
    MOCKER(FilterIp).stubs().with(any(), outBound(matchIps)).will(returnValue(0));
    MOCKER(UBDeviceHelper::Initialize).stubs().will(returnValue(0));
    MOCKER(UBContext::Create).stubs().will(returnValue(1));

    EXPECT_EQ(driver->CreateContext(), 1);
}

TEST_F(TestNetDriverUB, CreateContextSuccess)
{
    std::vector<std::string> matchIps{};
    matchIps.emplace_back("192.168.1.1");
    driver->mContext = nullptr;
    filters.clear();
    filters.emplace_back("192.168.1.1");
    driver->mProtocol = UBSHcomNetDriverProtocol::TCP;
    MOCKER(NetFunc::NN_SplitStr).stubs().will(invoke(MockSplitStr));
    MOCKER(FilterIp).stubs().with(any(), outBound(matchIps)).will(returnValue(0));
    MOCKER(UBDeviceHelper::Initialize).stubs().will(returnValue(0));
    MOCKER(UBContext::Create).stubs().with(any(), any(), outBound(ctx)).will(returnValue(0));

    EXPECT_EQ(driver->CreateContext(), 0);
}

TEST_F(TestNetDriverUB, CreateContextUbcModeMismatch)
{
    driver->mContext = nullptr;
    driver->mOptions.SetUbcMode(UBSHcomUbcMode::LowLatency);
    EXPECT_EQ(driver->CreateContext(), NN_ERROR);
}

TEST_F(TestNetDriverUB, CreateContextUbcModeOk)
{
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(static_cast<UResult>(UB_OK)));

    driver->mContext = nullptr;
    driver->mOptions.SetUbcMode(UBSHcomUbcMode::LowLatency);
    driver->mProtocol = UBSHcomNetDriverProtocol::UBC;
    EXPECT_EQ(driver->CreateContext(), NN_ERROR);
}

TEST_F(TestNetDriverUB, CreateSendMrErr)
{
    MOCKER(UBMemoryRegionFixedBuffer::Create).stubs().will(returnValue(1));
    EXPECT_EQ(driver->CreateSendMr(1), 1);
}

TEST_F(TestNetDriverUB, CreateSendMrInitializeErr)
{
    mDriverSendMR = new (std::nothrow) UBMemoryRegionFixedBuffer(mName, ctx, 0, 0, 0);
    ASSERT_NE(mDriverSendMR, nullptr);
    MOCKER(UBMemoryRegionFixedBuffer::Create)
        .stubs()
        .with(any(), any(), any(), any(), any(), outBound(mDriverSendMR))
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP_VIRTUAL(*mDriverSendMR, &UBMemoryRegionFixedBuffer::Initialize).stubs().will(returnValue(1));
    EXPECT_EQ(driver->CreateSendMr(1), 1);
    EXPECT_EQ(driver->CreateSendMr(1), 1);

    if (mDriverSendMR != nullptr) {
        delete mDriverSendMR;
        mDriverSendMR = nullptr;
    }
}

TEST_F(TestNetDriverUB, CreateSendMr)
{
    mDriverSendMR = new (std::nothrow) UBMemoryRegionFixedBuffer(mName, ctx, 0, 0, 0);
    ASSERT_NE(mDriverSendMR, nullptr);
    MOCKER(UBMemoryRegionFixedBuffer::Create)
        .stubs()
        .with(any(), any(), any(), any(), any(), outBound(mDriverSendMR))
        .will(returnValue(0));
    MOCKER_CPP_VIRTUAL(*mDriverSendMR, &UBMemoryRegionFixedBuffer::Initialize).stubs().will(returnValue(0));
    EXPECT_EQ(driver->CreateSendMr(1), 0);

    if (mDriverSendMR != nullptr) {
        delete mDriverSendMR;
        mDriverSendMR = nullptr;
    }
}

TEST_F(TestNetDriverUB, CreateOpCtxMemPoolErr)
{
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->CreateOpCtxMemPool(), 1);
    EXPECT_EQ(driver->CreateOpCtxMemPool(), 0);
}

TEST_F(TestNetDriverUB, CreateOpCtxMemPoolNullErr)
{
    NetMemPoolFixed *testOpCtxMemPool = nullptr;
    MOCKER_CPP(&NetRef<NetMemPoolFixed>::Get).stubs().will(returnValue(testOpCtxMemPool));
    EXPECT_EQ(driver->CreateOpCtxMemPool(), NN_INVALID_PARAM);
}

TEST_F(TestNetDriverUB, CreateSglCtxMemPoolErr)
{
    MOCKER_CPP(&NetMemPoolFixed::Initialize).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->CreateSglCtxMemPool(), 1);
    EXPECT_EQ(driver->CreateSglCtxMemPool(), 0);
}

TEST_F(TestNetDriverUB, CreateSglCtxMemPoolNullErr)
{
    NetMemPoolFixed *testSglOpCtxInfoPool = nullptr;
    MOCKER_CPP(&NetRef<NetMemPoolFixed>::Get).stubs().will(returnValue(testSglOpCtxInfoPool));
    EXPECT_EQ(driver->CreateSglCtxMemPool(), NN_INVALID_PARAM);
}

TEST_F(TestNetDriverUB, CreateWorkerResourceMrErr)
{
    MOCKER_CPP(&NetDriverUB::CreateSendMr).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateOpCtxMemPool).stubs().will(returnValue(1));

    EXPECT_EQ(driver->CreateWorkerResource(), 1);
    EXPECT_EQ(driver->CreateWorkerResource(), 1);
}

TEST_F(TestNetDriverUB, CreateWorkerResourceSuccess)
{
    MOCKER_CPP(&NetDriverUB::CreateSendMr).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateOpCtxMemPool).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUB::CreateSglCtxMemPool).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->CreateWorkerResource(), 0);
    EXPECT_EQ(driver->CreateWorkerResource(), 0);
}

TEST_F(TestNetDriverUB, DestroyEndpointErr)
{
    UBSHcomNetEndpointPtr ubEp = nullptr;
    EXPECT_NO_FATAL_FAILURE(driver->DestroyEndpoint(ubEp));
}

TEST_F(TestNetDriverUB, ClearWorkers)
{
    UBWorker *tmpWorker = new (std::nothrow) UBWorker(mName, ctx, workerOptions, memPool, sglMemPool);
    tmpWorker->mInited = false;
    tmpWorker->IncreaseRef();
    driver->mWorkers.emplace_back(tmpWorker);
    EXPECT_NO_FATAL_FAILURE(driver->ClearWorkers());
}

TEST_F(TestNetDriverUB, CreateWorkersErr)
{
    MOCKER(NetFunc::NN_ParseWorkersGroups).stubs().will(returnValue(true));
    MOCKER(NetFunc::NN_ParseWorkerGroupsCpus).stubs().will(returnValue(true));
    MOCKER(NetFunc::NN_FinalizeWorkerGroupCpus).stubs().will(returnValue(true));
    MOCKER(NetFunc::NN_ParseWorkersGroupsThreadPriority).stubs().will(returnValue(false));
    EXPECT_EQ(driver->CreateWorkers(), NN_INVALID_PARAM);
}

TEST_F(TestNetDriverUB, CreateWorkers)
{
    std::vector<uint16_t> workerGroups{ 1 };
    std::vector<int16_t> workerThreadPriority{ 1 };
    std::vector<int16_t> flatWorkerCpus{ 1 };
    MOCKER(NetFunc::NN_ParseWorkersGroups).stubs().with(any(), outBound(workerGroups)).will(returnValue(true));
    MOCKER(NetFunc::NN_ParseWorkerGroupsCpus).stubs().will(returnValue(true));
    MOCKER(NetFunc::NN_FinalizeWorkerGroupCpus)
        .stubs()
        .with(any(), any(), any(), outBound(flatWorkerCpus))
        .will(returnValue(true));
    MOCKER(NetFunc::NN_ParseWorkersGroupsThreadPriority)
        .stubs()
        .with(any(), outBound(workerThreadPriority), any())
        .will(returnValue(true));
    MOCKER_CPP(&UBWorker::Initialize).stubs().will(returnValue(1)).then(returnValue(0));

    driver->mOptions.workerThreadPriority = 1;
    EXPECT_EQ(driver->CreateWorkers(), NN_NEW_OBJECT_FAILED);
    EXPECT_EQ(driver->CreateWorkers(), 0);

    // clear resources
    delete driver->mWorkers[0];
    driver->mWorkers.clear();
}

TEST_F(TestNetDriverUB, UnInitializeErr)
{
    driver->mInited = false;
    EXPECT_NO_FATAL_FAILURE(driver->UnInitialize());
    driver->mInited = true;
    driver->mStarted = true;
    EXPECT_NO_FATAL_FAILURE(driver->UnInitialize());
}

TEST_F(TestNetDriverUB, UnInitialize)
{
    driver->mInited = true;
    driver->mStarted = false;
    driver->mContext = nullptr;
    EXPECT_NO_FATAL_FAILURE(driver->UnInitialize());
}

TEST_F(TestNetDriverUB, CreateMemoryRegion)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;

    driver->mInited = true;
    MOCKER_CPP(&UBMemoryRegion::InitializeForOneSide).stubs().will(returnValue(1)).then(returnValue(0));
    EXPECT_EQ(driver->CreateMemoryRegion(reinterpret_cast<uintptr_t>(mem), NN_NO8, mr), 1);
    EXPECT_EQ(driver->CreateMemoryRegion(reinterpret_cast<uintptr_t>(mem), NN_NO8, mr), 0);
}

TEST_F(TestNetDriverUB, CreateMemoryRegionAddressErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    driver->mInited = false;
    EXPECT_EQ(driver->CreateMemoryRegion(reinterpret_cast<uintptr_t>(mem), NN_NO8, mr), NN_EP_NOT_INITIALIZED);

    driver->mInited = true;
    EXPECT_EQ(driver->CreateMemoryRegion(0, NN_NO8, mr), NN_INVALID_PARAM);
}

TEST_F(TestNetDriverUB, CreateMemoryRegionInternalParamErr)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;

    EXPECT_EQ(driver->CreateMemoryRegion(0, mr), NN_INVALID_PARAM);
    driver->mInited = false;
    EXPECT_EQ(driver->CreateMemoryRegion(NN_NO8, mr), NN_EP_NOT_INITIALIZED);
}

TEST_F(TestNetDriverUB, CreateMemoryRegionInternal)
{
    UBSHcomNetMemoryRegionPtr mr = nullptr;
    driver->mInited = true;
    MOCKER_CPP(&UBMemoryRegion::InitializeForOneSide).stubs().will(returnValue(1)).then(returnValue(0));

    EXPECT_EQ(driver->CreateMemoryRegion(NN_NO8, mr), 1);
    EXPECT_EQ(driver->CreateMemoryRegion(NN_NO8, mr), 0);
}

TEST_F(TestNetDriverUB, StartParamErr)
{
    driver->mStarted = true;
    EXPECT_EQ(driver->Start(), NN_OK);

    driver->mStarted = false;
    driver->mInited = false;
    EXPECT_EQ(driver->Start(), NN_ERROR);
}

TEST_F(TestNetDriverUB, Start)
{
    driver->mStarted = false;
    driver->mInited = true;
    driver->mOptions.dontStartWorkers = true;
    MOCKER_CPP_VIRTUAL(*driver, &NetDriverUBWithOob::DoStart).stubs().will(returnValue(0));
    EXPECT_EQ(driver->Start(), NN_OK);
}

TEST_F(TestNetDriverUB, Stop)
{
    driver->mStarted = false;
    EXPECT_NO_FATAL_FAILURE(driver->Stop());

    driver->mStarted = true;
    MOCKER_CPP_VIRTUAL(*driver, &NetDriverUBWithOob::DoStop).stubs().will(ignoreReturnValue());
    EXPECT_NO_FATAL_FAILURE(driver->Stop());
}

TEST_F(TestNetDriverUB, DoInitializeWithoutWorker)
{
    driver->mStartOobSvr = false;
    EXPECT_EQ(driver->DoInitialize(), NN_OK);

    driver->mStartOobSvr = true;
    MOCKER_CPP(&UBSHcomNetDriver::CreateListeners).stubs().will(returnValue(1));
    EXPECT_EQ(driver->DoInitialize(), NN_ERROR);
}

TEST_F(TestNetDriverUB, DoUnInitialize)
{
    driver->mStarted = true;
    EXPECT_NO_FATAL_FAILURE(driver->DoUnInitialize());

    driver->mStarted = false;
    EXPECT_NO_FATAL_FAILURE(driver->DoUnInitialize());
}

TEST_F(TestNetDriverUB, HandleCqEventParamErr)
{
    urma_async_event_t event{};
    urma_jfc_t jfc{};
    event.element.jfc = nullptr;
    EXPECT_NO_FATAL_FAILURE(driver->HandleCqEvent(&event));

    event.element.jfc = &jfc;
    jfc.jfc_cfg.user_ctx = 1;
    event.element.jfc->jfc_cfg.user_ctx = reinterpret_cast<uint64_t>(&worker);
    MOCKER_CPP(&UBWorker::Stop).stubs().will(returnValue(1));
    EXPECT_NO_FATAL_FAILURE(driver->HandleCqEvent(&event));
}

TEST_F(TestNetDriverUB, HandleCqEvent)
{
    urma_async_event_t event{};
    urma_jfc_t jfc{};

    event.element.jfc = &jfc;
    jfc.jfc_cfg.user_ctx = 1;
    event.element.jfc->jfc_cfg.user_ctx = reinterpret_cast<uint64_t>(&worker);
    MOCKER_CPP(&UBWorker::Stop).stubs().will(returnValue(0));
    MOCKER_CPP(&NetDriverUBWithOob::DestroyEpInWorker).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBWorker::ReInitializeCQ).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&UBWorker::Start).stubs().will(returnValue(1));
    EXPECT_NO_FATAL_FAILURE(driver->HandleCqEvent(&event));
    EXPECT_NO_FATAL_FAILURE(driver->HandleCqEvent(&event));
}

TEST_F(TestNetDriverUB, ParseUrl)
{
    std::string badUrl = "127.0.0.1:9981";
    std::string testUrl = "uds://name";
    std::string testUrl2 = "tcp://127.0.0.1:0";
    std::string testUrl3 = "tcp://127.0.0.1:9981";
    std::string testUrl4 = "ubc://1111:2222:0000:0000:0000:0000:0100:0000:1";
    std::string testUrl5 = "ubc://1111:2222:0000:0000:0000:0000:0100:0000:888";
    NetDriverOobType type;
    std::string ip{};
    uint16_t port = 0;

    EXPECT_EQ(driver->ParseUrl(badUrl, type, ip, port), NN_PARAM_INVALID);
    EXPECT_EQ(driver->ParseUrl(testUrl, type, ip, port), SER_OK);
    EXPECT_EQ(driver->ParseUrl(testUrl2, type, ip, port), NN_PARAM_INVALID);
    EXPECT_EQ(driver->ParseUrl(testUrl3, type, ip, port), SER_OK);
    EXPECT_EQ(driver->ParseUrl(testUrl4, type, ip, port), NN_PARAM_INVALID);
    EXPECT_EQ(driver->ParseUrl(testUrl5, type, ip, port), SER_OK);
    EXPECT_EQ(type, NetDriverOobType::NET_OOB_UB);
    EXPECT_EQ(ip, "1111:2222:0000:0000:0000:0000:0100:0000");
    EXPECT_EQ(port, 888);
}

TEST_F(TestNetDriverUB, SetNetDeviceIpMask)
{
    UBSHcomNetDriverOptions opt{};
    std::vector<std::string> mask{};
    mask.emplace_back("1.2.3.4");
    mask.emplace_back("1.2.3.5");
    EXPECT_EQ(opt.SetNetDeviceIpMask(mask), true);
}

TEST_F(TestNetDriverUB, SetNetDeviceIpGroup)
{
    UBSHcomNetDriverOptions opt{};
    std::vector<std::string> ipGroup{};
    ipGroup.emplace_back("1.2.3.4");
    ipGroup.emplace_back("1.2.3.5");
    EXPECT_EQ(opt.SetNetDeviceIpGroup(ipGroup), true);
}

TEST_F(TestNetDriverUB, SetWorkerGroupsInfo)
{
    UBSHcomNetDriverOptions opt{};
    std::vector<UBSHcomWorkerGroupInfo> workerGroups{};
    std::vector<UBSHcomWorkerGroupInfo> workerGroups2{};
    UBSHcomWorkerGroupInfo info1{};
    UBSHcomWorkerGroupInfo info2{};
    workerGroups.emplace_back(info1);
    workerGroups.emplace_back(info2);
    EXPECT_EQ(opt.SetWorkerGroupsInfo(workerGroups), true);
    EXPECT_EQ(opt.SetWorkerGroupsInfo(workerGroups2), false);
}

TEST_F(TestNetDriverUB, DestroyMemoryRegion)
{
    UBMemoryRegion *mr = new (std::nothrow) UBMemoryRegion("name", nullptr, 0, 0, 0);
    MOCKER_CPP_VIRTUAL(*mr, &UBMemoryRegion::UnInitialize).stubs();
    UBSHcomNetMemoryRegionPtr mrPtr = mr;
    EXPECT_NO_FATAL_FAILURE(driver->DestroyMemoryRegion(mrPtr));
}

TEST_F(TestNetDriverUB, GetTseg)
{
    urma_target_seg_t *tseg = nullptr;
    EXPECT_EQ(driver->GetTseg(0, tseg), static_cast<uint32_t>(UB_PARAM_INVALID));
}
}
}
#endif
