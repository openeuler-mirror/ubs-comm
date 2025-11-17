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

#include "hcom.h"
#include "net_shm_sync_endpoint.h"
#include "net_shm_async_endpoint.h"
#include "net_security_alg.h"
#include "shm_worker.h"
#include "test_shm_common.h"
#include "test_shm_tls.h"

using namespace ock::hcom;
TestShmTls::TestShmTls() {}

UBSHcomNetEndpointPtr tlsShmServerEp = nullptr;
UBSHcomNetEndpointPtr tlsShmClientEp = nullptr;
static TestRegMrInfo tlsClientMrInfo;
static TestRegMrInfo tlsServerMrInfo;
static UBSHcomNetTransSgeIov tlsShmClientMrInfo[NN_NO4];
UBSHcomNetDriverOptions tlsShmOptions {};

UBSHcomNetDriver *tlsShmCDriver = nullptr;
UBSHcomNetDriver *tlsShmSDriver = nullptr;
std::string shmCertPath;
static uint32_t iovCnt = NN_NO4;
static sem_t sem;
static int g_nameSeed = 0;

int ShmValidateTlsCert()
{
    char *buffer;

    if ((buffer = getcwd(NULL, 0)) == NULL) {
        NN_LOG_ERROR("Cet path for TLS cert failed");
        return -1;
    }

    std::string currentPath = buffer;
    shmCertPath = currentPath + "/../test/opensslcrt/normalCert1";

    if (!CanonicalPath(shmCertPath)) {
        NN_LOG_ERROR("TLS cert path check failed " << shmCertPath);
        return -1;
    }

    return 0;
}

static void SetEncryptValue()
{
    // this step should exec after client connect and ep created
    std::string value = "value from server";
    size_t encryptLen = tlsShmServerEp->EstimatedEncryptLen(value.length());
    void *cipher = malloc(encryptLen);
    tlsShmServerEp->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    memcpy(reinterpret_cast<void *>(tlsServerMrInfo.lAddress), cipher, encryptLen);
}

static size_t SetClientEncryptValue()
{
    std::string value = "value from client";
    size_t encryptLen = tlsShmClientEp->EstimatedEncryptLen(value.length());
    void *cipher = malloc(encryptLen);
    tlsShmClientEp->Encrypt(value.c_str(), value.length(), cipher, encryptLen);

    memcpy(reinterpret_cast<void *>(tlsClientMrInfo.lAddress), cipher, encryptLen);
    return encryptLen;
}

static int ServerNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    tlsShmServerEp = newEP;
    SetEncryptValue();
    return 0;
}

static int ServerNewEndPointSend(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP,
    const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    tlsShmServerEp = newEP;
    return 0;
}

static void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    NN_LOG_INFO("end point " << ep->Id());
}

static int RequestReceivedServer(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("server request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp(&tlsServerMrInfo, sizeof(tlsServerMrInfo), 0);
    if ((result = tlsShmServerEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }
    NN_LOG_INFO("request rsp Mr info");
    return 0;
}

static TestRegMrInfo getRemoteMrInfo;
static int RequestReceivedClient(const UBSHcomNetRequestContext &ctx)
{
    memcpy(&getRemoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen());
    NN_LOG_INFO("get remote Mr info");
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

static void Erase(void *pass, int len) {}
static int Verify(void *x509, const char *path)
{
    return 0;
}

static bool CertCallback(const std::string &name, std::string &value)
{
    value = shmCertPath + "/server/cert.pem";
    return true;
}

static bool PrivateKeyCallback(const std::string &name, std::string &value, void *&keyPass, int &len,
    UBSHcomTLSEraseKeypass &erase)
{
    static char content[] = "huawei";
    keyPass = reinterpret_cast<void *>(content);
    len = sizeof(content);
    value = shmCertPath + "/server/key.pem";
    erase = std::bind(&Erase, std::placeholders::_1, std::placeholders::_2);

    return true;
}

static bool CACallback(const std::string &name, std::string &caPath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &peerCertVerifyType, UBSHcomTLSCertVerifyCallback &cb)
{
    caPath = shmCertPath + "/CA/cacert.pem";
    cb = std::bind(&Verify, std::placeholders::_1, std::placeholders::_2);
    return true;
}

static bool RegSglMem(UBSHcomNetDriver *driver, UBSHcomNetTransSgeIov mrInfo[],
    std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    for (int i = 0; i < 4; ++i) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = driver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        mrInfo[i].lAddress = mr->GetAddress();
        mrInfo[i].lKey = mr->GetLKey();
        mrInfo[i].size = NN_NO8;
        mrs.push_back(mr);
        memset(reinterpret_cast<void *>(mrInfo[i].lAddress), 0, mrInfo[i].size);
    }
    return true;
}

static void DestoryTlsMem(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    while (!mrs.empty()) {
        driver->DestroyMemoryRegion(mrs.back());
        mrs.pop_back();
    }
}

static bool RegReadWriteMem(UBSHcomNetDriver *driver, TestRegMrInfo mrInfo[],
    std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    UBSHcomNetMemoryRegionPtr mr;
    auto result = driver->CreateMemoryRegion(NN_NO1024, mr);
    if (result != NN_OK) {
        NN_LOG_ERROR("reg mr failed");
        return false;
    }
    mrInfo[0].lAddress = mr->GetAddress();
    mrInfo[0].lKey = mr->GetLKey();
    mrInfo[0].size = NN_NO1024;
    mrs.push_back(mr);
    memset(reinterpret_cast<void *>(mrInfo[0].lAddress), 0, mrInfo[0].size);

    return true;
}

static bool CreateServerDriver(UBSHcomNetDriver *&driver, int (*reqHandler)(const UBSHcomNetRequestContext &),
    UBSHcomNetDriverOptions &tlsShmOptions)
{
    auto name = "server_tls_" + std::to_string(g_nameSeed++);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, true);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create tlsShmSDriver already created");
        return false;
    }
    tlsShmOptions.oobType = ock::hcom::NET_OOB_UDS;
    tlsShmOptions.mode = ock::hcom::NET_EVENT_POLLING;

    UBSHcomNetOobUDSListenerOptions listenOpt;
    listenOpt.Name(UDSNAME);
    listenOpt.perm = 0;
    driver->AddOobUdsOptions(listenOpt);

    driver->RegisterNewEPHandler(
        std::bind(&ServerNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(reqHandler);
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    driver->RegisterTLSCertificationCallback(std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
    driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

    int result = 0;
    if ((result = driver->Initialize(tlsShmOptions)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("tlsShmSDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start asyncServerDriver " << result);
        return false;
    }
    NN_LOG_INFO("tlsShmSDriver started");
    return true;
}

static bool CreateServerDriverSend(UBSHcomNetDriver *&driver, int (*reqHandler)(const UBSHcomNetRequestContext &),
    UBSHcomNetDriverOptions &tlsShmOptions)
{
    auto name = "server_tls_" + std::to_string(g_nameSeed++);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, true);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create tlsShmSDriver already created");
        return false;
    }
    tlsShmOptions.oobType = ock::hcom::NET_OOB_UDS;
    tlsShmOptions.mode = ock::hcom::NET_EVENT_POLLING;

    UBSHcomNetOobUDSListenerOptions listenOpt;
    listenOpt.Name(UDSNAME);
    listenOpt.perm = 0;
    driver->AddOobUdsOptions(listenOpt);

    driver->RegisterNewEPHandler(
        std::bind(&ServerNewEndPointSend, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(reqHandler);
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    driver->RegisterTLSCertificationCallback(std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
    driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

    int result = 0;
    if ((result = driver->Initialize(tlsShmOptions)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("tlsShmSDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start asyncServerDriver " << result);
        return false;
    }
    NN_LOG_INFO("tlsShmSDriver started");
    return true;
}

static bool CreateClientDriver(UBSHcomNetDriver *&driver, int (*reqHandler)(const UBSHcomNetRequestContext &),
    UBSHcomNetDriverOptions &tlsShmOptions)
{
    auto name = "client_tls_" + std::to_string(g_nameSeed++);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, false);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create tlsShmCDriver already created");
        return false;
    }
    tlsShmOptions.oobType = ock::hcom::NET_OOB_UDS;
    tlsShmOptions.mode = ock::hcom::NET_EVENT_POLLING;

    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(reqHandler);
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    driver->RegisterTLSCertificationCallback(std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
    driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

    int result = 0;
    if ((result = driver->Initialize(tlsShmOptions)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("tlsShmCDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start tlsShmCDriver " << result);
        return false;
    }
    NN_LOG_INFO("tlsShmCDriver started");
    return true;
}

static bool CreateSyncClientDriver(UBSHcomNetDriver *&driver, UBSHcomNetDriverOptions &options)
{
    auto name = "clientSync_ep_" + std::to_string(g_nameSeed++);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, false);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create clientDriver already created");
        return false;
    }

    options.mode = ock::hcom::NET_EVENT_POLLING;
    options.oobType = ock::hcom::NET_OOB_UDS;
    options.dontStartWorkers = true;

    driver->RegisterTLSCertificationCallback(std::bind(&CertCallback, std::placeholders::_1, std::placeholders::_2));
    driver->RegisterTLSCaCallback(std::bind(&CACallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    driver->RegisterTLSPrivateKeyCallback(std::bind(&PrivateKeyCallback, std::placeholders::_1, std::placeholders::_2,
        std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));

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

void TlsCloseShmDriver(UBSHcomNetDriver *&tlsShmCDriver, UBSHcomNetDriver *&tlsShmSDriver)
{
    tlsShmClientEp->Close();
    tlsShmServerEp->Close();
    if (tlsShmServerEp != nullptr) {
        tlsShmServerEp.Set(nullptr);
    }
    if (tlsShmClientEp != nullptr) {
        tlsShmClientEp.Set(nullptr);
    }
    std::string serverName = tlsShmSDriver->Name();
    std::string clientName = tlsShmCDriver->Name();
    if (tlsShmCDriver->IsStarted()) {
        tlsShmCDriver->Stop();
    }
    if (tlsShmCDriver->IsInited()) {
        tlsShmCDriver->UnInitialize();
    }
    if (tlsShmSDriver->IsStarted()) {
        tlsShmSDriver->Stop();
    }
    if (tlsShmSDriver->IsInited()) {
        tlsShmSDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(serverName);
    UBSHcomNetDriver::DestroyInstance(clientName);
}

void TestShmTls::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
    ASSERT_EQ(0, ShmValidateTlsCert());
}

void TestShmTls::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestShmTls, PostSendTls)
{
    NResult result;

    tlsShmOptions.enableTls = true;
    CreateServerDriverSend(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    result = tlsShmClientEp->PostSend(1, req);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&AesGcm128::Encrypt, bool (AesGcm128::*)(const unsigned char *, const unsigned char *,
        const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));

    result = tlsShmClientEp->PostSend(1, req);
    EXPECT_EQ(NN_ENCRYPT_FAILED, result);

    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostSendTlsCipherSuite256)
{
    NResult result;

    tlsShmOptions.enableTls = true;
    tlsShmOptions.cipherSuite = ock::hcom::AES_GCM_256;
    CreateServerDriverSend(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    result = tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);
    EXPECT_EQ(SH_OK, result);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    result = tlsShmClientEp->PostSend(1, req);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&AesGcm128::Encrypt, bool (AesGcm128::*)(const unsigned char *, const unsigned char *,
        const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));

    result = tlsShmClientEp->PostSend(1, req);
    EXPECT_EQ(NN_ENCRYPT_FAILED, result);

    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostSendTlsCipherSuiteUnknown)
{
    tlsShmOptions.enableTls = true;
    tlsShmOptions.cipherSuite = ock::hcom::UBSHcomNetCipherSuite(4);
    auto result = CreateServerDriverSend(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    EXPECT_EQ(false, result);

    result = CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);
    EXPECT_EQ(false, result);
}

TEST_F(TestShmTls, PostSendOpInfoTls)
{
    NResult result;

    tlsShmOptions.enableTls = true;
    tlsShmOptions.cipherSuite = ock::hcom::AES_GCM_128;
    CreateServerDriverSend(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    UBSHcomNetTransOpInfo innerOpInfo(2, 0, 0, NTH_TWO_SIDE);
    result = tlsShmClientEp->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&AesGcm128::Encrypt, bool (AesGcm128::*)(const unsigned char *, const unsigned char *,
        const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));

    result = tlsShmClientEp->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(NN_ENCRYPT_FAILED, result);

    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostSendRawTls)
{
    NResult result;

    tlsShmOptions.enableTls = true;
    CreateServerDriverSend(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    result = tlsShmClientEp->PostSendRaw(req, 1);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&AesGcm128::Encrypt, bool (AesGcm128::*)(const unsigned char *, const unsigned char *,
        const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));

    result = tlsShmClientEp->PostSendRaw(req, 1);
    EXPECT_EQ(NN_ENCRYPT_FAILED, result);

    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostSendRawSglTls)
{
    NResult result;

    tlsShmOptions.enableTls = true;
    CreateServerDriverSend(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    bool res = RegSglMem(tlsShmCDriver, tlsShmClientMrInfo, mrs);
    EXPECT_TRUE(res);
    UBSHcomNetTransSglRequest reqSgl(tlsShmClientMrInfo, iovCnt, 0);
    result = tlsShmClientEp->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&AesGcm128::Encrypt, bool (AesGcm128::*)(const unsigned char *, const unsigned char *,
        const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));

    result = tlsShmClientEp->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(NN_ENCRYPT_FAILED, result);

    DestoryTlsMem(tlsShmCDriver, mrs);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostTlsReadWrite)
{
    NResult result;
    tlsShmOptions.enableTls = true;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);
    sem_init(&sem, 0, 0);

    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = tlsShmClientEp->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);

    sem_wait(&sem);

    UBSHcomNetTransRequest req;
    size_t encryptLen = SetClientEncryptValue();
    req.lAddress = tlsClientMrInfo.lAddress;
    req.rAddress = getRemoteMrInfo.lAddress;
    req.lKey = tlsClientMrInfo.lKey;
    req.rKey = getRemoteMrInfo.lKey;
    req.size = encryptLen;

    result = tlsShmClientEp->PostRead(req);
    EXPECT_EQ(SH_OK, result);
    sem_wait(&sem);

    void *readValue = reinterpret_cast<void *>(req.lAddress);
    size_t rawLen = tlsShmClientEp->EstimatedDecryptLen(req.size);
    void *rawValue = malloc(rawLen);
    tlsShmClientEp->Decrypt(readValue, req.size, rawValue, rawLen);
    NN_LOG_INFO("post read value is : " << rawValue);
    NN_LOG_INFO("value[" << 0 << "]= " << readValue);

    NN_LOG_INFO("=========Read end ,Write start===========");
    SetClientEncryptValue();
    result = tlsShmClientEp->PostWrite(req);
    EXPECT_EQ(SH_OK, result);
    void *readServerValue = reinterpret_cast<void *>(req.rAddress);
    size_t rawServerLen = tlsShmClientEp->EstimatedDecryptLen(req.size);
    void *rawServerValue = malloc(rawLen);
    tlsShmClientEp->Decrypt(readServerValue, req.size, rawServerValue, rawServerLen);
    NN_LOG_INFO("post Write value is : " << rawServerValue);
    NN_LOG_INFO("value[" << 0 << "]= " << rawServerValue);

    free(rawValue);
    free(rawServerValue);
    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostTlsEncryptFail)
{
    NResult result;
    tlsShmOptions.enableTls = true;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);
    sem_init(&sem, 0, 0);

    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = tlsShmClientEp->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);

    sem_wait(&sem);

    /* Set Client Encrypt Value ,when value.length() is 0 */
    std::string value;
    size_t encryptLen = tlsShmClientEp->EstimatedEncryptLen(value.length());
    EXPECT_EQ(0, encryptLen);
    value = "value from client";
    encryptLen = tlsShmClientEp->EstimatedEncryptLen(value.length());
    void *cipher = malloc(encryptLen);

    /* Set Client Encrypt Value ,AesGcm128::Encrypt is fail */
    MOCKER_CPP(&AesGcm128::Encrypt, bool (AesGcm128::*)(const unsigned char *, const unsigned char *,
        const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));
    result = tlsShmClientEp->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    EXPECT_EQ(NN_ERROR, result);

    free(cipher);
    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostTlsEncryptFail1)
{
    NResult result;
    tlsShmOptions.enableTls = false;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);
    sem_init(&sem, 0, 0);

    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = tlsShmClientEp->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);

    sem_wait(&sem);

    /* Encrypt ,when Options.enableTls = false */
    std::string value = "value from client";
    size_t encryptLen = tlsShmClientEp->EstimatedEncryptLen(value.length());
    EXPECT_EQ(0, encryptLen);
    void *cipher = malloc(encryptLen);
    result = tlsShmClientEp->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    EXPECT_EQ(NN_ERROR, result);

    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostTlsDecryptFail)
{
    NResult result;
    tlsShmOptions.enableTls = true;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);
    sem_init(&sem, 0, 0);

    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = tlsShmClientEp->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);

    sem_wait(&sem);

    UBSHcomNetTransRequest req;
    size_t encryptLen = SetClientEncryptValue();
    req.lAddress = tlsClientMrInfo.lAddress;
    req.rAddress = getRemoteMrInfo.lAddress;
    req.lKey = tlsClientMrInfo.lKey;
    req.rKey = getRemoteMrInfo.lKey;
    req.size = encryptLen;

    void *readValue = reinterpret_cast<void *>(req.lAddress);
    size_t rawLen = tlsShmClientEp->EstimatedDecryptLen(req.size);
    void *rawValue = malloc(rawLen);

    /* Set Decrypt ,AesGcm128::Decrypt is fail */
    MOCKER_CPP(&AesGcm128::Decrypt,
        bool (AesGcm128::*)(const unsigned char *, const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));
    result = tlsShmClientEp->Decrypt(readValue, req.size, rawValue, rawLen);
    EXPECT_EQ(NN_ERROR, result);

    free(rawValue);
    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, PostTlsDecryptFail1)
{
    NResult result;
    tlsShmOptions.enableTls = false;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsShmOptions);
    CreateClientDriver(tlsShmCDriver, RequestReceivedClient, tlsShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp);
    sem_init(&sem, 0, 0);

    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = tlsShmClientEp->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);

    sem_wait(&sem);

    /* Decrypt ,when Options.enableTls = false */
    void *readValue = reinterpret_cast<void *>(tlsClientMrInfo.lAddress);
    size_t rawLen = tlsShmClientEp->EstimatedDecryptLen(tlsClientMrInfo.size);
    EXPECT_EQ(0, rawLen);
    void *rawValue = malloc(rawLen);
    result = tlsShmClientEp->Decrypt(readValue, tlsClientMrInfo.lAddress, rawValue, rawLen);
    EXPECT_EQ(NN_ERROR, result);

    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, SyncPostTlsReadWrite)
{
    NResult result;
    UBSHcomNetDriverOptions tlsSyncShmOptions {};
    tlsSyncShmOptions.enableTls = true;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsSyncShmOptions);
    CreateSyncClientDriver(tlsShmCDriver, tlsSyncShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp, NET_EP_SELF_POLLING);

    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = tlsShmClientEp->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = tlsShmClientEp->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = tlsShmClientEp->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(&getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransRequest req;
    size_t encryptLen = SetClientEncryptValue();
    req.lAddress = tlsClientMrInfo.lAddress;
    req.rAddress = getRemoteMrInfo.lAddress;
    req.lKey = tlsClientMrInfo.lKey;
    req.rKey = getRemoteMrInfo.lKey;
    req.size = encryptLen;

    result = tlsShmClientEp->PostRead(req);
    EXPECT_EQ(SH_OK, result);

    void *readValue = reinterpret_cast<void *>(req.lAddress);
    size_t rawLen = tlsShmClientEp->EstimatedDecryptLen(req.size);
    void *rawValue = malloc(rawLen);
    tlsShmClientEp->Decrypt(readValue, req.size, rawValue, rawLen);
    NN_LOG_INFO("post read value is : " << rawValue);
    NN_LOG_INFO("value[" << 0 << "]= " << readValue);

    NN_LOG_INFO("=========Read end ,Write start===========");
    SetClientEncryptValue();
    result = tlsShmClientEp->PostWrite(req);
    EXPECT_EQ(SH_OK, result);
    void *readServerValue = reinterpret_cast<void *>(req.rAddress);
    size_t rawServerLen = tlsShmClientEp->EstimatedDecryptLen(req.size);
    void *rawServerValue = malloc(rawLen);
    tlsShmClientEp->Decrypt(readServerValue, req.size, rawServerValue, rawServerLen);
    NN_LOG_INFO("post Write value is : " << rawServerValue);
    NN_LOG_INFO("value[" << 0 << "]= " << rawServerValue);

    free(rawValue);
    free(rawServerValue);
    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, SyncPostTlsEncryptFail)
{
    NResult result;
    UBSHcomNetDriverOptions tlsSyncShmOptions {};
    tlsSyncShmOptions.enableTls = true;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsSyncShmOptions);
    CreateSyncClientDriver(tlsShmCDriver, tlsSyncShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp, NET_EP_SELF_POLLING);

    /* Set Client Encrypt Value ,when value.length() is 0 */
    std::string value;
    size_t encryptLen = tlsShmClientEp->EstimatedEncryptLen(value.length());
    EXPECT_EQ(0, encryptLen);

    /* Set Client Encrypt Value ,AesGcm128::Encrypt is fail */
    value = "value from client";
    encryptLen = tlsShmClientEp->EstimatedEncryptLen(value.length());
    void *cipher = malloc(encryptLen);
    MOCKER_CPP(&AesGcm128::Encrypt, bool (AesGcm128::*)(const unsigned char *, const unsigned char *,
        const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));
    result = tlsShmClientEp->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    EXPECT_EQ(NN_ERROR, result);

    free(cipher);
    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, SyncPostTlsEncryptFail1)
{
    NResult result;
    UBSHcomNetDriverOptions tlsSyncShmOptions {};
    tlsSyncShmOptions.enableTls = false;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsSyncShmOptions);
    CreateSyncClientDriver(tlsShmCDriver, tlsSyncShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp, NET_EP_SELF_POLLING);

    /* Encrypt ,when Options.enableTls = false */
    std::string value = "value from client";
    size_t encryptLen = tlsShmClientEp->EstimatedEncryptLen(value.length());
    EXPECT_EQ(0, encryptLen);
    void *cipher = malloc(encryptLen);
    result = tlsShmClientEp->Encrypt(value.c_str(), value.length(), cipher, encryptLen);
    EXPECT_EQ(NN_ERROR, result);

    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, SyncPostTlsDecryptFail)
{
    NResult result;
    UBSHcomNetDriverOptions tlsSyncShmOptions {};
    tlsSyncShmOptions.enableTls = true;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsSyncShmOptions);
    CreateSyncClientDriver(tlsShmCDriver, tlsSyncShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp, NET_EP_SELF_POLLING);
    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = tlsShmClientEp->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = tlsShmClientEp->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = tlsShmClientEp->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(&getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransRequest req;
    size_t encryptLen = SetClientEncryptValue();
    req.lAddress = tlsClientMrInfo.lAddress;
    req.rAddress = getRemoteMrInfo.lAddress;
    req.lKey = tlsClientMrInfo.lKey;
    req.rKey = getRemoteMrInfo.lKey;
    req.size = encryptLen;

    result = tlsShmClientEp->PostRead(req);
    EXPECT_EQ(SH_OK, result);

    void *readValue = reinterpret_cast<void *>(req.lAddress);
    size_t rawLen = tlsShmClientEp->EstimatedDecryptLen(req.size);
    void *rawValue = malloc(rawLen);

    /* Set Decrypt ,AesGcm128::Decrypt is fail */
    MOCKER_CPP(&AesGcm128::Decrypt,
        bool (AesGcm128::*)(const unsigned char *, const unsigned char *, size_t, unsigned char *, size_t &))
        .defaults()
        .will(returnValue(false));
    result = tlsShmClientEp->Decrypt(readValue, req.size, rawValue, rawLen);
    EXPECT_EQ(NN_ERROR, result);

    free(rawValue);
    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}

TEST_F(TestShmTls, SyncPostTlsDecryptFail1)
{
    NResult result;
    UBSHcomNetDriverOptions tlsSyncShmOptions {};
    tlsSyncShmOptions.enableTls = false;
    CreateServerDriver(tlsShmSDriver, RequestReceivedServer, tlsSyncShmOptions);
    CreateSyncClientDriver(tlsShmCDriver, tlsSyncShmOptions);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(tlsShmSDriver, &tlsServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(tlsShmCDriver, &tlsClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    tlsShmCDriver->Connect(UDSNAME, 0, "hello server", tlsShmClientEp, NET_EP_SELF_POLLING);

    /* Decrypt ,when Options.enableTls = false */
    void *readValue = reinterpret_cast<void *>(tlsClientMrInfo.lAddress);
    size_t rawLen = tlsShmClientEp->EstimatedDecryptLen(tlsClientMrInfo.size);
    EXPECT_EQ(0, rawLen);
    void *rawValue = malloc(rawLen);
    result = tlsShmClientEp->Decrypt(readValue, tlsClientMrInfo.lAddress, rawValue, rawLen);
    EXPECT_EQ(NN_ERROR, result);

    DestoryTlsMem(tlsShmSDriver, mrServer);
    DestoryTlsMem(tlsShmCDriver, mrClient);
    TlsCloseShmDriver(tlsShmCDriver, tlsShmSDriver);
}