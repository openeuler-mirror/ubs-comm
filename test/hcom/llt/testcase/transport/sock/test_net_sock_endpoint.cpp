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

#include "gtest/gtest.h"
#include "hcom.h"
#include "hcom_def.h"
#include "net_sock_common.h"
#include "test_net_sock_endpoint.h"

using namespace ock::hcom;
TestNetSockEndpoint::TestNetSockEndpoint() {}

#define BASE_IP "127.0.0.1"
#define IP_SEG "127.0.0.0/16"

struct TestRegMrInfo {
    uintptr_t lAddress = 0;
    uint32_t lKey = 0;
    uint32_t size = 0;
} __attribute__((packed));

static UBSHcomNetEndpointPtr serverEp = nullptr;
static sem_t sem;
std::string certPath1;

void TestNetSockEndpoint::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestNetSockEndpoint::TearDown()
{
    GlobalMockObject::verify();
}

/* callback functions */
static int NewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    serverEp = newEP;
    return 0;
}

static void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("end point " << ep->Id());
}

static int RequestReceived(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("client request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);
    sem_post(&sem);
    return 0;
}

static int RequestReceivedServer(const UBSHcomNetRequestContext &ctx)
{
    std::string respMsg = "Hello client, this is a reply message";

    int result = 0;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(respMsg.c_str())), respMsg.length(), 0);
    if ((result = serverEp->PostSend(1, req)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
    }
    return 0;
}

/* server new request sgl callback */
static TestRegMrInfo localMrInfo[NN_NO4];
static int RequestReceivedSglServer(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("server request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp((void *)(localMrInfo), sizeof(localMrInfo), 0);
    if ((result = serverEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }

    NN_LOG_INFO("request rsp Mr info");
    for (uint16_t i = 0; i < NN_NO4; i++) {
        NN_LOG_INFO("idx:" << i << " key:" << localMrInfo[i].lKey << " address:" << localMrInfo[i].lAddress <<
            " size: " << localMrInfo[i].size);
    }
    return 0;
}

/* client new request sgl callback */
static TestRegMrInfo remoteMrInfo[NN_NO4];
static int RequestReceivedSglClient(const UBSHcomNetRequestContext &ctx)
{
    memcpy(remoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen());
    NN_LOG_INFO("get remote Mr info");
    for (uint16_t i = 0; i < NN_NO4; i++) {
        NN_LOG_INFO("idx:" << i << " key:" << remoteMrInfo[i].lKey << " address:" << remoteMrInfo[i].lAddress <<
            " size" << remoteMrInfo[i].size);
    }

    sem_post(&sem);
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
    sem_post(&sem);
    return 0;
}

static bool RegSglMem(UBSHcomNetDriver *driver, TestRegMrInfo mrInfo[], std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    for (uint32_t i = 0; i < NN_NO4; ++i) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = driver->CreateMemoryRegion(NN_NO100, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        mrInfo[i].lAddress = mr->GetAddress();
        mrInfo[i].lKey = mr->GetLKey();
        mrInfo[i].size = NN_NO100;
        mrs.push_back(mr);
        memset(reinterpret_cast<void *>(mrInfo[i].lAddress), 0, mrInfo[i].size);
        NN_LOG_INFO(driver->Name() << ": lAddress = " << mrInfo[i].lAddress << ", lKey = " << mrInfo[i].lKey);
    }
    return true;
}

static void SockEpDestroyMem(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    while (!mrs.empty()) {
        driver->DestroyMemoryRegion(mrs.back());
        mrs.pop_back();
    }
}

static void Erase(void *pass, int len) {}

static int Verify(void *x509, const char *path)
{
    return 0;
}

static bool CertCallback(const std::string &name, std::string &value)
{
    value = certPath1 + "/server/cert.pem";
    return true;
}

static bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certPath1 + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certPath1 + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool CreateServerDriver(UBSHcomNetDriver *&driver, uint16_t port,
    int (*reqHandler)(const UBSHcomNetRequestContext &), bool enableTls, uint32_t segSize = 1024, uint16_t buffSize = 0)
{
    auto name = "server-" + std::to_string(port);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, name, true);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create serverDriver already created");
        return false;
    }

    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.SetNetDeviceIpMask(IP_SEG);
    options.enableTls = enableTls;
    options.mrSendReceiveSegCount = NN_NO10;
    options.mrSendReceiveSegSize = segSize;
    options.tcpSendBufSize = buffSize;
    options.tcpReceiveBufSize = buffSize;
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    driver->RegisterNewEPHandler(
        std::bind(&NewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(reqHandler);
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    if (enableTls) {
        driver->RegisterTLSCertificationCallback(
            std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
        driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    }

    driver->OobIpAndPort(BASE_IP, port);

    int result = 0;
    if ((result = driver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("serverDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start serverDriver " << result);
        return false;
    }
    NN_LOG_INFO("serverDriver started");
    return true;
}

static bool CreateClientDriver(UBSHcomNetDriver *&driver, uint16_t port,
    int (*reqHandler)(const UBSHcomNetRequestContext &), bool enableTls)
{
    auto name = "client-" + std::to_string(port);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, name, false);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create clientDriver already created");
        return false;
    }

    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.SetNetDeviceIpMask(IP_SEG);
    options.enableTls = enableTls;
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(reqHandler);
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    if (enableTls) {
        driver->RegisterTLSCertificationCallback(
            std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
        driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2,
            std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
        driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    }

    driver->OobIpAndPort(BASE_IP, port);

    int result = 0;
    if ((result = driver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start clientDriver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver started");
    return true;
}

static bool CreateClientDriverSync(UBSHcomNetDriver *&driver, uint16_t port, uint32_t segSize = 1024,
    uint16_t buffSize = 0)
{
    auto name = "client-" + std::to_string(port);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, name, false);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create clientDriver already created");
        return false;
    }
    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.mrSendReceiveSegCount = NN_NO10;
    options.mrSendReceiveSegSize = segSize;
    options.dontStartWorkers = true;
    options.tcpSendBufSize = buffSize;
    options.tcpReceiveBufSize = buffSize;
    options.enableTls = false;
    options.SetNetDeviceIpMask(IP_SEG);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    driver->OobIpAndPort(BASE_IP, port);

    int result = 0;
    if ((result = driver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start clientDriver " << result);
        return false;
    }
    NN_LOG_INFO("clientDriver started");
    return true;
}

void CloseDriver(UBSHcomNetDriver *&clientDriver, UBSHcomNetDriver *&serverDriver)
{
    std::string clientName = clientDriver->Name();
    std::string serverName = serverDriver->Name();
    if (clientDriver->IsStarted()) {
        clientDriver->Stop();
        clientDriver->UnInitialize();
    }
    if (serverDriver->IsStarted()) {
        serverDriver->Stop();
        serverDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(clientName);
    UBSHcomNetDriver::DestroyInstance(serverName);
}

TEST_F(TestNetSockEndpoint, PostSendRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    uint16_t port = 9911;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    CreateServerDriver(serverDriver, port, RequestReceived, false);
    CreateClientDriver(clientDriver, port, RequestReceived, false);

    clientDriver->Connect("hello server", ep, 0);

    ep->DefaultTimeout(1);
    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    auto peerIpAndPort = ep->PeerIpAndPort();
    NN_LOG_INFO(peerIpAndPort);
    EXPECT_EQ("127.0.0.1:9911", peerIpAndPort);

    result = ep->PostSend(1, req);
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetTransOpInfo innerOpInfo(0, 0, 0, NTH_TWO_SIDE);
    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SS_OK, result);

    MOCKER_CPP(&SockWorker::PostSend)
        .defaults()
        .will(returnObjectList(SS_TCP_RETRY, SS_OK, SS_TCP_RETRY, SS_OK));
    result = ep->PostSend(1, req);
    EXPECT_EQ(SS_ERROR, result);

    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SS_ERROR, result);

    result = ep->WaitCompletion();
    EXPECT_EQ(NN_INVALID_OPERATION, result);

    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(NN_INVALID_OPERATION, result);

    result = ep->ReceiveRaw(NN_NO2, respCtx);
    EXPECT_EQ(NN_INVALID_OPERATION, result);

    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, PostSendRawRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9912;
    CreateServerDriver(serverDriver, port, RequestReceived, false);
    CreateClientDriver(clientDriver, port, RequestReceived, false);

    clientDriver->Connect("hello world", ep, 0);
    ep->DefaultTimeout(1);
    std::string value = "sock ping pong client";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SS_OK, result);

    MOCKER_CPP(&SockWorker::PostSend).defaults().will(returnObjectList(SS_TCP_RETRY, SS_OK));
    result = ep->PostSendRaw(req, 0);
    EXPECT_EQ(SS_ERROR, result);

    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, PostSendRawSglRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9913;
    CreateServerDriver(serverDriver, port, RequestReceived, false);
    CreateClientDriver(clientDriver, port, RequestReceived, false);

    clientDriver->Connect("hello server", ep, 0);
    ep->DefaultTimeout(1);
    sem_init(&sem, 0, 0);

    TestRegMrInfo clientMrInfo[NN_NO4];
    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    bool res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = clientMrInfo[i].lAddress;
        iov[i].lKey = clientMrInfo[i].lKey;
        iov[i].size = clientMrInfo[i].size;
    }
    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);

    result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(SS_OK, result);
    sem_wait(&sem);

    MOCKER_CPP(&SockWorker::PostSendRawSgl).defaults().will(returnObjectList(SS_TCP_RETRY, SS_OK));
    result = ep->PostSendRawSgl(req, 0);
    EXPECT_EQ(SS_ERROR, result);

    SockEpDestroyMem(clientDriver, mrs);
    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, PostReadWriteRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9914;
    CreateServerDriver(serverDriver, port, RequestReceivedSglServer, false);
    CreateClientDriver(clientDriver, port, RequestReceivedSglClient, false);

    clientDriver->Connect("hello server", ep, 0);

    ep->DefaultTimeout(1);
    sem_init(&sem, 0, 0);

    TestRegMrInfo clientMrInfo[NN_NO4];
    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    res = RegSglMem(clientDriver, clientMrInfo, clientMrs);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> serverMrs;
    res = RegSglMem(serverDriver, localMrInfo, serverMrs);
    EXPECT_TRUE(res);

    std::string msg = "Transfer MrInfo of the server to the client.";
    UBSHcomNetTransRequest rsp((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, rsp);
    EXPECT_EQ(SS_OK, result);
    sem_wait(&sem);

    UBSHcomNetTransRequest req;
    req.lAddress = clientMrInfo[0].lAddress;
    req.rAddress = localMrInfo[0].lAddress;
    req.lKey = clientMrInfo[0].lKey;
    req.rKey = localMrInfo[0].lKey;
    req.size = localMrInfo[0].size;

    NN_LOG_INFO("++++++++++++rAddress = " << req.rAddress << ", value = " <<
        *(reinterpret_cast<uint64_t *>((void *)req.rAddress)));
    NN_LOG_INFO("++++++++++++lAddress = " << req.lAddress << ", value = " <<
        *(reinterpret_cast<uint64_t *>((void *)req.lAddress)));
    result = ep->PostRead(req);
    sem_wait(&sem);

    for (uint16_t i = 0; i < 1; i++) {
        uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(clientMrInfo[i].lAddress));
        NN_LOG_INFO("value[" << i << "]=" << *readValue);
    }
    EXPECT_EQ(SS_OK, result);

    result = ep->PostWrite(req);
    sem_wait(&sem);

    MOCKER_CPP(&SockWorker::PostRead, SResult(SockWorker::*)(Sock *, SockTransHeader &,
        const UBSHcomNetTransRequest &))
        .defaults()
        .will(returnObjectList(SS_TCP_RETRY, SS_OK));
    result = ep->PostRead(req);
    EXPECT_EQ(SS_ERROR, result);

    MOCKER_CPP(&SockWorker::PostWrite, SResult(SockWorker::*)(Sock *, SockTransHeader &,
        const UBSHcomNetTransRequest &))
        .defaults()
        .will(returnObjectList(SS_TCP_RETRY, SS_OK));
    result = ep->PostWrite(req);
    EXPECT_EQ(SS_ERROR, result);

    SockEpDestroyMem(clientDriver, clientMrs);
    SockEpDestroyMem(serverDriver, serverMrs);
    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, PostReadWriteSglRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9915;
    CreateServerDriver(serverDriver, port, RequestReceivedSglServer, false);
    CreateClientDriver(clientDriver, port, RequestReceivedSglClient, false);

    clientDriver->Connect("hello server", ep, 0);
    ep->DefaultTimeout(1);
    sem_init(&sem, 0, 0);

    TestRegMrInfo clientMrInfo[NN_NO4];
    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    res = RegSglMem(clientDriver, clientMrInfo, clientMrs);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> serverMrs;
    res = RegSglMem(serverDriver, localMrInfo, serverMrs);
    EXPECT_TRUE(res);

    std::string msg = "Transfer MrInfo of the server to the client.";
    UBSHcomNetTransRequest rsp((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, rsp);
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = clientMrInfo[i].lAddress;
        iov[i].rAddress = localMrInfo[i].lAddress;
        iov[i].lKey = clientMrInfo[i].lKey;
        iov[i].rKey = localMrInfo[i].lKey;
        iov[i].size = localMrInfo[i].size;
    }
    UBSHcomNetTransSglRequest reqRead(iov, NN_NO4, 0);
    result = ep->PostRead(reqRead);
    sem_wait(&sem);

    for (uint16_t i = 0; i < NN_NO4; i++) {
        uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(clientMrInfo[i].lAddress));
        uint64_t value = *readValue;
        NN_LOG_INFO("value[" << i << "]=" << *readValue);
        *readValue = ++value;
    }
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO4, 0);
    result = ep->PostWrite(reqWrite);
    sem_wait(&sem);

    MOCKER_CPP(&SockWorker::PostRead, SResult(SockWorker::*)(Sock *, SockTransHeader &,
        const UBSHcomNetTransSglRequest &))
        .defaults()
        .will(returnObjectList(SS_TCP_RETRY, SS_OK));
    result = ep->PostRead(reqRead);
    EXPECT_EQ(SS_ERROR, result);

    MOCKER_CPP(&SockWorker::PostWrite, SResult(SockWorker::*)(Sock *, SockTransHeader &,
        const UBSHcomNetTransSglRequest &))
        .defaults()
        .will(returnObjectList(SS_TCP_RETRY, SS_OK));
    result = ep->PostWrite(reqRead);
    EXPECT_EQ(SS_ERROR, result);

    SockEpDestroyMem(clientDriver, clientMrs);
    SockEpDestroyMem(serverDriver, serverMrs);
    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, SyncPostSendRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9916;
    bool res = CreateServerDriver(serverDriver, port, RequestReceivedServer, false);
    EXPECT_TRUE(res);

    res = CreateClientDriverSync(clientDriver, port);
    EXPECT_TRUE(res);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(1);
    auto peerIpAndPort = ep->PeerIpAndPort();
    NN_LOG_INFO(peerIpAndPort);
    EXPECT_EQ("127.0.0.1:9916", peerIpAndPort);

    std::string msg = "Hello server, this is a message";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, req);
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    std::string resp((char *)respCtx.Message()->Data(), respCtx.Header().dataLength);
    NN_LOG_INFO("server response received - " << respCtx.Header().opCode << ", dataLen " <<
        respCtx.Header().dataLength);
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetTransOpInfo innerOpInfo(0, 0, 0, NTH_TWO_SIDE);
    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SS_OK, result);

    MOCKER_CPP(&Sock::PostSend, SResult(Sock::*)(SockTransHeader &, const UBSHcomNetTransRequest &))
        .defaults()
        .will(returnObjectList(SS_TCP_RETRY, SS_OK, SS_TCP_RETRY, SS_OK));
    result = ep->PostSend(NN_NO3, req);
    EXPECT_EQ(SS_ERROR, result);

    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SS_ERROR, result);

    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, SyncReceiveRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;

    uint16_t port = 9917;
    bool res = CreateServerDriver(serverDriver, port, RequestReceived, false);
    EXPECT_TRUE(res);

    res = CreateClientDriverSync(clientDriver, port);
    EXPECT_TRUE(res);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);

    UBSHcomNetResponseContext respCtx {};

    MOCKER_CPP(&Sock::PostReceiveHeader).defaults().will(returnObjectList(SS_OK, 0, 0));
    result = ep->Receive(NN_NO4, respCtx);
    EXPECT_EQ(SS_ERROR, result);

    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed).defaults().will(returnObjectList(false, true));
    result = ep->Receive(NN_NO6, respCtx);
    EXPECT_EQ(NN_MALLOC_FAILED, result);

    MOCKER_CPP(&Sock::PostReceiveBody).defaults().will(returnValue(SS_OK));
    result = ep->Receive(NN_NO4, respCtx);
    EXPECT_EQ(SS_ERROR, result);

    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, SyncPostSendRawRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9918;
    bool res = CreateServerDriver(serverDriver, port, RequestReceivedServer, false);
    EXPECT_TRUE(res);
    res = CreateClientDriverSync(clientDriver, port);
    EXPECT_TRUE(res);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(1);
    std::string msg = "Hello server, this is a message";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetResponseContext respCtx {};
    result = ep->ReceiveRaw(-1, respCtx);
    std::string resp((char *)respCtx.Message()->Data(), respCtx.Header().dataLength);
    NN_LOG_INFO("server response received - " << respCtx.Header().opCode << ", dataLen " <<
        respCtx.Header().dataLength);
    EXPECT_EQ(SS_OK, result);

    MOCKER_CPP(&Sock::PostSend, SResult(Sock::*)(SockTransHeader &, const UBSHcomNetTransRequest &))
        .defaults()
        .will(returnObjectList(413, 400));
    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SS_ERROR, result);

    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, SyncReceiveRawRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9919;
    bool res = CreateServerDriver(serverDriver, port, RequestReceivedServer, false);
    EXPECT_TRUE(res);
    res = CreateClientDriverSync(clientDriver, port);
    EXPECT_TRUE(res);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);

    std::string msg = "Hello server, this is a message";
    UBSHcomNetResponseContext respCtx {};

    MOCKER_CPP(&Sock::PostReceiveHeader).defaults().will(returnObjectList(SS_OK, 0, 0));
    result = ep->ReceiveRaw(NN_NO4, respCtx);
    EXPECT_EQ(SS_ERROR, result);

    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed).defaults().will(returnObjectList(false, true));
    result = ep->ReceiveRaw(NN_NO6, respCtx);
    EXPECT_EQ(NN_MALLOC_FAILED, result);

    MOCKER_CPP(&Sock::PostReceiveBody).defaults().will(returnValue(SS_OK));
    result = ep->ReceiveRaw(NN_NO4, respCtx);
    EXPECT_EQ(SS_ERROR, result);

    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, SyncPostSendRawSglRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9920;
    bool createRes = CreateServerDriver(serverDriver, port, RequestReceivedServer, false);
    EXPECT_TRUE(createRes);
    createRes = CreateClientDriverSync(clientDriver, port);
    EXPECT_TRUE(createRes);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(1);
    sem_init(&sem, 0, 0);

    TestRegMrInfo clientMrInfo[NN_NO4];
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    bool res = RegSglMem(clientDriver, clientMrInfo, clientMrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = clientMrInfo[i].lAddress;
        iov[i].lKey = clientMrInfo[i].lKey;
        iov[i].size = clientMrInfo[i].size;
    }

    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);
    UBSHcomNetResponseContext respCtx {};

    result = ep->PostSendRawSgl(req, 0);
    EXPECT_EQ(SS_OK, result);
    result = ep->ReceiveRawSgl(respCtx);
    EXPECT_EQ(SS_OK, result);

    MOCKER_CPP(&Sock::PostSendSgl, SResult(Sock::*)(SockTransHeader &, const UBSHcomNetTransSglRequest &))
        .defaults()
        .will(returnObjectList(SS_TCP_RETRY, SS_OK));
    result = ep->PostSendRawSgl(req, 0);
    EXPECT_EQ(SS_ERROR, result);

    SockEpDestroyMem(clientDriver, clientMrs);
    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, SyncPostReadWriteRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9921;
    bool createRes = CreateServerDriver(serverDriver, port, RequestReceivedSglServer, false);
    EXPECT_TRUE(createRes);
    createRes = CreateClientDriverSync(clientDriver, port);
    EXPECT_TRUE(createRes);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);

    TestRegMrInfo clientMrInfo[NN_NO4];
    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    res = RegSglMem(clientDriver, clientMrInfo, clientMrs);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> serverMrs;
    res = RegSglMem(serverDriver, localMrInfo, serverMrs);
    EXPECT_TRUE(res);

    std::string msg = "Transfer MrInfo of the server to the client.";
    UBSHcomNetTransRequest rsp((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, rsp);
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(respCtx);
    EXPECT_EQ(SS_OK, result);

    memcpy(remoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransRequest req;
    req.lAddress = clientMrInfo[0].lAddress;
    req.rAddress = localMrInfo[0].lAddress;
    req.lKey = clientMrInfo[0].lKey;
    req.rKey = localMrInfo[0].lKey;
    req.size = localMrInfo[0].size;

    result = ep->PostRead(req);
    EXPECT_EQ(SS_OK, result);

    result = ep->WaitCompletion();
    EXPECT_EQ(SS_OK, result);

    for (uint16_t i = 0; i < 1; i++) {
        uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(clientMrInfo[i].lAddress));
        NN_LOG_INFO("value[" << i << "]=" << *readValue);
    }

    result = ep->PostWrite(req);
    EXPECT_EQ(SS_OK, result);

    result = ep->WaitCompletion();
    EXPECT_EQ(SS_OK, result);

    MOCKER_CPP(&Sock::PostRead, SResult(Sock::*)(SockOpContextInfo *)).defaults().will(returnObjectList(401));
    result = ep->PostRead(req);
    EXPECT_EQ(SS_PARAM_INVALID, result);

    MOCKER_CPP(&Sock::PostWrite, SResult(Sock::*)(SockOpContextInfo *)).defaults().will(returnObjectList(401));
    result = ep->PostWrite(req);
    EXPECT_EQ(SS_PARAM_INVALID, result);

    SockEpDestroyMem(clientDriver, clientMrs);
    SockEpDestroyMem(serverDriver, serverMrs);
    CloseDriver(clientDriver, serverDriver);
}

TEST_F(TestNetSockEndpoint, SyncPostReadWriteSglRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9922;
    bool createRes = CreateServerDriver(serverDriver, port, RequestReceivedSglServer, false);
    EXPECT_TRUE(createRes);
    createRes = CreateClientDriverSync(clientDriver, port);
    EXPECT_TRUE(createRes);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);

    TestRegMrInfo clientMrInfo[NN_NO4];
    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> clientMrs;
    res = RegSglMem(clientDriver, clientMrInfo, clientMrs);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> serverMrs;
    res = RegSglMem(serverDriver, localMrInfo, serverMrs);
    EXPECT_TRUE(res);

    std::string msg = "Transfer MrInfo of the server to the client.";
    UBSHcomNetTransRequest rsp((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, rsp);
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(respCtx);
    EXPECT_EQ(SS_OK, result);

    memcpy(localMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = clientMrInfo[i].lAddress;
        iov[i].rAddress = localMrInfo[i].lAddress;
        iov[i].lKey = clientMrInfo[i].lKey;
        iov[i].rKey = localMrInfo[i].lKey;
        iov[i].size = localMrInfo[i].size;
        NN_LOG_INFO("idx:" << i << " lKey:" << iov[i].lKey << " lAddress:" << iov[i].lAddress << " rKey:" <<
            iov[i].rKey << " rAddress:" << iov[i].rAddress << " size:" << iov[i].size);
    }
    UBSHcomNetTransSglRequest reqRead(iov, NN_NO4, 0);
    result = ep->PostRead(reqRead);
    EXPECT_EQ(SS_OK, result);

    result = ep->WaitCompletion();
    EXPECT_EQ(SS_OK, result);

    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO4, 0);
    result = ep->PostWrite(reqWrite);
    EXPECT_EQ(SS_OK, result);

    result = ep->WaitCompletion();
    EXPECT_EQ(SS_OK, result);

    MOCKER_CPP(&Sock::PostReadSgl, SResult(Sock::*)(SockOpContextInfo *)).defaults().will(returnObjectList(401));
    result = ep->PostRead(reqRead);
    EXPECT_EQ(SS_PARAM_INVALID, result);

    MOCKER_CPP(&Sock::PostWriteSgl, SResult(Sock::*)(SockOpContextInfo *)).defaults().will(returnObjectList(401));
    result = ep->PostWrite(reqWrite);
    EXPECT_EQ(SS_PARAM_INVALID, result);

    SockEpDestroyMem(clientDriver, clientMrs);
    SockEpDestroyMem(serverDriver, serverMrs);
    CloseDriver(clientDriver, serverDriver);
}

int SockValidateTlsCert()
{
    char *buffer;
    if ((buffer = getcwd(NULL, 0)) == NULL) {
        NN_LOG_ERROR("Cet path for TLS cert failed");
        return -1;
    }

    std::string currentPath = buffer;
    certPath1 = currentPath + "/../test/opensslcrt/normalCert1";

    if (!CanonicalPath(certPath1)) {
        NN_LOG_ERROR("TLS cert path check failed " << certPath1);
        return -1;
    }

    return 0;
}

TEST_F(TestNetSockEndpoint, EpEncrypt)
{
    SockValidateTlsCert();
    UBSHcomNetEndpointPtr ep = nullptr;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9923;
    CreateServerDriver(serverDriver, port, RequestReceivedSglServer, true);
    CreateClientDriver(clientDriver, port, RequestReceivedSglClient, true);

    auto ret = clientDriver->Connect("hello server", ep, 0);
    ASSERT_EQ(ret, NN_OK);
    std::string value = "EpEncrypt";
    uint64_t encryptLen = ep->EstimatedEncryptLen(value.length());
    void *cipher = malloc(encryptLen);
    ep->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    uint64_t decryptLen = ep->EstimatedDecryptLen(encryptLen);
    void *rawData = malloc(decryptLen);
    ep->Decrypt(cipher, encryptLen, rawData, decryptLen);
    auto result = memcmp(value.c_str(), reinterpret_cast<char *>(rawData), value.length());
    EXPECT_EQ(0, result);
    ep->Close();

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);
    encryptLen = ep->EstimatedEncryptLen(value.length());
    cipher = malloc(encryptLen);
    ep->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    decryptLen = ep->EstimatedDecryptLen(encryptLen);
    rawData = malloc(decryptLen);
    ep->Decrypt(cipher, encryptLen, rawData, decryptLen);
    result = memcmp(value.c_str(), reinterpret_cast<char *>(rawData), value.length());
    EXPECT_EQ(0, result);
    ep->Close();

    clientDriver = nullptr;
    serverDriver = nullptr;
    port = 9924;
    CreateServerDriver(serverDriver, port, RequestReceivedSglServer, false);
    CreateClientDriver(clientDriver, port, RequestReceivedSglClient, false);

    clientDriver->Connect("hello server", ep, 0);
    value = "EpEncrypt";
    encryptLen = ep->EstimatedEncryptLen(0);
    EXPECT_EQ(0, encryptLen);
    encryptLen = ep->EstimatedEncryptLen(value.length());
    EXPECT_EQ(0, encryptLen);
    cipher = malloc(encryptLen);
    auto res = ep->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    EXPECT_EQ(NN_ERROR, res);
    decryptLen = ep->EstimatedDecryptLen(encryptLen);
    EXPECT_EQ(0, decryptLen);
    rawData = malloc(decryptLen);
    res = ep->Decrypt(cipher, encryptLen, rawData, decryptLen);
    EXPECT_EQ(NN_ERROR, res);
    ep->Close();

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);
    value = "EpEncrypt";
    encryptLen = ep->EstimatedEncryptLen(0);
    EXPECT_EQ(0, encryptLen);
    encryptLen = ep->EstimatedEncryptLen(value.length());
    EXPECT_EQ(0, encryptLen);
    cipher = malloc(encryptLen);
    res = ep->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    EXPECT_EQ(NN_ERROR, res);
    decryptLen = ep->EstimatedDecryptLen(encryptLen);
    EXPECT_EQ(0, decryptLen);
    rawData = malloc(decryptLen);
    res = ep->Decrypt(cipher, encryptLen, rawData, decryptLen);
    EXPECT_EQ(NN_ERROR, res);
    ep->Close();
}

TEST_F(TestNetSockEndpoint, SyncPostSendTimeout)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;

    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    uint16_t port = 9925;
    bool res;
    uint32_t segSize = 3 * 1024 * 1024;
    res = CreateServerDriver(serverDriver, port, RequestReceivedServer, false, segSize, 1);
    EXPECT_TRUE(res);
    res = CreateClientDriverSync(clientDriver, port, segSize, 1);
    EXPECT_TRUE(res);
    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);
    UBSHcomNetResponseContext respCtx{};
    static char data[20] = "sock_pp_client";
    UBSHcomNetTransRequest req((void *)(data), sizeof(data), 0);
    UBSHcomEpOptions epOptions{};

    epOptions.sendTimeout = -1;
    ep->SetEpOption(epOptions);
    result = ep->PostSend(1, req);
    EXPECT_EQ(SS_OK, result);

    result = ep->Receive(-1, respCtx);
    EXPECT_EQ(SS_OK, result);
    uint32_t size1 = 2 * 1024 * 1024;
    static char data1[size1] = "sock_pp_client";
    UBSHcomNetTransRequest req1((void *)(data1), sizeof(data1), 0);

    epOptions.sendTimeout = 0;
    ep->SetEpOption(epOptions);
    result = ep->PostSend(1, req1);
    EXPECT_EQ(SS_TIMEOUT, result);

    clientDriver->Connect("hello server", ep, NET_EP_SELF_POLLING);
    result = ep->Receive(0, respCtx);
    EXPECT_EQ(SS_TIMEOUT, result);

    CloseDriver(clientDriver, serverDriver);
}