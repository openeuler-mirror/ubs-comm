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

#ifdef RDMA_BUILD_ENABLED
#include "transport/rdma/verbs/net_rdma_driver.h"
#include "transport/rdma/verbs/net_rdma_driver_oob.h"
#include "transport/rdma/verbs/net_rdma_async_endpoint.h"
#include "common/net_util.h"
#include "test_net_rdma_driver.hpp"
#include "ut_helper.h"

using namespace ock::hcom;

int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    return 0;
}

void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("end point " << ep->Id());
}

int RequestReceived(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

int RequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted");
    return 0;
}
int OneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done");
    return 0;
}

TestNetDriverRDMA::TestNetDriverRDMA() {}

UBSHcomNetDriver *driver = nullptr;
UBSHcomNetDriverOptions options{};

UBSHcomNetDriver *CreateDriver(std::string name, bool isOobServer, UBSHcomNetDriverSecType secType = NET_SEC_DISABLED)
{
    UBSHcomNetDriver *innerDriver = nullptr;
    std::string ipSeg = IP_SEG;
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING; // 只支持EVENT模式
    options.mrSendReceiveSegSize = NN_NO1024;
    options.mrSendReceiveSegCount = NN_NO1024;
    options.secType = secType;
    options.enableTls = false;
    options.SetNetDeviceIpMask(ipSeg);
    uint16_t testPort = 9989;
    innerDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, name, isOobServer);
    innerDriver->OobIpAndPort(BASE_IP, testPort);

    innerDriver->RegisterNewEPHandler(
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    innerDriver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    innerDriver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    innerDriver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    innerDriver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
    return innerDriver;
}

void TestNetDriverRDMA::SetUp()
{
    MOCK_VERSION
    if (HcomIbv::Load() != 0) {
        NN_LOG_ERROR("Failed to load verbs API");
    }
    driver = CreateDriver("rdmaServer1", true);
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestNetDriverRDMA::TearDown()
{
    GlobalMockObject::verify();
    driver->Stop();
    driver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(driver->Name());
}

TEST_F(TestNetDriverRDMA, InitSuccess)
{
    NResult result = driver->Initialize(options);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetDriverRDMA, InitTwiceSuccess)
{
    NResult result = driver->Initialize(options);
    result = driver->Initialize(options);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetDriverRDMA, InitSizeOverSuccess)
{
    options.prePostReceiveSizePerQP = NN_NO1024;
    options.maxPostSendCountPerQP = NN_NO1024;
    NResult result = driver->Initialize(options);
    EXPECT_EQ(NNCode::NN_OK, result);

    options.prePostReceiveSizePerQP = NN_NO64;
    options.maxPostSendCountPerQP = NN_NO64;
}

TEST_F(TestNetDriverRDMA, InitSizeZeroFail)
{
    options.prePostReceiveSizePerQP = 0;
    NResult result = driver->Initialize(options);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);

    options.prePostReceiveSizePerQP = NN_NO64;
}

TEST_F(TestNetDriverRDMA, InitPostSendCountZeroFail)
{
    options.maxPostSendCountPerQP = 0;
    NResult result = driver->Initialize(options);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);

    options.maxPostSendCountPerQP = NN_NO64;
}

TEST_F(TestNetDriverRDMA, InitWithoutIpMaskFailed)
{
    options.SetNetDeviceIpMask("");
    NResult result = driver->Initialize(options);
    EXPECT_EQ(NNCode::NN_INVALID_IP, result);
    options.SetNetDeviceIpMask(IP_SEG);
}

TEST_F(TestNetDriverRDMA, InitWithInvalidTypeFailed)
{
    NetDriverOobType x = (NetDriverOobType)NN_NO9;
    options.oobType = x;
    NResult result = driver->Initialize(options);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
    options.oobType = ock::hcom::NET_OOB_TCP;
}

TEST_F(TestNetDriverRDMA, StartSuccess)
{
    uint32_t testTimeout = 1000;
    options.eventPollingTimeout = testTimeout;
    NResult result = driver->Initialize(options);
    result = driver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetDriverRDMA, StartTwiceSuccess)
{
    NResult result = driver->Initialize(options);
    result = driver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    result = driver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetDriverRDMA, StartWithoutStartWorkersSuccess)
{
    options.dontStartWorkers = true;
    NResult result = driver->Initialize(options);

    result = driver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);
    options.dontStartWorkers = false;

    driver->DumpObjectStatistics();
}

TEST_F(TestNetDriverRDMA, StartWithoutInitializeFailed)
{
    NResult result = driver->Start();
    EXPECT_EQ(NNCode::NN_ERROR, result);
}

TEST_F(TestNetDriverRDMA, StartWithoutNewEPHdFailed)
{
    NResult result = driver->Initialize(options);
    driver->RegisterNewEPHandler(nullptr);

    result = driver->Start();
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetDriverRDMA, StartWithoutEPBrokenHdFailed)
{
    NResult result = driver->Initialize(options);
    driver->RegisterEPBrokenHandler(nullptr);

    result = driver->Start();
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetDriverRDMA, StartWithoutNewReqHdFailed)
{
    NResult result = driver->Initialize(options);
    driver->RegisterNewReqHandler(nullptr);

    result = driver->Start();
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetDriverRDMA, StartWithoutReqPostedHdFailed)
{
    NResult result = driver->Initialize(options);
    driver->RegisterReqPostedHandler(nullptr);
    result = driver->Start();
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetDriverRDMA, StartWithoutOneSideHdFailed)
{
    NResult result = driver->Initialize(options);
    driver->RegisterOneSideDoneHandler(nullptr);

    result = driver->Start();
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}


TEST_F(TestNetDriverRDMA, DriverOobConnectWithoutClientCbFailed)
{
    driver->Initialize(options);
    driver->Start();

    UBSHcomNetDriver *oobC = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "c", true);
    uint16_t testPort = 9989;
    oobC->OobIpAndPort(BASE_IP, testPort);
    oobC->Initialize(options);
    NResult result = oobC->Start();
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = oobC->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_ERROR, result);
}

TEST_F(TestNetDriverRDMA, DriverOobConnectSuccess)
{
    driver->Initialize(options);
    driver->Start();
    uint16_t testPort = 9989;
    UBSHcomNetDriver *oobC = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "cSuccess", false);

    oobC->OobIpAndPort(BASE_IP, testPort);
    oobC->Initialize(options);
    oobC->RegisterNewEPHandler(
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    oobC->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    oobC->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    oobC->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    oobC->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    NResult result = oobC->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = oobC->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OK, result);

    oobC->Stop();
    oobC->UnInitialize();
}

int CreateAuthInfo(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output, uint32_t &outLen,
    bool &needAutoFree)
{
    const char *kToken = "token";
    flag = 1;
    output = const_cast<char *>(kToken);
    outLen = strlen(kToken);
    type = NET_SEC_VALID_TWO_WAY;
    NN_LOG_INFO("auth info " << output << " len:" << outLen << " flag:" << flag << " sec type:" <<
        UBSHcomNetDriverSecTypeToString(type));
    return 0;
}

int CreateAuthInfoFailed(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output, uint32_t &outLen,
    bool &needAutoFree)
{
    return -1;
}

int AuthValidate(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
{
    if (input != nullptr) {
        NN_LOG_INFO("Auth validate flag:" << flag << " ctx:" << ctx);
    } else {
        NN_LOG_INFO("Auth validate flag:" << flag << " ctx:" << ctx << " input:" << input << " input Len:" << inputLen);
    }

    return 0;
}

int AuthValidateFailed(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)
{
    if (input != nullptr) {
        NN_LOG_INFO("Auth validate flag:" << flag << " ctx:" << ctx);
    } else {
        NN_LOG_INFO("Auth validate flag:" << flag << " ctx:" << ctx << " input:" << input << " input Len:" << inputLen);
    }

    return -1;
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecSuccess)
{
    auto sDriver = CreateDriver("twoWaySecServer", true);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    sDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("twoWaySecClient", false);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OK, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    sDriver->Stop();
    sDriver->UnInitialize();
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithClientSendError)
{
    auto sDriver = CreateDriver("twoWaySecServer1", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    sDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));
    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("CreateAuthInfoFailedCli", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfoFailed, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5,
        std::placeholders::_6));
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithServerValidError)
{
    auto sDriver = CreateDriver("AuthValidateFailedServer", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    sDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidateFailed, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("twoWaySecClient1", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithServerSendError)
{
    auto sDriver = CreateDriver("CreateAuthInfoFailedSrv", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfoFailed, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5,
        std::placeholders::_6));
    sDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));
    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("twoWaySecClient2", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithClientValidError)
{
    auto sDriver = CreateDriver("twoWaySecServer", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    sDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));
    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("AuthValidateFailedClient", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidateFailed, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithClientNotSetProvider)
{
    auto sDriver = CreateDriver("twoWaySecServer3", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    sDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));
    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("NotSetProviderClient", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithClientNotSetValidator)
{
    auto sDriver = CreateDriver("twoWaySecServer4", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    sDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));
    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("NotSetValidatorClient", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithServerNotSetValidator)
{
    auto sDriver = CreateDriver("NotSetValidatorServer", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));

    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("twoWaySecClient3", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobSecTwoWaySecFailedWithServerNotSetProvider)
{
    auto sDriver = CreateDriver("NotSetProviderServer", true, NET_SEC_VALID_TWO_WAY);
    sDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    sDriver->Initialize(options);
    NResult result = sDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    auto cDriver = CreateDriver("twoWaySecClient4", false, NET_SEC_VALID_TWO_WAY);
    cDriver->RegisterEndpointSecInfoProvider(std::bind(&CreateAuthInfo, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    cDriver->RegisterEndpointSecInfoValidator(std::bind(&AuthValidate, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4));

    cDriver->Initialize(options);
    result = cDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = cDriver->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    cDriver->Stop();
    cDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(cDriver->Name());
    sDriver->Stop();
    sDriver->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(sDriver->Name());
}

TEST_F(TestNetDriverRDMA, DriverOobConnectSendReceiveFailed)
{
    options.secType = NET_SEC_VALID_TWO_WAY;
    driver->Initialize(options);
    driver->Start();
    uint16_t testPort = 9989;
    UBSHcomNetDriver *oobC = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "cSendReceiveFailed", false);

    oobC->OobIpAndPort(BASE_IP, testPort);
    oobC->Initialize(options);
    oobC->RegisterNewEPHandler(
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    oobC->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    oobC->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    oobC->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    oobC->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
    NResult result = oobC->Start();
    EXPECT_EQ(NNCode::NN_OK, result);

    UBSHcomNetEndpointPtr ep = nullptr;
    result = oobC->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_SEC_PROCESS_ERROR, result);

    MOCKER(::recv).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    MOCKER(::send).stubs().will(returnValue(static_cast<ssize_t>(-1)));
    result = oobC->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OOB_CLIENT_SOCKET_ERROR, result);

    oobC->Stop();
    oobC->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(oobC->Name());
    GlobalMockObject::verify();
}

TEST_F(TestNetDriverRDMA, DriverOobConnectEmplaceFailed)
{
    options.enableTls = false;
    driver->Initialize(options);
    driver->Start();
    uint64_t epId = 1;
    UBSHcomNetWorkerIndex workerIndex{};
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(epId, nullptr, nullptr, workerIndex);
    EXPECT_NE(ep.Get(), nullptr);
    driver->mEndPoints.emplace(epId, ep);
    MOCKER_CPP(&UBSHcomNetEndpoint::Id).stubs().will(returnValue(epId));

    UBSHcomNetEndpointPtr ep1 = nullptr;
    NResult result = driver->Connect(BASE_IP, 9989, "a", ep1, 0, 0);
    EXPECT_EQ(result, NN_ERROR);
    GlobalMockObject::verify();
}

TEST_F(TestNetDriverRDMA, DriverOobConnectUdsSuccess)
{
    UBSHcomNetOobUDSListenerOptions opt{};

    opt.Name("udsServer");
    opt.perm = 0;
    UBSHcomNetDriver *oobUdsS = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "udsServer", true);

    oobUdsS->RegisterNewEPHandler(
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    oobUdsS->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    oobUdsS->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    oobUdsS->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    oobUdsS->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    options.oobType = ock::hcom::NET_OOB_UDS;

    oobUdsS->AddOobUdsOptions(opt);
    oobUdsS->OobUdsName("udsServer");

    oobUdsS->Initialize(options);
    NResult result = oobUdsS->Start();
    UBSHcomNetDriver *oobUdsC = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "udsClient", false);
    oobUdsC->RegisterNewEPHandler(
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    oobUdsC->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    oobUdsC->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    oobUdsC->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    oobUdsC->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    options.oobType = ock::hcom::NET_OOB_UDS;
    oobUdsC->OobUdsName("udsServer");
    oobUdsC->Initialize(options);
    oobUdsC->AddOobUdsOptions(opt);
    result = oobUdsC->Start();

    EXPECT_EQ(NNCode::NN_OK, result);
    UBSHcomNetEndpointPtr ep = nullptr;

    result = oobUdsC->Connect("a", ep, 0, 0, 0);
    EXPECT_EQ(NNCode::NN_OK, result);

    options.oobType = ock::hcom::NET_OOB_TCP;
    oobUdsS->Stop();
    oobUdsS->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(oobUdsS->Name());
    oobUdsC->Stop();
    oobUdsC->UnInitialize();
    UBSHcomNetDriver::DestroyInstance(oobUdsC->Name());
}

TEST_F(TestNetDriverRDMA, CreateMemoryRegionSuccess)
{
    driver->Initialize(options);

    UBSHcomNetMemoryRegionPtr mr;
    NResult result = driver->CreateMemoryRegion(NN_NO1024, mr);

    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetDriverRDMA, CreateMemoryWithAddRegionSuccess)
{
    driver->Initialize(options);

    UBSHcomNetMemoryRegionPtr mr;
    uintptr_t address = 1;
    NResult result = driver->CreateMemoryRegion(address, NN_NO1024, mr);

    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetDriverRDMA, CreateMemoryWithAddRegionFailed)
{
    driver->Initialize(options);

    UBSHcomNetMemoryRegionPtr mr;
    NResult result = driver->CreateMemoryRegion(0, NN_NO1024, mr);

    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetDriverRDMA, DestroyMemoryRegionSuccess)
{
    driver->Initialize(options);

    UBSHcomNetMemoryRegionPtr mr;
    NResult result = driver->CreateMemoryRegion(NN_NO1024, mr);
    EXPECT_EQ(NNCode::NN_OK, result);
    driver->DestroyMemoryRegion(mr);
    EXPECT_EQ(NNCode::NN_OK, result);
}
#endif
