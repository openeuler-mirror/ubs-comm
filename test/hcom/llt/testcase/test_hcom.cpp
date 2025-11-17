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

#include "transport/rdma/verbs/net_rdma_driver.h"
#include "transport/rdma/verbs/net_rdma_driver_oob.h"
#include "common/net_util.h"
#include "ut_helper.h"
#include "net_trace.h"
#include "test_hcom.h"

using namespace ock::hcom;

static int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    return 0;
}

static void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("end point " << ep->Id());
}

static int RequestReceived(const UBSHcomNetRequestContext &ctx)
{
    // std::string req((char *)ctx.Message()->Data(), ctx.Header().dataLength);
    return 0;
}

static int RequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted");
    return 0;
}
static int OneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done");
    return 0;
}

TestHcom::TestHcom() {}
static UBSHcomNetDriverOptions options {};
void TestHcom::SetUp()
{
#ifdef RDMA_BUILD_ENABLED
    MOCK_VERSION
    if (HcomIbv::Load() != 0) {
        NN_LOG_ERROR("Failed to load verbs API");
    }
#endif
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING; // 只支持EVENT模式
    options.mrSendReceiveSegSize = NN_NO1024;
    options.mrSendReceiveSegCount = NN_NO1024;
    options.enableTls = false;
    options.SetNetDeviceIpMask(IP_SEG);
}

void TestHcom::TearDown()
{
    GlobalMockObject::verify();
}

static void Log(int level, const char *msg)
{
    struct timeval tv {};
    char strTime[24];

    (void)gettimeofday(&tv, nullptr);
    (void)strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", localtime(&tv.tv_sec));

    static  std::string levelInfo[4] = {"debug", "info", "warn", "error"};

    std::cout << strTime << tv.tv_usec << " " << levelInfo[level & NN_NO3] << " " << msg <<
        " ExteralLogFunc" << std::endl;
}

TEST_F(TestHcom, InstanceOfTcpProtocolSuccess)
{
    auto tcpDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "tcpServer", true);
    UBSHcomNetDriverDeviceInfo deviceInfo;
    bool ret = tcpDriver->LocalSupport(ock::hcom::TCP, deviceInfo);
    EXPECT_EQ(true, ret);
    uint16_t testPort = 9989;
    tcpDriver->OobIpAndPort(BASE_IP, testPort);
    tcpDriver->Initialize(options);
    tcpDriver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    tcpDriver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    tcpDriver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    tcpDriver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
    NResult result = tcpDriver->Initialize(options);
    EXPECT_EQ(NNCode::NN_OK, result);
    tcpDriver->Stop();
}


TEST_F(TestHcom, InstanceOfUdsProtocolSuccess)
{
    auto udsDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::UDS, "udsSerDriver", true);
    UBSHcomNetDriverDeviceInfo deviceInfo;
    bool ret = udsDriver->LocalSupport(ock::hcom::UDS, deviceInfo);
    EXPECT_EQ(true, ret);
    uint16_t testPort = 9989;
    udsDriver->OobIpAndPort(BASE_IP, testPort);
    udsDriver->Initialize(options);
    udsDriver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    udsDriver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    udsDriver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    udsDriver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
    NResult result = udsDriver->Initialize(options);
    EXPECT_EQ(NNCode::NN_OK, result);
    udsDriver->Stop();
}

TEST_F(TestHcom, ExteralLogFunc)
{
    NetLogger::Instance()->SetExternalLogFunction(Log);
    NN_LOG_DEBUG("debug_log");
    NN_LOG_INFO("info_log");
    NN_LOG_WARN("warn_log");
    NN_LOG_ERROR("error_log");
    NetLogger::Instance()->SetExternalLogFunction(nullptr);
}

#ifdef RDMA_BUILD_ENABLED
TEST_F(TestHcom, InstanceOfRDMAProtocolSuccess)
{
    auto rdmaDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rServer", true);
    UBSHcomNetDriverDeviceInfo deviceInfo;
    bool ret = rdmaDriver->LocalSupport(ock::hcom::RDMA, deviceInfo);
    EXPECT_EQ(true, ret);
    uint16_t testPort = 9989;
    rdmaDriver->OobIpAndPort(BASE_IP, testPort);
    rdmaDriver->Initialize(options);
    rdmaDriver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    rdmaDriver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
    rdmaDriver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    rdmaDriver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
    NResult result = rdmaDriver->Initialize(options);
    EXPECT_EQ(NNCode::NN_OK, result);
    rdmaDriver->Stop();
}
#endif

TEST_F(TestHcom, InstanceOfOtherProtocolFailed)
{
    UBSHcomNetDriverProtocol driverProtocol = (UBSHcomNetDriverProtocol)NN_NO100;

    auto otherDriver = UBSHcomNetDriver::Instance(driverProtocol, "otherServer", true);
    EXPECT_EQ(nullptr, otherDriver);
}

#ifdef RDMA_BUILD_ENABLED
TEST_F(TestHcom, LocalSupportOtherFailed)
{
    auto otherDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::RDMA, "rServer", true);
    UBSHcomNetDriverProtocol driverProtocol = (UBSHcomNetDriverProtocol)NN_NO100;

    UBSHcomNetDriverDeviceInfo deviceInfo;
    bool ret = otherDriver->LocalSupport(driverProtocol, deviceInfo);
    EXPECT_EQ(false, ret);
}
#endif

TEST_F(TestHcom, InstanceWithoutHtracer)
{
    auto tcpDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "driver_without_htracer", true);
    ASSERT_EQ(NetTrace::gTraceInst, nullptr);
    tcpDriver->Stop();
}

TEST_F(TestHcom, InstanceWithHtracer)
{
    setenv("HCOM_ENABLE_TRACE", "1", 1);
    auto tcpDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "driver_with_htracer", true);
    ASSERT_NE(NetTrace::gTraceInst, nullptr);
    tcpDriver->Stop();
    unsetenv("HCOM_ENABLE_TRACE");
}