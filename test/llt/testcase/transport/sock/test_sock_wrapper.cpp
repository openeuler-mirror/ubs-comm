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

#include "test_sock_wrapper.h"
#include "hcom.h"
#include "net_sock_common.h"
#include "net_oob_ssl.h"

using namespace ock::hcom;

TestSockWrapper::TestSockWrapper() {}

#define BASE_IP "127.0.0.1"
#define IP_SEG "127.0.0.0/16"
static char sendTemp[] = "hello world";
static UBSHcomNetEndpointPtr sockServerEp = nullptr;

using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint32_t lKey = 0;
    uint32_t size = 0;
} __attribute__((packed));
static TestRegMrInfo localMrInfo[4];
static TestRegMrInfo remoteMrInfo[4];
static TestRegMrInfo serverMrInfo[4];

static UBSHcomNetTransSgeIov iovPtr[NN_NO4];
static uint32_t iovCnt = NN_NO4;
static UBSHcomNetMemoryRegionPtr mr = nullptr;
static UBSHcomNetTransSgeIov iov[NN_NO4];
sem_t sem_sock;

static int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload << " ep id " << newEP->Id());
    sockServerEp = newEP;
    return 0;
}

static void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("end point " << ep->Id());
    if (sockServerEp != nullptr) {
        sockServerEp.Set(nullptr);
    }
}

static int RequestReceived(const UBSHcomNetRequestContext &ctx) // 0
{
    return 0;
}

static int RequestReceivedWithSend(const UBSHcomNetRequestContext &ctx) // 1
{
    static char data[100] = {};

    UBSHcomNetTransRequest req((void *)(data), sizeof(data), 0);
    sockServerEp->PostSend(1, req);
    return 0;
}

static int RequestReceivedSglClient(const UBSHcomNetRequestContext &ctx) // 2
{
    memcpy(remoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen());
    NN_LOG_INFO("get remote Mr info");
    for (uint16_t i = 0; i < NN_NO4; i++) {
        NN_LOG_INFO("idx:" << i << " key:" << remoteMrInfo[i].lKey << " address:" << remoteMrInfo[i].lAddress <<
            " size" << remoteMrInfo[i].size);
    }
    sem_post(&sem_sock);
    return 0;
}

static int RequestReceivedSglServer(const UBSHcomNetRequestContext &ctx) // 3
{
    NN_LOG_INFO("request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp((void *)(serverMrInfo), sizeof(serverMrInfo), 0);
    if ((result = sockServerEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }

    NN_LOG_INFO("request rsp Mr info");
    for (uint16_t i = 0; i < NN_NO4; i++) {
        NN_LOG_INFO("idx:" << i << " key:" << serverMrInfo[i].lKey << " address:" << serverMrInfo[i].lAddress <<
            " size" << serverMrInfo[i].size);
    }
    return 0;
}
static int RequestPosted(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

static int OneSideDone(const UBSHcomNetRequestContext &ctx)
{
    sem_post(&sem_sock);
    return 0;
}

static void CreateServerMR(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = driver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return;
        }
        serverMrInfo[i].lAddress = mr->GetAddress();
        serverMrInfo[i].lKey = mr->GetLKey();
        serverMrInfo[i].size = NN_NO16;
        mrs.push_back(mr);
        memset(reinterpret_cast<void *>(serverMrInfo[i].lAddress), 0, NN_NO16);
    }
}

static void CreateClientMR(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = driver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return;
        }
        localMrInfo[i].lAddress = mr->GetAddress();
        localMrInfo[i].lKey = mr->GetLKey();
        localMrInfo[i].size = NN_NO16;
        mrs.push_back(mr);
        memset(reinterpret_cast<void *>(localMrInfo[i].lAddress), 0, NN_NO16);
    }
}

static void SockWrapperDestoryMem(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    while (!mrs.empty()) {
        driver->DestroyMemoryRegion(mrs.back());
        mrs.pop_back();
    }
}


static void SetCB(UBSHcomNetDriver *driver, bool isServer, uint8_t reqHandlerMode)
{
    if (isServer) {
        driver->RegisterNewEPHandler(
            std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    switch (reqHandlerMode) {
        case 0:
            driver->RegisterNewReqHandler(std::bind(&RequestReceived, std::placeholders::_1));
            break;
        case 1:
            driver->RegisterNewReqHandler(std::bind(&RequestReceivedWithSend, std::placeholders::_1));
            break;
        case 2:
            driver->RegisterNewReqHandler(std::bind(&RequestReceivedSglClient, std::placeholders::_1));
            break;
        case 3:
            driver->RegisterNewReqHandler(std::bind(&RequestReceivedSglServer, std::placeholders::_1));
            break;
    }
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));
}

static int TestNewConnectionHandler(ock::hcom::OOBTCPConnection &conn1)
{
    char *sendBuff = sendTemp;
    NResult ret = conn1.Send(sendBuff, strlen(sendBuff));
    EXPECT_EQ(ret, ock::hcom::NN_OK);
    return ret;
}

static void SetDriverOptions(UBSHcomNetDriverOptions &sockOptions)
{
    sockOptions.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    sockOptions.SetNetDeviceIpMask(IP_SEG);
    sockOptions.pollingBatchSize = 16;
    sockOptions.enableTls = false;
    sockOptions.SetWorkerGroups("1");
    sockOptions.SetWorkerGroupsCpuSet("10-10");
}

static int SockConnect(uint16_t port)
{
    OOBTCPConnection *conn = nullptr;
    OOBTCPServer oobServer(BASE_IP, port);
    oobServer.SetNewConnCB(std::bind(&TestNewConnectionHandler, std::placeholders::_1));
    UBSHcomNetDriverOptions mOptions;
    auto *lb = new (std::nothrow) ock::hcom::NetWorkerLB("mName", mOptions.lbPolicy, UINT16_MAX);
    oobServer.SetWorkerLb(lb);
    oobServer.Start();
    ock::hcom::OOBTCPClientPtr client = new (std::nothrow) OOBTCPClient(BASE_IP, port);

    client->Connect(BASE_IP, port, conn);
    auto connFd = conn->TransferFd();
    return connFd;
}

static void CloseDriver(UBSHcomNetDriver *&driver)
{
    std::string name = driver->Name();
    if (driver->IsStarted()) {
        driver->Stop();
        driver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(name);
}
void TestSockWrapper::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}
void TestSockWrapper::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestSockWrapper, SockInitializeSuccess)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-1", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_OK, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithInvalidType)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_UDS_TCP, "sock-2", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_PARAM_INVALID, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeTwice)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-3", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_OK, result);
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_OK, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithInvalidFd)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-4", newSockId, -1, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_PARAM_INVALID, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithInvalidReceiveBuf)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-5", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    MOCKER(setsockopt).defaults().will(returnValue(-1));
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_TCP_SET_OPTION_FAILED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithInvalidSendBuf)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-6", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    MOCKER(setsockopt).defaults().will(returnValue(0)).then(returnValue(-1));
    SResult result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_TCP_SET_OPTION_FAILED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithUDS)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_UDS, "sock-7", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(NN_OK, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithExpand)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    option.receiveBufSizeKB = 0;
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-8", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    sockWorkerOptions.keepaliveIdleTime = -1;
    sockWorkerOptions.keepaliveProbeInterval = -1;
    sockWorkerOptions.keepaliveProbeTimes = -1;
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_TCP_SET_OPTION_FAILED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithNoDelay)
{
    auto connFd = SockConnect(9981);
    SockOptions option{};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-9", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions{};
    MOCKER(setsockopt)
        .defaults()
        .will(returnValue(0))
        .then(returnValue(0))
        .then(returnValue(0))
        .then(returnValue(0))
        .then(returnValue(-1));
    SResult result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_TCP_SET_OPTION_FAILED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockSendSuccess)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-10", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    sockWorkerOptions.tcpEnableNoDelay = false;
    SResult result;
    sock->Initialize(sockWorkerOptions);
    std::string payload = "hello world";
    void *tmpBuf = const_cast<char *>(payload.c_str());
    result = sock->Send(tmpBuf, payload.length());
    EXPECT_EQ(NN_OK, result);
    char receiveBuf[payload.length() + 1];
    bzero(receiveBuf, payload.length() + 1);
    void *buff = reinterpret_cast<void *>(receiveBuf);
    result = sock->Receive(buff, payload.length());
    std::string receivePayload = reinterpret_cast<char *>(receiveBuf);
    EXPECT_EQ(SS_OK, result);
    EXPECT_EQ(payload, receivePayload);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockSendFailedWithInvalidFdBuf)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-11", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_OK, result);
    void *tmpBuf1 = nullptr;
    result = sock->Send(tmpBuf1, 0);
    EXPECT_EQ(SS_PARAM_INVALID, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockReceiveFailedWithInvalidFdBuf)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-12", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    sock->Initialize(sockWorkerOptions);
    std::string payload = "hello world";
    void *tmpBuf = const_cast<char *>(payload.c_str());
    sock->Send(tmpBuf, payload.length());
    void *receiveBuf1 = nullptr;
    result = sock->Receive(receiveBuf1, payload.length());
    EXPECT_EQ(SS_PARAM_INVALID, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockReceiveFailedWithInvalidSize2)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-14", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    sock->Initialize(sockWorkerOptions);
    std::string payload = "hello world";
    void *tmpBuf = const_cast<char *>(payload.c_str());
    sock->Send(tmpBuf, payload.length());
    auto receiveBuf = memalign(NN_NO1024, payload.length());
    MOCKER(::recv).defaults().will(returnValue(payload.length() - 1));
    SResult result = sock->Receive(receiveBuf, payload.length());
    EXPECT_EQ(SS_SOCK_DATA_SIZE_UN_MATCHED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostSendSglFailed)
{
    auto connFd = SockConnect(9982);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-15", newSockId, connFd, option);
    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    sock->Initialize(sockWorkerOptions);
    UBSHcomNetTransSglRequest req(iovPtr, 0, 0);

    UBSHcomNetTransHeader header {};
    header.immData = 1;
    header.seqNo = 1;
    header.flags = NTH_TWO_SIDE_SGL;
    header.dataLength = 0;
    for (uint16_t i = 0; i < req.iovCount; i++) {
        header.dataLength += req.iov[i].size;
    }

    result = sock->PostSendSgl(header, req);
    EXPECT_EQ(SS_OK, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostSendInvalidAddress)
{
    auto connFd = SockConnect(9983);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-16", newSockId, connFd, option);
    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    sock->Initialize(sockWorkerOptions);
    std::string payload = "hello world";
    static char data[1023] = {};
    UBSHcomNetTransRequest req(0, sizeof(data), 0);
    UBSHcomNetTransHeader header {};
    header.opCode = 1;
    header.seqNo = 1;
    header.flags = NTH_TWO_SIDE;
    header.dataLength = req.size;
    result = sock->PostSend(header, req);
    EXPECT_EQ(SS_SOCK_SEND_FAILED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostReceiveHeaderNormalReceive)
{
    static char data[1023] = {};
    UBSHcomNetTransRequest req(0, sizeof(data), 0);
    SockTransHeader header {};
    header.dataLength = req.size;
    auto connFd = SockConnect(9983);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "test", newSockId, connFd, option);
    MOCKER_CPP(setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(::recv).stubs().will(returnValue(sizeof(SockTransHeader)));
    MOCKER_CPP(NetFunc::ValidateHeader).stubs().will(returnValue(0));

    SResult result = sock->PostReceiveHeader(header, 1);
    EXPECT_EQ(SS_OK, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostReceiveHeaderMultipleReceives)
{
    static char data[1023] = {};
    UBSHcomNetTransRequest req(0, sizeof(data), 0);
    SockTransHeader header {};
    header.dataLength = req.size;
    auto connFd = SockConnect(9983);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "test", newSockId, connFd, option);
    MOCKER_CPP(setsockopt).stubs().will(returnValue(0));
    MOCKER_CPP(::recv).stubs()
        .will(returnValue(sizeof(SockTransHeader) / NN_NO2));
    MOCKER_CPP(NetFunc::ValidateHeader).stubs().will(returnValue(0));

    SResult result = sock->PostReceiveHeader(header, 1);
    EXPECT_EQ(SS_OK, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostReceiveBodyNormalReceive)
{
    void *buff = malloc(NN_NO1024);
    uint32_t dataLength = NN_NO1024;
    bool isOneSide = true;
    auto connFd = SockConnect(9983);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "test", newSockId, connFd, option);
    sock->mEnableTls = false;
    MOCKER_CPP(::recv).stubs().will(returnValue(dataLength));

    SResult result = sock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(SS_OK, result);
    free(buff);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostReceiveBodyMultipleReceives)
{
    void *buff = malloc(NN_NO1024);
    uint32_t dataLength = NN_NO1024;
    bool isOneSide = true;
    auto connFd = SockConnect(9983);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "test", newSockId, connFd, option);
    sock->mEnableTls = false;
    MOCKER_CPP(::recv).stubs().will(returnValue(dataLength / NN_NO2));

    SResult result = sock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(SS_OK, result);
    free(buff);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostReceiveBodyTlsNormalReceive)
{
    void *buff = malloc(NN_NO1024);
    uint32_t dataLength = NN_NO1024;
    bool isOneSide = false;
    auto connFd = SockConnect(9983);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "test", newSockId, connFd, option);
    sock->mEnableTls = true;
    MOCKER_CPP(HcomSsl::SslRead).stubs().will(returnValue(static_cast<int>(dataLength)));

    SResult result = sock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(SS_OK, result);
    free(buff);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostReceiveBodyTlsMultipleReceives)
{
    void *buff = malloc(NN_NO1024);
    uint32_t dataLength = NN_NO1024;
    bool isOneSide = false;
    auto connFd = SockConnect(9983);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "test", newSockId, connFd, option);
    sock->mEnableTls = true;
    MOCKER_CPP(HcomSsl::SslRead).stubs().will(returnValue(static_cast<int>(dataLength / NN_NO2)));

    SResult result = sock->PostReceiveBody(buff, dataLength, isOneSide);
    EXPECT_EQ(SS_OK, result);
    free(buff);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostReceiveHeaderSuccess)
{
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;

    UBSHcomNetDriverOptions sockOptions {};
    SetDriverOptions(sockOptions);
    NResult result;
    serverDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-server-9000", true);
    SetCB(serverDriver, true, 1);
    serverDriver->OobIpAndPort(BASE_IP, 9990);
    serverDriver->Initialize(sockOptions);
    serverDriver->Start();

    clientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-client-9000", false);
    clientDriver->OobIpAndPort(BASE_IP, 9990);
    sockOptions.dontStartWorkers = true;
    clientDriver->Initialize(sockOptions);
    clientDriver->Start();
    result = clientDriver->Connect("hello world", clientEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(SS_OK, result);

    static char data[100] = {};
    UBSHcomNetResponseContext respCtx {};
    UBSHcomNetTransRequest req((void *)(data), sizeof(data), 0);
    result = clientEp->PostSend(1, req);
    EXPECT_EQ(SS_OK, result);
    result = clientEp->Receive(2, respCtx);
    EXPECT_EQ(SS_OK, result);

    CloseDriver(serverDriver);
    CloseDriver(clientDriver);
}

TEST_F(TestSockWrapper, SockPostWriteSglSuccess)
{
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;

    UBSHcomNetDriverOptions sockOptions {};
    SetDriverOptions(sockOptions);
    NResult result;

    sem_init(&sem_sock, 0, 0);
    serverDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-server-9992", true);
    SetCB(serverDriver, true, 3);
    serverDriver->OobIpAndPort(BASE_IP, 9992);
    serverDriver->Initialize(sockOptions);
    result = serverDriver->Start();
    EXPECT_EQ(result, SS_OK);
    std::vector<UBSHcomNetMemoryRegionPtr> serverMrs;
    CreateServerMR(serverDriver, serverMrs);

    clientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-client-9992", false);
    SetCB(clientDriver, false, 2);
    clientDriver->OobIpAndPort(BASE_IP, 9992);
    clientDriver->Initialize(sockOptions);
    clientDriver->Start();
    clientDriver->Connect("hello world", clientEp, 0);
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    CreateClientMR(clientDriver, clientMrs);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if ((result = clientEp->PostSend(1, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
    }
    sem_wait(&sem_sock);

    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = localMrInfo[i].lAddress;
        iov[i].rAddress = remoteMrInfo[i].lAddress;
        iov[i].lKey = localMrInfo[i].lKey;
        iov[i].rKey = remoteMrInfo[i].lKey;
        iov[i].size = NN_NO16;
    }
    UBSHcomNetTransSglRequest reqRead(iov, NN_NO4, 0);
    result = clientEp->PostRead(reqRead);
    sem_wait(&sem_sock);
    EXPECT_EQ(SS_OK, result);
    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO4, 0);
    result = clientEp->PostWrite(reqWrite);
    sem_wait(&sem_sock);
    EXPECT_EQ(SS_OK, result);

    SockWrapperDestoryMem(serverDriver, serverMrs);
    SockWrapperDestoryMem(clientDriver, clientMrs);
    CloseDriver(serverDriver);
    CloseDriver(clientDriver);
}

TEST_F(TestSockWrapper, SockPostWriteSglFailedRead)
{
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;

    NResult result;
    UBSHcomNetDriverOptions sockOptions {};
    SetDriverOptions(sockOptions);
    sem_init(&sem_sock, 0, 0);

    serverDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-server-9993", true);
    SetCB(serverDriver, true, 3);
    serverDriver->OobIpAndPort(BASE_IP, 9993);
    serverDriver->Initialize(sockOptions);
    serverDriver->Start();
    std::vector<UBSHcomNetMemoryRegionPtr> serverMrs;
    CreateServerMR(serverDriver, serverMrs);

    clientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-client-9993", false);
    SetCB(clientDriver, false, 2);
    clientDriver->OobIpAndPort(BASE_IP, 9993);
    clientDriver->Initialize(sockOptions);
    clientDriver->Start();
    clientDriver->Connect("hello world", clientEp, 0);
    UBSHcomEpOptions epOptions {};
    epOptions.tcpBlockingIo = true;
    clientEp->SetEpOption(epOptions);
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    CreateClientMR(clientDriver, clientMrs);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if ((result = clientEp->PostSend(1, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
    }
    sem_wait(&sem_sock);

    ssize_t res = -1;
    MOCKER(writev).defaults().will(returnValue(res));

    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = localMrInfo[i].lAddress;
        iov[i].rAddress = remoteMrInfo[i].lAddress;
        iov[i].lKey = localMrInfo[i].lKey;
        iov[i].rKey = remoteMrInfo[i].lKey;
        iov[i].size = NN_NO16;
    }
    UBSHcomNetTransSglRequest reqRead(iov, NN_NO4, 0);
    result = clientEp->PostRead(reqRead);
    EXPECT_EQ(SS_TIMEOUT, result);

    SockWrapperDestoryMem(serverDriver, serverMrs);
    SockWrapperDestoryMem(clientDriver, clientMrs);
    CloseDriver(serverDriver);
    CloseDriver(clientDriver);
}

TEST_F(TestSockWrapper, SockInitializeFailedWithInvalidAliveTime)
{
    auto connFd = SockConnect(9984);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-17", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    sockWorkerOptions.keepaliveIdleTime = -1;
    sockWorkerOptions.keepaliveProbeInterval = -1;
    sockWorkerOptions.keepaliveProbeTimes = -1;
    SResult result;
    result = sock->Initialize(sockWorkerOptions);
    EXPECT_EQ(SS_TCP_SET_OPTION_FAILED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}

TEST_F(TestSockWrapper, SockPostWriteSglFailedWrite)
{
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetEndpointPtr clientEp = nullptr;

    NResult result;
    UBSHcomNetDriverOptions sockOptions {};
    SetDriverOptions(sockOptions);

    sem_init(&sem_sock, 0, 0);
    serverDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-server-9994", true);
    SetCB(serverDriver, true, 3);
    serverDriver->OobIpAndPort(BASE_IP, 9994);
    serverDriver->Initialize(sockOptions);
    serverDriver->Start();
    std::vector<UBSHcomNetMemoryRegionPtr> serverMrs;
    CreateServerMR(serverDriver, serverMrs);

    clientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, "sock-client-9994", false);
    SetCB(clientDriver, false, 2);
    clientDriver->OobIpAndPort(BASE_IP, 9994);
    clientDriver->Initialize(sockOptions);
    clientDriver->Start();
    clientDriver->Connect("hello world", clientEp, 0);
    UBSHcomEpOptions epOptions {};
    epOptions.tcpBlockingIo = true;
    clientEp->SetEpOption(epOptions);
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    CreateClientMR(clientDriver, clientMrs);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if ((result = clientEp->PostSend(1, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
    }
    sem_wait(&sem_sock);

    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = localMrInfo[i].lAddress;
        iov[i].rAddress = remoteMrInfo[i].lAddress;
        iov[i].lKey = localMrInfo[i].lKey;
        iov[i].rKey = remoteMrInfo[i].lKey;
        iov[i].size = NN_NO16;
    }
    UBSHcomNetTransSglRequest reqRead(iov, NN_NO4, 0);
    result = clientEp->PostRead(reqRead);
    sem_wait(&sem_sock);
    EXPECT_EQ(SS_OK, result);

    SockWrapperDestoryMem(serverDriver, serverMrs);
    SockWrapperDestoryMem(clientDriver, clientMrs);
    CloseDriver(serverDriver);
    CloseDriver(clientDriver);
}

TEST_F(TestSockWrapper, SockSendFail)
{
    auto connFd = SockConnect(9981);
    SockOptions option {};
    uint64_t newSockId = NetUuid::GenerateUuid();
    auto sock = new (std::nothrow) Sock(SockType::SOCK_TCP, "sock-18", newSockId, connFd, option);

    NetLocalAutoDecreasePtr<Sock> autoDecSock(sock);
    SockWorkerOptions sockWorkerOptions {};
    SResult result;
    sock->Initialize(sockWorkerOptions);
    std::string payload = "hello world";
    void *tmpBuf = const_cast<char *>(payload.c_str());
    ssize_t res = -1;
    MOCKER(::send).defaults().will(returnValue(res));
    result = sock->SendRealConnHeader(connFd, tmpBuf, payload.length());
    EXPECT_EQ(SS_SOCK_SEND_FAILED, result);
    NetFunc::NN_SafeCloseFd(connFd);
}
