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

#include <fstream>
#include <sys/epoll.h>

#include "hcom.h"
#include "mockcpp/mockcpp.hpp"
#include "sock_common.h"
#include "ut_helper.h"
#include "test_net_sock_driver_oob.h"

using namespace ock::hcom;

TestNetSockDriverOob::TestNetSockDriverOob() {}

UBSHcomNetEndpointPtr ep = nullptr;
UBSHcomNetDriverOptions sockOptions {};
static int g_nameSeed = 159753;
static int port = 9031;
UBSHcomNetDriver *sockServerDriver;
UBSHcomNetDriver *sockClientDriver;
UBSHcomNetTransSgeIov iovPtrServer[4];
UBSHcomNetTransSgeIov iovPtrClient[4];
std::string certificatePath;

int sockOobNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    ep = newEP;
    return 0;
}

void sockOobEndPointBroken(const UBSHcomNetEndpointPtr &brokenEp)
{
    NN_LOG_INFO("end point " << brokenEp->Id());
    if (ep != nullptr) {
        ep.Set(nullptr);
    }
}

int sockOobRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);
    return 0;
}

int sockOobRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted");
    return 0;
}

int sockOobRequestPostedFail(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted fail");
    return -1;
}

int sockOobOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done");
    return 0;
}

int sockOobOneSideDoneFail(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done fail");
    return -1;
}

void Idle(const UBSHcomNetWorkerIndex &workerIndex) {}

static void Erase(void *pass, int len) {}

static int Verify(void *x509, const char *path)
{
    return 0;
}

int SockOobValidateTlsCert()
{
    char *buffer;
    if ((buffer = getcwd(NULL, 0)) == NULL) {
        NN_LOG_ERROR("Cet path for TLS cert failed");
        return -1;
    }

    std::string currentPath = buffer;
    certificatePath = currentPath + "/../test/opensslcrt/normalCert1";

    if (!CanonicalPath(certificatePath)) {
        NN_LOG_ERROR("TLS cert path check failed " << certificatePath);
        return -1;
    }

    return 0;
}

static bool CertCallback(const std::string &name, std::string &value)
{
    value = certificatePath + "/server/cert.pem";
    return true;
}

static bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = certificatePath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = certificatePath + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    return true;
}

void SetCB(UBSHcomNetDriver *driver)
{
    driver->RegisterNewEPHandler(
        std::bind(&sockOobNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterEPBrokenHandler(std::bind(&sockOobEndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(std::bind(&sockOobRequestReceived, std::placeholders::_1));
    driver->RegisterReqPostedHandler(std::bind(&sockOobRequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&sockOobOneSideDone, std::placeholders::_1));

    driver->RegisterTLSCertificationCallback(std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
    driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
}

bool RegisterMemory(UBSHcomNetDriver *driver, UBSHcomNetTransSgeIov iovs[])
{
    for (int i = 0; i < NN_NO4; i++) {
        auto &iov = iovs[i];
        UBSHcomNetMemoryRegionPtr mr;
        auto result = driver->CreateMemoryRegion(NN_NO8, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        iov.lAddress = mr->GetAddress();
        iov.lKey = mr->GetLKey();
        iov.size = NN_NO8;
        memset(reinterpret_cast<void *>(iov.lAddress), 0, iov.size);
    }
    return true;
}

bool RegisterMemoryWithAddress(UBSHcomNetDriver *driver, UBSHcomNetTransSgeIov iovs[])
{
    for (int i = 0; i < NN_NO16; i++) {
        auto &iov = iovs[i];
        UBSHcomNetMemoryRegionPtr mr;
        auto tmpBuf = memalign(1024, NN_NO8);
        auto result = driver->CreateMemoryRegion(reinterpret_cast<uintptr_t>(tmpBuf), NN_NO8, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        iov.lAddress = mr->GetAddress();
        iov.lKey = mr->GetLKey();
        iov.size = NN_NO8;
        memset(reinterpret_cast<void *>(iov.lAddress), 0, iov.size);
    }
    return true;
}

void TestNetSockDriverOob::SetUp()
{
    MOCK_VERSION
    SockOobValidateTlsCert();
    sockOptions.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    sockOptions.SetNetDeviceIpMask(IP_SEG);
    sockOptions.pollingBatchSize = NN_NO16;
    sockOptions.SetWorkerGroups("1");
    sockOptions.SetWorkerGroupsCpuSet("1-1");
    sockOptions.enableTls = false;
    sockOptions.dontStartWorkers = false;
    sockOptions.magic = NN_NO256;
    sockOptions.oobType = ock::hcom::NET_OOB_TCP;
    sockServerDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, std::to_string(g_nameSeed++), true);
    sockClientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP, std::to_string(g_nameSeed++), false);
    sockServerDriver->OobIpAndPort(BASE_IP, port);
    sockClientDriver->OobIpAndPort(BASE_IP, port++);
    SetCB(sockServerDriver);
    SetCB(sockClientDriver);
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestNetSockDriverOob::TearDown()
{
    std::string clientName = sockClientDriver->Name();
    std::string serverName = sockServerDriver->Name();
    if (sockServerDriver->IsStarted()) {
        sockServerDriver->Stop();
    }
    if (sockServerDriver->IsInited()) {
        sockServerDriver->UnInitialize();
    }
    if (sockClientDriver->IsStarted()) {
        sockClientDriver->Stop();
    }
    if (sockClientDriver->IsInited()) {
        sockClientDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(clientName);
    UBSHcomNetDriver::DestroyInstance(serverName);
    GlobalMockObject::verify();
}

TEST_F(TestNetSockDriverOob, InitSuccess)
{
    int testWorkerThreadPriority1 = -21;
    int testWorkerThreadPriority2 = -21;
    sockOptions.workerThreadPriority = testWorkerThreadPriority1;
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
    sockOptions.workerThreadPriority = testWorkerThreadPriority2;
    result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
    sockOptions.workerThreadPriority = 1;
    result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
    sockServerDriver->UnInitialize();
}

TEST_F(TestNetSockDriverOob, InitSuccessTwice)
{
    sockServerDriver->Initialize(sockOptions);
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, InitFailWithEmptyIp)
{
    sockOptions.SetNetDeviceIpMask("");
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_INVALID_IP, result);
}

TEST_F(TestNetSockDriverOob, InitSuccessWithBusyPolling)
{
    sockOptions.mode = UBSHcomNetDriverWorkingMode::NET_BUSY_POLLING;
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, InitSuccessWithTLS)
{
    sockOptions.enableTls = true;
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, InitSuccessWithoutSetWorkGroup)
{
    sockOptions.SetWorkerGroups("");
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, InitFailWithWorkGroupHasZeroWorker)
{
    sockOptions.SetWorkerGroups("0");
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetSockDriverOob, InitFailWithoutSetListeningIpAndPort)
{
    sockOptions.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    sockOptions.SetNetDeviceIpMask(IP_SEG);
    sockOptions.pollingBatchSize = NN_NO16;
    sockOptions.SetWorkerGroups("1");
    sockOptions.SetWorkerGroupsCpuSet("1-1");
    sockOptions.enableTls = false;
    sockOptions.dontStartWorkers = false;
    sockOptions.magic = NN_NO256;
    UBSHcomNetDriver *netDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::TCP,
        std::to_string(g_nameSeed++), true);
    SetCB(netDriver);
    NResult result = netDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetSockDriverOob, InitFailWithFailToInitWorker)
{
    MOCKER(epoll_create).defaults().will(returnValue(-1));
    NResult result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_NEW_OBJECT_FAILED, result);
}

TEST_F(TestNetSockDriverOob, InitFailWithTLSCipherSuiteUnknown)
{
    sockOptions.enableTls = true;
    sockOptions.cipherSuite = ock::hcom::UBSHcomNetCipherSuite(NN_NO4);
    auto result = sockServerDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
    result = sockClientDriver->Initialize(sockOptions);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
    sockOptions.cipherSuite = ock::hcom::AES_GCM_128;
}

TEST_F(TestNetSockDriverOob, StartSuccessWithIdleHandler)
{
    sockServerDriver->RegisterIdleHandler(std::bind(&Idle, std::placeholders::_1));
    sockServerDriver->Initialize(sockOptions);
    NResult result = sockServerDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, StartFailWithStartOobServerFail)
{
    sockServerDriver->Initialize(sockOptions);
    MOCKER(::socket).defaults().will(returnValue(-1));
    NResult result = sockServerDriver->Start();
    EXPECT_EQ(NNCode::NN_OOB_LISTEN_SOCKET_ERROR, result);
}

TEST_F(TestNetSockDriverOob, StartSuccessWithDontStartWorker)
{
    sockOptions.dontStartWorkers = true;
    sockServerDriver->Initialize(sockOptions);
    NResult result = sockServerDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, StartFailWithoutInit)
{
    NResult result = sockServerDriver->Start();
    EXPECT_EQ(NNCode::NN_ERROR, result);
}

TEST_F(TestNetSockDriverOob, StartSuccessTwice)
{
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    NResult result = sockServerDriver->Start();
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionSuccess)
{
    sockServerDriver->Initialize(sockOptions);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = sockServerDriver->CreateMemoryRegion(16, mr);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionFailWithoutInit)
{
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = sockServerDriver->CreateMemoryRegion(16, mr);
    EXPECT_EQ(NNCode::NN_NOT_INITIALIZED, result);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionFail)
{
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = sockServerDriver->CreateMemoryRegion(0, mr);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionWithAddressSuccess)
{
    sockServerDriver->Initialize(sockOptions);
    auto tmpBuf = memalign(NN_NO4096, 10);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = sockServerDriver->CreateMemoryRegion(reinterpret_cast<uintptr_t>(tmpBuf), 16, mr);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionWithAddressFailWithoutInit)
{
    auto tmpBuf = memalign(NN_NO4096, 10);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = sockServerDriver->CreateMemoryRegion(reinterpret_cast<uintptr_t>(tmpBuf), 16, mr);
    EXPECT_EQ(NNCode::NN_NOT_INITIALIZED, result);
}

TEST_F(TestNetSockDriverOob, CreateMemoryRegionWithAddressFailWithAddressIsZero)
{
    sockServerDriver->Initialize(sockOptions);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = sockServerDriver->CreateMemoryRegion(0, 16, mr);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetSockDriverOob, ConnectSuccess)
{
    sockOptions.tcpUserTimeout = 0;
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_OK, result);
}


TEST_F(TestNetSockDriverOob, ConnectUdsSuccess)
{
    const char *testFile = "hcom-server1";
    std::ofstream file(testFile);
    file.close();
    sockOptions.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    sockOptions.SetNetDeviceIpMask(IP_SEG);
    sockOptions.pollingBatchSize = NN_NO16;
    sockOptions.SetWorkerGroups("1");
    sockOptions.SetWorkerGroupsCpuSet("1-1");
    sockOptions.enableTls = false;
    sockOptions.dontStartWorkers = false;
    sockOptions.magic = NN_NO256;
    sockOptions.oobType = ock::hcom::NET_OOB_UDS;
    sockServerDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::UDS, std::to_string(g_nameSeed++), true);
    sockClientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::UDS, std::to_string(g_nameSeed++), false);
    UBSHcomNetOobUDSListenerOptions opt {};
    opt.Name("hcom-server1");
    opt.perm = 0;
    sockServerDriver->AddOobUdsOptions(opt);
    sockClientDriver->OobUdsName("hcom-server1");
    SetCB(sockServerDriver);
    SetCB(sockClientDriver);
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_OK, result);
    remove(testFile);
}

TEST_F(TestNetSockDriverOob, ConnectSuccessWithTLS)
{
    sockOptions.enableTls = true;
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, ConnectSuccessWithTLSCipherSuite256)
{
    sockOptions.enableTls = true;
    sockOptions.cipherSuite = ock::hcom::AES_GCM_256;
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, ConnectFailWithoutInit)
{
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_NOT_INITIALIZED, result);
}

TEST_F(TestNetSockDriverOob, ConnectFailWithPayloadOversize)
{
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    char payload[1030];
    uint16_t index = 1029;
    for (char &i : payload) {
        i = '1';
    }
    payload[index] = '\0';
    NResult result = sockClientDriver->Connect(payload, ep, 0);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestNetSockDriverOob, ConnectFail)
{
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    MOCKER(::connect).defaults().will(returnValue(-1));
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_OOB_CLIENT_SOCKET_ERROR, result);
}

TEST_F(TestNetSockDriverOob, ConnectFailWithMagicMismatch)
{
    uint16_t testMagic = 104;
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockOptions.magic = testMagic;
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_CONNECT_REFUSED, result);
}

TEST_F(TestNetSockDriverOob, ConnectFailWithFailToInitSock)
{
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    MOCKER(setsockopt).defaults().will(returnValue(-1));
    NResult result = sockClientDriver->Connect("hello world", ep, 0);
    EXPECT_EQ(NNCode::NN_NEW_OBJECT_FAILED, result);
}

TEST_F(TestNetSockDriverOob, ConnectSyncFailWithUninit)
{
    sockOptions.enableTls = true;
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, ConnectSyncFailWithFailToInitSock)
{
    sockOptions.enableTls = true;
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    MOCKER(setsockopt).defaults().will(returnValue(-1));
    NResult result = sockClientDriver->Connect("hello world", ep, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_NEW_OBJECT_FAILED, result);
}

TEST_F(TestNetSockDriverOob, ConnectSyncSuccess)
{
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, ConnectSyncFail)
{
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    MOCKER(::connect).defaults().will(returnValue(-1));
    NResult result = sockClientDriver->Connect("hello world", ep, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_OOB_CLIENT_SOCKET_ERROR, result);
}

TEST_F(TestNetSockDriverOob, ConnectSyncFailWithMagicMismatch)
{
    uint16_t testMagic = 104;
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockOptions.magic = testMagic;
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    NResult result = sockClientDriver->Connect("hello world", ep, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_CONNECT_REFUSED, result);
}

TEST_F(TestNetSockDriverOob, SendSuccess)
{
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    sockClientDriver->Connect("hello world", ep, 0);
    static char data[100] = {};
    UBSHcomNetTransRequest req((void *)(data), sizeof(data), 0);
    req.upCtxSize = NN_NO16;
    for (auto i = 0; i < NN_NO16; i++) {
        req.upCtxData[i] = 'a';
    }
    NResult result = ep->PostSend(1, req);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, ReadWriteSuccess)
{
    sockServerDriver->RegisterOneSideDoneHandler(std::bind(&sockOobOneSideDoneFail, std::placeholders::_1));
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    sockClientDriver->Connect("hello world", ep, 0);
    RegisterMemory(sockServerDriver, iovPtrServer);
    RegisterMemory(sockClientDriver, iovPtrClient);
    UBSHcomNetTransRequest req;
    req.lAddress = iovPtrClient[0].lAddress;
    req.rAddress = iovPtrServer[0].lAddress;
    req.lKey = iovPtrClient[0].lKey;
    req.rKey = iovPtrServer[0].lKey;
    req.size = NN_NO4;
    req.upCtxSize = NN_NO16;
    for (uint32_t i = 0; i < NN_NO16; i++) {
        req.upCtxData[i] = 'a';
    }
    NResult result = ep->PostRead(req);
    EXPECT_EQ(NNCode::NN_OK, result);
    result = ep->PostWrite(req);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, ReadWriteSglSuccess)
{
    sockServerDriver->RegisterOneSideDoneHandler(std::bind(&sockOobOneSideDoneFail, std::placeholders::_1));
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    sockClientDriver->Connect("hello world", ep, 0);
    RegisterMemory(sockServerDriver, iovPtrServer);
    RegisterMemory(sockClientDriver, iovPtrClient);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = iovPtrClient[i].lAddress;
        iov[i].rAddress = iovPtrServer[i].lAddress;
        iov[i].lKey = iovPtrClient[i].lKey;
        iov[i].rKey = iovPtrServer[i].lKey;
        iov[i].size = NN_NO4;
    }
    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);
    req.upCtxSize = NN_NO16;
    for (auto i = 0; i < NN_NO16; i++) {
        req.upCtxData[i] = 'a';
    }
    NResult result = ep->PostRead(req);
    EXPECT_EQ(NNCode::NN_OK, result);
    result = ep->PostWrite(req);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, SendRawSglSuccess)
{
    sockServerDriver->RegisterReqPostedHandler(std::bind(&sockOobRequestPostedFail, std::placeholders::_1));
    sockServerDriver->Initialize(sockOptions);
    sockServerDriver->Start();
    sockClientDriver->Initialize(sockOptions);
    sockClientDriver->Start();
    sockClientDriver->Connect("hello world", ep, 0);
    RegisterMemory(sockServerDriver, iovPtrServer);
    RegisterMemory(sockClientDriver, iovPtrClient);
    UBSHcomNetTransSgeIov iov[NN_NO16];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = iovPtrClient[i].lAddress;
        iov[i].rAddress = iovPtrServer[i].lAddress;
        iov[i].lKey = iovPtrClient[i].lKey;
        iov[i].rKey = iovPtrServer[i].lKey;
        iov[i].size = NN_NO4;
    }
    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);
    req.upCtxSize = NN_NO16;
    for (auto i = 0; i < NN_NO16; i++) {
        req.upCtxData[i] = 'a';
    }
    NResult result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestNetSockDriverOob, DestroyMemoryRegion)
{
    sockServerDriver->Initialize(sockOptions);
    sockClientDriver->Initialize(sockOptions);
    UBSHcomNetMemoryRegionPtr mr1;
    UBSHcomNetMemoryRegionPtr mr2;
    UBSHcomNetMemoryRegionPtr mr3;
    sockServerDriver->CreateMemoryRegion(NN_NO8, mr1);
    sockClientDriver->CreateMemoryRegion(NN_NO8, mr3);
    sockServerDriver->DestroyMemoryRegion(mr1);
    sockServerDriver->DestroyMemoryRegion(mr2);
    sockServerDriver->DestroyMemoryRegion(mr3);
}