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
#include "shm_worker.h"
#include "shm_composed_endpoint.h"
#include "test_shm_common.h"
#include "test_shm_endpoint.h"

using namespace ock::hcom;
TestShmEndpoint::TestShmEndpoint() {}

static uint32_t iovCnt = NN_NO4;
static UBSHcomNetEndpointPtr asyncEp = nullptr;
static sem_t sem;
static int g_nameSeed = 0;

static int ServerNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    asyncEp = newEP;
    return 0;
}

static void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    if (asyncEp != nullptr) {
        asyncEp.Set(nullptr);
    }
    NN_LOG_INFO("end point " << ep->Id());
}

static int RequestReceived(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("client request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);
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


static bool RegSglMem(UBSHcomNetDriver *driver, UBSHcomNetTransSgeIov mrInfo[],
    std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    for (int i = 0; i < NN_NO4; ++i) {
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
        memset(reinterpret_cast<void *>(mrInfo[i].lAddress), 1, mrInfo[i].size);
    }
    return true;
}

static void DestorySglMem(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    while (!mrs.empty()) {
        driver->DestroyMemoryRegion(mrs.back());
        mrs.pop_back();
    }
}

static bool RegReadWriteMem(UBSHcomNetDriver *driver, TestRegMrInfo mrInfo[],
    std::vector<UBSHcomNetMemoryRegionPtr> &mrReadWrite)
{
    UBSHcomNetMemoryRegionPtr mr;
    auto result = driver->CreateMemoryRegion(NN_NO16, mr);
    if (result != NN_OK) {
        NN_LOG_ERROR("reg mr failed");
        return false;
    }
    mrInfo[0].lAddress = mr->GetAddress();
    mrInfo[0].lKey = mr->GetLKey();
    mrInfo[0].size = NN_NO8;
    mrReadWrite.push_back(mr);
    memset(reinterpret_cast<void *>(mrInfo[0].lAddress), 0, mrInfo[0].size);

    return true;
}

static bool RegReadWriteSglMem(UBSHcomNetDriver *driver, TestRegMrInfo mrInfo[],
    std::vector<UBSHcomNetMemoryRegionPtr> &mrReadWrite)
{
    for (int i = 0; i < NN_NO4; ++i) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = driver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        mrInfo[i].lAddress = mr->GetAddress();
        mrInfo[i].lKey = mr->GetLKey();
        mrInfo[i].size = NN_NO8;
        mrReadWrite.push_back(mr);
        memset(reinterpret_cast<void *>(mrInfo[i].lAddress), '1', mrInfo[i].size);
    }
    return true;
}

/* server new request sgl callback */
TestRegMrInfo asyncServerMrInfo[NN_NO4];
static int RequestReceivedServer(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("server request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp((void *)(asyncServerMrInfo), sizeof(asyncServerMrInfo), 0);
    if ((result = asyncEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }

    NN_LOG_INFO("request rsp Mr info");
    std::string readValue((char *)asyncServerMrInfo[0].lAddress, asyncServerMrInfo[0].size);
    NN_LOG_INFO("idx:" << 0 << " key:" << asyncServerMrInfo[0].lKey << " address:" << asyncServerMrInfo[0].lAddress <<
        " size: " << asyncServerMrInfo[0].size << "string: " << readValue);
    return 0;
}
static int RequestReceivedSglServer(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("server request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp((void *)(asyncServerMrInfo), sizeof(asyncServerMrInfo), 0);
    if ((result = asyncEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }

    NN_LOG_INFO("request rsp Mr info");
    for (uint16_t i = 0; i < NN_NO4; i++) {
        NN_LOG_INFO("idx:" << i << " key:" << asyncServerMrInfo[i].lKey << " address:" <<
            asyncServerMrInfo[i].lAddress << " size: " << asyncServerMrInfo[i].size);
    }
    return 0;
}

/* client new request sgl callback */

TestRegMrInfo getRemoteMrInfo[NN_NO4];
static int RequestReceivedClient(const UBSHcomNetRequestContext &ctx)
{
    memcpy(getRemoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen());
    NN_LOG_INFO("get remote Mr info");
    std::string readValue((char *)getRemoteMrInfo[0].lAddress, getRemoteMrInfo[0].size);
    NN_LOG_INFO("idx:" << 0 << " key:" << getRemoteMrInfo[0].lKey << " address:" << getRemoteMrInfo[0].lAddress <<
        " size:" << getRemoteMrInfo[0].size << "string: " << readValue);

    sem_post(&sem);
    return 0;
}

static int RequestReceivedSglClient(const UBSHcomNetRequestContext &ctx)
{
    memcpy(getRemoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen());
    NN_LOG_INFO("get remote Mr info");
    sem_post(&sem);
    return 0;
}


static bool CreateServerDriver(UBSHcomNetDriver *&driver, int (*reqHandler)(const UBSHcomNetRequestContext &),
    UBSHcomNetDriverOptions &asyncShmOptions)
{
    auto name = "server_ep_" + std::to_string(g_nameSeed++);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, true);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create asyncServerDriver already created");
        return false;
    }
    asyncShmOptions.oobType = ock::hcom::NET_OOB_UDS;
    asyncShmOptions.mode = ock::hcom::NET_EVENT_POLLING;
    asyncShmOptions.enableTls = false;

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

    int result = 0;
    if ((result = driver->Initialize(asyncShmOptions)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("asyncServerDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start asyncServerDriver " << result);
        return false;
    }
    NN_LOG_INFO("asyncServerDriver started");
    return true;
}

static bool CreateClientDriver(UBSHcomNetDriver *&driver, int (*reqHandler)(const UBSHcomNetRequestContext &),
    UBSHcomNetDriverOptions &asyncShmOptions)
{
    auto name = "client_ep_" + std::to_string(g_nameSeed++);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, false);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create asyncClientDriver already created");
        return false;
    }
    asyncShmOptions.oobType = ock::hcom::NET_OOB_UDS;
    asyncShmOptions.mode = ock::hcom::NET_EVENT_POLLING;
    asyncShmOptions.enableTls = false;

    driver->RegisterEPBrokenHandler(std::bind(&EndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(reqHandler);
    driver->RegisterReqPostedHandler(std::bind(&RequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&OneSideDone, std::placeholders::_1));

    int result = 0;
    if ((result = driver->Initialize(asyncShmOptions)) != 0) {
        NN_LOG_ERROR("failed to initialize driver " << result);
        return false;
    }
    NN_LOG_INFO("asyncClientDriver initialized");

    if ((result = driver->Start()) != 0) {
        NN_LOG_ERROR("failed to start asyncClientDriver " << result);
        return false;
    }
    NN_LOG_INFO("asyncClientDriver started");
    return true;
}


void CloseShmDriver(UBSHcomNetDriver *&asyncClientDriver, UBSHcomNetDriver *&asyncServerDriver)
{
    asyncEp->Close();
    if (asyncEp != nullptr) {
        asyncEp.Set(nullptr);
    }
    std::string serverName = asyncServerDriver->Name();
    std::string clientName = asyncClientDriver->Name();
    if (asyncServerDriver->IsStarted()) {
        asyncServerDriver->Stop();
    }
    if (asyncServerDriver->IsInited()) {
        asyncServerDriver->UnInitialize();
    }

    if (asyncClientDriver->IsStarted()) {
        asyncClientDriver->Stop();
    }
    if (asyncClientDriver->IsInited()) {
        asyncClientDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(serverName);
    UBSHcomNetDriver::DestroyInstance(clientName);
}

void TestShmEndpoint::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestShmEndpoint::TearDown()
{
    GlobalMockObject::verify();
}


TEST_F(TestShmEndpoint, PostSendRetry)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriverOptions asyncShmOptions {};
    UBSHcomNetDriver *asyncClientDriver = nullptr;
    UBSHcomNetDriver *asyncServerDriver = nullptr;
    CreateServerDriver(asyncServerDriver, RequestReceived, asyncShmOptions);
    CreateClientDriver(asyncClientDriver, RequestReceived, asyncShmOptions);
    asyncClientDriver->Connect(UDSNAME, 0, "hello server", ep);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_OK, result);

    UBSHcomNetTransOpInfo innerOpInfo(NN_NO2, 0, 0, NTH_TWO_SIDE);
    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SH_OK, result);

    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_OK, result);

    ep->DefaultTimeout(1);
    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1));
    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1));

    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1));

    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmDataChannel::TryOccupyWithWait).defaults().will(returnValue(SH_NOT_INITIALIZED));
    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .defaults()
        .will(returnValue(-1));
    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_RETRY_FULL, result);

    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SH_RETRY_FULL, result);

    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_RETRY_FULL, result);

    GlobalMockObject::verify();

    ep->Close();
    CloseShmDriver(asyncClientDriver, asyncServerDriver);
}

TEST_F(TestShmEndpoint, PostSendRawSglRetry)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriverOptions asyncShmOptions {};
    UBSHcomNetDriver *asyncClientDriver = nullptr;
    UBSHcomNetDriver *asyncServerDriver = nullptr;
    CreateServerDriver(asyncServerDriver, RequestReceived, asyncShmOptions);
    CreateClientDriver(asyncClientDriver, RequestReceived, asyncShmOptions);
    asyncClientDriver->Connect(UDSNAME, 0, "hello server", ep);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    UBSHcomNetTransSgeIov clientMrInfo[NN_NO4];
    bool res = RegSglMem(asyncClientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);
    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);

    result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(SH_OK, result);

    UBSHcomNetTransSglRequest req2(clientMrInfo, iovCnt, NN_NO29);
    result = ep->PostSendRawSgl(req2, 1);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    UBSHcomNetTransSglRequest reqSgl(clientMrInfo, iovCnt, 0);
    result = ep->PostSendRawSgl(reqSgl, 0);
    EXPECT_EQ(NN_INVALID_PARAM, result);

    ep->DefaultTimeout(1);
    MOCKER_CPP(&UBSHcomNetAtomicState<ShmChannelState>::Compare).defaults().will(returnValue(true));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(SH_CH_BROKEN, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .defaults()
        .will(returnValue(-1));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(SH_RETRY_FULL, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1));

    MOCKER_CPP(&ShmChannel::RemoveOpCompInfo, HResult(ShmChannel::*)(ShmOpCompInfo *))
        .defaults()
        .will(returnValue(317));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    GlobalMockObject::verify();

    ShmSglOpContextInfo *infoSgl = nullptr;
    MOCKER_CPP(&OpContextInfoPool<ShmSglOpContextInfo>::Get).defaults().will(returnValue(infoSgl));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    GlobalMockObject::verify();

    ShmOpCompInfo *info = nullptr;
    MOCKER_CPP(&OpContextInfoPool<ShmOpCompInfo>::Get).defaults().will(returnValue(info));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(SH_OP_CTX_FULL, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&UBSHcomNetAtomicState<UBSHcomNetEndPointState>::Compare).defaults().will(returnValue(false));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(NN_EP_NOT_ESTABLISHED, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&MemoryRegionChecker::Validate).defaults().will(returnValue(NN_INVALID_LKEY));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(NN_INVALID_LKEY, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmDataChannel::TryOccupyWithWait).defaults().will(returnValue(SH_NOT_INITIALIZED));
    result = ep->PostSendRawSgl(reqSgl, 1);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    GlobalMockObject::verify();

    DestorySglMem(asyncClientDriver, mrs);
    ep->Close();

    CloseShmDriver(asyncClientDriver, asyncServerDriver);
}

TEST_F(TestShmEndpoint, PostReadWrite)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriverOptions asyncShmOptions {};
    UBSHcomNetDriver *asyncClientDriver = nullptr;
    UBSHcomNetDriver *asyncServerDriver = nullptr;
    CreateServerDriver(asyncServerDriver, RequestReceivedServer, asyncShmOptions);
    CreateClientDriver(asyncClientDriver, RequestReceivedClient, asyncShmOptions);
    asyncClientDriver->Connect(UDSNAME, 0, "hello server", ep);
    ep->DefaultTimeout(1);
    sem_init(&sem, 0, 0);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(asyncServerDriver, asyncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    TestRegMrInfo asyncClientMrInfo[NN_NO4];
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(asyncClientDriver, asyncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);

    sem_wait(&sem);

    UBSHcomNetTransRequest req;
    req.lAddress = asyncClientMrInfo[0].lAddress;
    req.rAddress = getRemoteMrInfo[0].lAddress;
    req.lKey = asyncClientMrInfo[0].lKey;
    req.rKey = getRemoteMrInfo[0].lKey;
    req.size = getRemoteMrInfo[0].size;

    result = ep->PostRead(req);
    EXPECT_EQ(SH_OK, result);
    sem_wait(&sem);

    std::string readValue((char *)asyncClientMrInfo->lAddress, asyncClientMrInfo->size);
    NN_LOG_INFO("value[" << 0 << "]= " << readValue);

    result = ep->PostWrite(req);
    EXPECT_EQ(SH_OK, result);
    sem_wait(&sem);

    ShmHandlePtr localMrHandle = nullptr;
    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap).defaults().will(returnValue(localMrHandle));
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_ERROR, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .defaults()
        .will(returnValue(-1));
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&UBSHcomNetAtomicState<ShmChannelState>::Compare).defaults().will(returnValue(true));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_CH_BROKEN, result);

    GlobalMockObject::verify();

    ShmOpContextInfo *info = nullptr;
    MOCKER_CPP(&OpContextInfoPool<ShmOpContextInfo>::Get).defaults().will(returnValue(info));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_OP_CTX_FULL, result);

    result = ep->PostWrite(req);
    EXPECT_EQ(SH_OP_CTX_FULL, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .stubs()
        .will(returnValue(-1));
    MOCKER_CPP(&ShmChannel::RemoveOpCtxInfo).defaults().will(returnValue(SH_OP_CTX_REMOVED));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    GlobalMockObject::verify();

    DestorySglMem(asyncServerDriver, mrServer);
    DestorySglMem(asyncClientDriver, mrClient);
    ep->Close();
    CloseShmDriver(asyncClientDriver, asyncServerDriver);
}

TEST_F(TestShmEndpoint, PostReadWriteSgl)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriverOptions asyncShmOptions {};
    UBSHcomNetDriver *asyncClientDriver = nullptr;
    UBSHcomNetDriver *asyncServerDriver = nullptr;
    CreateServerDriver(asyncServerDriver, RequestReceivedSglServer, asyncShmOptions);
    CreateClientDriver(asyncClientDriver, RequestReceivedSglClient, asyncShmOptions);
    asyncClientDriver->Connect(UDSNAME, 0, "hello server", ep);
    ep->DefaultTimeout(1);
    sem_init(&sem, 0, 0);

    bool res;
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteSglMem(asyncServerDriver, asyncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    TestRegMrInfo asyncClientMrInfo[NN_NO4];
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteSglMem(asyncClientDriver, asyncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);

    sem_wait(&sem);

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = asyncClientMrInfo[i].lAddress;
        iov[i].rAddress = getRemoteMrInfo[i].lAddress;
        iov[i].lKey = asyncClientMrInfo[i].lKey;
        iov[i].rKey = getRemoteMrInfo[i].lKey;
        iov[i].size = getRemoteMrInfo[i].size;
    }

    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);
    result = ep->PostRead(req);
    EXPECT_EQ(SH_OK, result);
    sem_wait(&sem);

    for (uint16_t i = 0; i < NN_NO4; i++) {
        std::string readValue((char *)asyncClientMrInfo[i].lAddress, asyncClientMrInfo[i].size);
        NN_LOG_INFO("value[" << i << "]= " << readValue);
    }
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_OK, result);
    sem_wait(&sem);

    ShmOpContextInfo *info = nullptr;
    MOCKER_CPP(&OpContextInfoPool<ShmOpContextInfo>::Get).defaults().will(returnValue(info));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_OP_CTX_FULL, result);

    result = ep->PostWrite(req);
    EXPECT_EQ(SH_OP_CTX_FULL, result);

    GlobalMockObject::verify();

    ShmSglOpContextInfo *infoSgl = nullptr;
    MOCKER_CPP(&OpContextInfoPool<ShmSglOpContextInfo>::Get).defaults().will(returnValue(infoSgl));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    GlobalMockObject::verify();

    ShmHandlePtr localMrHandle = nullptr;
    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap).defaults().will(returnValue(localMrHandle));
    MOCKER_CPP(&ShmChannel::RemoveOpCtxInfo).defaults().will(returnValue(SH_OP_CTX_REMOVED));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_ERROR, result);

    GlobalMockObject::verify();

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .defaults()
        .will(returnValue(-1));
    MOCKER_CPP(&ShmChannel::RemoveOpCtxInfo).defaults().will(returnValue(SH_OP_CTX_REMOVED));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    GlobalMockObject::verify();

    DestorySglMem(asyncServerDriver, mrServer);
    DestorySglMem(asyncClientDriver, mrClient);

    CloseShmDriver(asyncClientDriver, asyncServerDriver);
}

TEST_F(TestShmEndpoint, GetRemoteUdsIdInfo)
{
    NResult result;
    UBSHcomNetEndpointPtr ep = nullptr;
    UBSHcomNetDriverOptions asyncShmOptions {};
    UBSHcomNetDriver *asyncClientDriver = nullptr;
    UBSHcomNetDriver *asyncServerDriver = nullptr;
    CreateServerDriver(asyncServerDriver, RequestReceived, asyncShmOptions);
    CreateClientDriver(asyncClientDriver, RequestReceived, asyncShmOptions);
    asyncClientDriver->Connect(UDSNAME, 0, "hello server", ep);

    UBSHcomEpOptions epOptions {};
    result = ep->SetEpOption(epOptions);
    EXPECT_EQ(NN_OK, result);

    UBSHcomNetUdsIdInfo idInfo {};
    if (asyncEp != nullptr) {
        result = asyncEp->GetRemoteUdsIdInfo(idInfo);
        EXPECT_EQ(NN_OK, result);
        NN_LOG_INFO("=======new endpoint remote uds ids, pid: " << idInfo.pid << " uid: " << idInfo.uid << " gid: " <<
            idInfo.gid << " result:" << result);
    }

    GlobalMockObject::verify();

    MOCKER_CPP(&UBSHcomNetAtomicState<UBSHcomNetEndPointState>::Compare).defaults().will(returnValue(false));
    result = asyncEp->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(NN_EP_NOT_ESTABLISHED, result);

    GlobalMockObject::verify();

    result = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(NN_UDS_ID_INFO_NOT_SUPPORT, result);

    ep->Close();
    CloseShmDriver(asyncClientDriver, asyncServerDriver);
}