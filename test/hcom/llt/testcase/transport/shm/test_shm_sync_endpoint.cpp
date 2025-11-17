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
#include "shm_queue.h"
#include "shm_common.h"
#include "shm_worker.h"
#include "test_shm_common.h"
#include "test_shm_sync_endpoint.h"

using namespace ock::hcom;
TestShmSyncEndpoint::TestShmSyncEndpoint() {}

static UBSHcomNetTransSgeIov clientMrInfo[NN_NO4];
static uint32_t iovCnt = NN_NO4;
static UBSHcomNetEndpointPtr syncEp = nullptr;
static TestRegMrInfo syncClientMrInfo[NN_NO4];
static int g_nameSeed = 0;


static int ServerNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    syncEp = newEP;
    return 0;
}

static void EndPointBroken(const UBSHcomNetEndpointPtr &ep)
{
    if (syncEp != nullptr) {
        syncEp.Set(nullptr);
    }
    NN_LOG_INFO("end point " << ep->Id());
}

static int RequestReceivedSend(const UBSHcomNetRequestContext &ctx)
{
    std::string respMsg = "Hello client, this is a reply message";

    int result = 0;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(respMsg.c_str())), respMsg.length(), 0);
    if ((result = syncEp->PostSend(0, req)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
    }
    return 0;
}

static int RequestReceivedSendRaw(const UBSHcomNetRequestContext &ctx)
{
    std::string respMsg = "Hello client, this is a reply message";

    int result = 0;
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(respMsg.c_str())), respMsg.length(), 0);
    if ((result = syncEp->PostSendRaw(req, 1)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
    }
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

static void DestoryMem(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
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
    auto result = driver->CreateMemoryRegion(NN_NO16, mr);
    if (result != NN_OK) {
        NN_LOG_ERROR("reg mr failed");
        return false;
    }
    mrInfo[0].lAddress = mr->GetAddress();
    mrInfo[0].lKey = mr->GetLKey();
    mrInfo[0].size = NN_NO8;
    mrs.push_back(mr);
    memset(reinterpret_cast<void *>(mrInfo[0].lAddress), '1', mrInfo[0].size);
    return true;
}

static bool RegReadWriteSglMem(UBSHcomNetDriver *driver, TestRegMrInfo mrInfo[],
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
        memset(reinterpret_cast<void *>(mrInfo[i].lAddress), '1', mrInfo[i].size);
    }
    return true;
}

/* server new request sgl callback */
static TestRegMrInfo syncServerMrInfo[NN_NO4];
static int RequestReceivedServer(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("server request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp((void *)(syncServerMrInfo), sizeof(syncServerMrInfo), 0);
    if ((result = syncEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }

    NN_LOG_INFO("request rsp Mr info");
    std::string readValue((char *)syncServerMrInfo[0].lAddress, syncServerMrInfo[0].size);
    NN_LOG_INFO("idx:" << 0 << " key:" << syncServerMrInfo[0].lKey << " address:" << syncServerMrInfo[0].lAddress <<
        " size: " << syncServerMrInfo[0].size << "string: " << readValue);
    return 0;
}

static int RequestReceivedSglServer(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("server request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp((void *)(syncServerMrInfo), sizeof(syncServerMrInfo), 0);
    if ((result = syncEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }

    NN_LOG_INFO("request rsp Mr info");
    for (uint16_t i = 0; i < NN_NO4; i++) {
        NN_LOG_INFO("idx:" << i << " key:" << syncServerMrInfo[i].lKey << " address:" << syncServerMrInfo[i].lAddress <<
            " size: " << syncServerMrInfo[i].size);
    }
    return 0;
}

/* client receive server mr info */
static TestRegMrInfo getRemoteMrInfo[NN_NO4];

static bool CreateServerDriver(UBSHcomNetDriver *&driver, int (*reqHandler)(const UBSHcomNetRequestContext &),
    UBSHcomNetDriverOptions &options)
{
    auto name = "serverSync_ep_" + std::to_string(g_nameSeed++);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, true);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create serverDriver already created");
        return false;
    }

    options.oobType = ock::hcom::NET_OOB_UDS;
    options.enableTls = false;
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

static bool CreateSyncClientDriver(UBSHcomNetDriver *&driver, UBSHcomNetDriverOptions &options)
{
    auto name = "clientSync_ep_" + std::to_string(g_nameSeed);

    driver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM, name, false);
    if (driver == nullptr) {
        NN_LOG_ERROR("failed to create clientDriver already created");
        return false;
    }

    options.oobType = ock::hcom::NET_OOB_UDS;
    options.dontStartWorkers = true;
    options.enableTls = false;

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

void closeShmDriver(UBSHcomNetDriver *&clientDriver, UBSHcomNetDriver *&serverDriver)
{
    syncEp->Close();
    if (syncEp != nullptr) {
        syncEp.Set(nullptr);
    }
    std::string serverName = serverDriver->Name();
    std::string clientName = clientDriver->Name();
    if (serverDriver->IsStarted()) {
        serverDriver->Stop();
    }
    if (serverDriver->IsInited()) {
        serverDriver->UnInitialize();
    }
    if (clientDriver->IsStarted()) {
        clientDriver->Stop();
    }
    if (clientDriver->IsInited()) {
        clientDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(serverName);
    UBSHcomNetDriver::DestroyInstance(clientName);
}

void TestShmSyncEndpoint::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestShmSyncEndpoint::TearDown()
{
    GlobalMockObject::verify();
}


TEST_F(TestShmSyncEndpoint, SyncPostSendRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(1);
    std::string msg = "Hello Hello";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_OK, result);

    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);

    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    std::string resp((char *)respCtx.Message()->Data(), respCtx.Header().dataLength);
    NN_LOG_INFO("server response received - " << respCtx.Header().opCode << ", dataLen " <<
        respCtx.Header().dataLength);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&ShmChannel::EQEventEnqueue).defaults().will(returnValue(-1));
    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_RETRY_FULL, result);

    UBSHcomNetTransOpInfo innerOpInfo(1, 0, 0, NTH_TWO_SIDE);
    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SH_RETRY_FULL, result);

    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_RETRY_FULL, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

/* set time out */
TEST_F(TestShmSyncEndpoint, SyncPostSendFail2)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(1);
    std::string msg = "Hello Hello";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1));

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

/* no set timeout */
TEST_F(TestShmSyncEndpoint, SyncPostSendFail3)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);
    std::string msg = "Hello Hello";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);

    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .defaults()
        .will(returnObjectList(0, -1));

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendFail4)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);
    std::string msg = "Hello Hello";
    char upctx[NN_NO29];
    for (uint32_t i = 0; i < NN_NO29; ++i) {
        upctx[i] = '2';
    }
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), *upctx);

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendFail5)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);
    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);

    MOCKER_CPP(&ShmDataChannel::TryOccupyWithWait).defaults().will(returnValue(SH_NOT_INITIALIZED));
    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    UBSHcomNetTransOpInfo innerOpInfo(NN_NO2, 0, 0, NTH_TWO_SIDE);
    result = ep->PostSend(1, req, innerOpInfo);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncReceiveRetry1)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::string msg = "Hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    UBSHcomNetResponseContext respCtx {};

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset).defaults().will(returnValue(SH_PARAM_INVALID));
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncReceiveRetry2)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::string msg = "Hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    UBSHcomNetResponseContext respCtx {};

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset).defaults().will(returnValue(SH_NOT_INITIALIZED));
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncReceiveRetry3)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::string msg = "Hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    UBSHcomNetResponseContext respCtx {};

    result = ep->PostSend(1, req);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed).defaults().will(returnValue(false));
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(NN_MALLOC_FAILED, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendRawRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSendRaw, options);
    EXPECT_TRUE(res);
    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::string msg = "Hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_OK, result);

    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);

    UBSHcomNetResponseContext respCtx {};
    result = ep->ReceiveRaw(-1, respCtx);
    std::string resp((char *)respCtx.Message()->Data(), respCtx.Header().dataLength);
    NN_LOG_INFO("server response received - " << respCtx.Header().opCode << ", dataLen " <<
        respCtx.Header().dataLength);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&ShmSyncEndpoint::PostSend)
        .defaults()
        .will(returnObjectList(SH_PARAM_INVALID, SH_SEND_COMPLETION_CALLBACK_FAILURE));
    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    result = ep->PostSendRaw(req, 1);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglRetry)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};

    bool createRes = CreateServerDriver(serverDriver, RequestReceivedSendRaw, options);
    EXPECT_TRUE(createRes);
    createRes = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(createRes);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    bool res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);
    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    UBSHcomNetResponseContext respCtx {};

    result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(-1);
    EXPECT_EQ(SH_OK, result);
    result = ep->ReceiveRawSgl(respCtx);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&ShmSyncEndpoint::PostSendRawSgl)
        .defaults()
        .will(returnObjectList(SH_PARAM_INVALID, SH_SEND_COMPLETION_CALLBACK_FAILURE));
    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglRetry1)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool createRes = CreateServerDriver(serverDriver, RequestReceivedSendRaw, options);
    EXPECT_TRUE(createRes);
    createRes = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(createRes);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(1);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    bool res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    MOCKER_CPP(&ShmChannel::EQEventEnqueue).defaults().will(returnValue(-1));
    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(SH_RETRY_FULL, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}
TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglRetry2)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool createRes = CreateServerDriver(serverDriver, RequestReceivedSendRaw, options);
    EXPECT_TRUE(createRes);
    createRes = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(createRes);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    bool res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    MOCKER_CPP(&ShmSyncEndpoint::FillSglCtx).defaults().will(returnValue(SH_PARAM_INVALID));
    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

/* set time out */
TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglFail1)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(1);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1));

    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

/* no set timeout */
TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglFail2)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    MOCKER_CPP(&ShmQueue<ShmEvent>::EnqueueAndNotify, int32_t(ShmQueue<ShmEvent>::*)(ShmEvent &))
        .defaults()
        .will(returnObjectList(0, -1));

    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(SH_SEND_COMPLETION_CALLBACK_FAILURE, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglFail3)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);
    std::string msg = "Hello Hello";

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, NN_NO29);
    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglFail4)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);
    std::string msg = "Hello Hello";

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, NN_NO16);
    result = ep->PostSendRawSgl(req, 0);
    EXPECT_EQ(NN_INVALID_PARAM, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglFail5)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);
    std::string msg = "Hello Hello";

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, NN_NO16);
    MOCKER_CPP(&UBSHcomNetAtomicState<UBSHcomNetEndPointState>::Compare).defaults().will(returnValue(false));
    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(NN_EP_NOT_ESTABLISHED, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncPostSendRawSglFail6)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);
    ep->DefaultTimeout(0);
    std::string msg = "Hello Hello";

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, NN_NO16);
    MOCKER_CPP(&MemoryRegionChecker::Validate).defaults().will(returnValue(NN_INVALID_LKEY));
    result = ep->PostSendRawSgl(req, NN_NO2);
    EXPECT_EQ(NN_INVALID_LKEY, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncReceiveRawRetry1)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSendRaw, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    UBSHcomNetResponseContext respCtx {};
    result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(-1);
    EXPECT_EQ(SH_OK, result);
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset).defaults().will(returnValue(SH_PARAM_INVALID));
    result = ep->ReceiveRaw(NN_NO2, respCtx);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncReceiveRawRetry2)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    UBSHcomNetResponseContext respCtx {};
    result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset).defaults().will(returnValue(SH_NOT_INITIALIZED));
    result = ep->ReceiveRaw(NN_NO2, respCtx);
    EXPECT_EQ(SH_NOT_INITIALIZED, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncReceiveRawRetry3)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    UBSHcomNetResponseContext respCtx {};
    result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    MOCKER_CPP(&ShmSyncEndpoint::Receive).defaults().will(returnValue(SH_TIME_OUT));
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_TIME_OUT, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, SyncReceiveRawRetry4)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrs;
    res = RegSglMem(clientDriver, clientMrInfo, mrs);
    EXPECT_TRUE(res);

    UBSHcomNetTransSglRequest req(clientMrInfo, iovCnt, 0);
    UBSHcomNetResponseContext respCtx {};
    result = ep->PostSendRawSgl(req, 1);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed, bool (UBSHcomNetMessage::*)(uint32_t))
        .defaults().will(returnValue(false));
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(NN_MALLOC_FAILED, result);

    DestoryMem(clientDriver, mrs);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, PostReadWriteSgl)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSglServer, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteSglMem(serverDriver, syncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteSglMem(clientDriver, syncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = syncClientMrInfo[i].lAddress;
        iov[i].rAddress = getRemoteMrInfo[i].lAddress;
        iov[i].lKey = syncClientMrInfo[i].lKey;
        iov[i].rKey = getRemoteMrInfo[i].lKey;
        iov[i].size = getRemoteMrInfo[i].size;
    }

    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);

    result = ep->PostRead(req);
    EXPECT_EQ(SH_OK, result);

    for (uint16_t i = 0; i < NN_NO4; i++) {
        std::string readValue((char *)syncClientMrInfo[i].lAddress, syncClientMrInfo[i].size);
        NN_LOG_INFO("value[" << i << "]= " << readValue);
    }
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&ShmSyncEndpoint::PostRead,
        HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransSglRequest &, ShmMRHandleMap &))
        .defaults()
        .will(returnObjectList(301, 300));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_PARAM_INVALID, result);
    result = ep->PostRead(req);
    EXPECT_EQ(SH_ERROR, result);

    MOCKER_CPP(&ShmSyncEndpoint::PostWrite,
        HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransSglRequest &, ShmMRHandleMap &))
        .defaults()
        .will(returnObjectList(301, 300));
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_PARAM_INVALID, result);
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_ERROR, result);

    DestoryMem(serverDriver, mrServer);
    DestoryMem(clientDriver, mrClient);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, PostReadWriteSglFail)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSglServer, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteSglMem(serverDriver, syncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteSglMem(clientDriver, syncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = syncClientMrInfo[i].lAddress;
        iov[i].rAddress = getRemoteMrInfo[i].lAddress;
        iov[i].lKey = syncClientMrInfo[i].lKey;
        iov[i].rKey = getRemoteMrInfo[i].lKey;
        iov[i].size = getRemoteMrInfo[i].size;
    }

    UBSHcomNetTransSglRequest req(iov, NN_NO4, NN_NO17);

    result = ep->PostRead(req);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    DestoryMem(serverDriver, mrServer);
    DestoryMem(clientDriver, mrClient);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, GetFromLocalMapFail)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedSglServer, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteSglMem(serverDriver, syncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteSglMem(clientDriver, syncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = syncClientMrInfo[i].lAddress;
        iov[i].rAddress = getRemoteMrInfo[i].lAddress;
        iov[i].lKey = syncClientMrInfo[i].lKey;
        iov[i].rKey = getRemoteMrInfo[i].lKey;
        iov[i].size = getRemoteMrInfo[i].size;
    }

    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);
    ShmHandlePtr localMrHandle = nullptr;
    MOCKER_CPP(&ShmMRHandleMap::GetFromLocalMap).defaults().will(returnValue(localMrHandle));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_ERROR, result);

    DestoryMem(serverDriver, mrServer);
    DestoryMem(clientDriver, mrClient);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, PostReadWrite)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;
    bool res = CreateServerDriver(serverDriver, RequestReceivedServer, options);
    EXPECT_TRUE(res);

    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);

    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(serverDriver, syncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(clientDriver, syncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransRequest req;
    req.lAddress = syncClientMrInfo[0].lAddress;
    req.rAddress = getRemoteMrInfo[0].lAddress;
    req.lKey = syncClientMrInfo[0].lKey;
    req.rKey = getRemoteMrInfo[0].lKey;
    req.size = getRemoteMrInfo[0].size;

    NN_LOG_INFO("req "
        << "req.lAddress: " << req.lAddress << " req.rAddress: " << req.rAddress << " req.lKey: " << req.lKey <<
        " req.rKey: " << req.rKey << " req.size:" << req.size);
    result = ep->PostRead(req);
    EXPECT_EQ(SH_OK, result);

    std::string readValue((char *)syncClientMrInfo[0].lAddress, syncClientMrInfo[0].size);
    NN_LOG_INFO("value[" << 0 << "]= " << readValue);
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_OK, result);

    MOCKER_CPP(&ShmSyncEndpoint::PostRead,
        HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransRequest &, ShmMRHandleMap &))
        .defaults()
        .will(returnObjectList(301, 300));
    result = ep->PostRead(req);
    EXPECT_EQ(SH_PARAM_INVALID, result);
    result = ep->PostRead(req);
    EXPECT_EQ(SH_ERROR, result);

    MOCKER_CPP(&ShmSyncEndpoint::PostWrite,
        HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransRequest &, ShmMRHandleMap &))
        .defaults()
        .will(returnObjectList(301, 300));
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_PARAM_INVALID, result);
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_ERROR, result);

    DestoryMem(serverDriver, mrServer);
    DestoryMem(clientDriver, mrClient);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, PostReadWriteFail)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;

    bool res = CreateServerDriver(serverDriver, RequestReceivedServer, options);
    EXPECT_TRUE(res);
    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);
    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(serverDriver, syncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(clientDriver, syncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());
    uint16_t testUpCtxSize = 18;
    UBSHcomNetTransRequest req;
    req.lAddress = syncClientMrInfo[0].lAddress;
    req.rAddress = getRemoteMrInfo[0].lAddress;
    req.lKey = syncClientMrInfo[0].lKey;
    req.rKey = getRemoteMrInfo[0].lKey;
    req.size = getRemoteMrInfo[0].size;
    req.upCtxSize = testUpCtxSize;
    NN_LOG_INFO("req "
        << "req.lAddress: " << req.lAddress << " req.rAddress: " << req.rAddress << " req.lKey: " << req.lKey <<
        " req.rKey: " << req.rKey << " req.size:" << req.size);

    result = ep->PostWrite(req);
    EXPECT_EQ(SH_PARAM_INVALID, result);

    DestoryMem(serverDriver, mrServer);
    DestoryMem(clientDriver, mrClient);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, GetRemoteMrFdsFail)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};

    bool res = CreateServerDriver(serverDriver, RequestReceivedServer, options);
    EXPECT_TRUE(res);
    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);
    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    res = RegReadWriteMem(serverDriver, syncServerMrInfo, mrServer);
    EXPECT_TRUE(res);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    res = RegReadWriteMem(clientDriver, syncClientMrInfo, mrClient);
    EXPECT_TRUE(res);

    /* exchange mr info */
    std::string msg = "Transfer MrInfo of  the client to the server.";
    UBSHcomNetTransRequest msgReq((void *)(const_cast<char *>(msg.c_str())), msg.length(), 0);
    result = ep->PostSend(1, msgReq);
    EXPECT_EQ(SH_OK, result);
    result = ep->WaitCompletion(NN_NO2);
    EXPECT_EQ(SH_OK, result);
    UBSHcomNetResponseContext respCtx {};
    result = ep->Receive(NN_NO2, respCtx);
    EXPECT_EQ(SH_OK, result);
    memcpy(getRemoteMrInfo, respCtx.Message()->Data(), respCtx.Message()->DataLen());

    UBSHcomNetTransRequest req;
    req.lAddress = syncClientMrInfo[0].lAddress;
    req.rAddress = getRemoteMrInfo[0].lAddress;
    req.lKey = syncClientMrInfo[0].lKey;
    req.rKey = getRemoteMrInfo[0].lKey;
    req.size = getRemoteMrInfo[0].size;

    NN_LOG_INFO("req "
        << "req.lAddress: " << req.lAddress << " req.rAddress: " << req.rAddress << " req.lKey: " << req.lKey <<
        " req.rKey: " << req.rKey << " req.size:" << req.size);

    MOCKER_CPP(&ShmChannel::GetRemoteMrFds).defaults().will(returnValue(SH_TIME_OUT));
    result = ep->PostWrite(req);
    EXPECT_EQ(SH_TIME_OUT, result);

    DestoryMem(serverDriver, mrServer);
    DestoryMem(clientDriver, mrClient);
    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, NetSyncEndpointShmFuncation)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;

    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);
    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);
    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    UBSHcomEpOptions epOptions {};
    result = ep->SetEpOption(epOptions);
    EXPECT_EQ(NN_OK, result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, GetRemoteUdsIdInfo)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;

    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);
    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);
    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    UBSHcomNetUdsIdInfo idInfo {};
    if (syncEp != nullptr) {
        result = syncEp->GetRemoteUdsIdInfo(idInfo);
        EXPECT_EQ(NN_OK, result);
        NN_LOG_INFO("========new endpoint remote uds ids, pid: " << idInfo.pid << " uid: " << idInfo.uid << " gid: " <<
            idInfo.gid << " result:" << result);
    }

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, GetRemoteUdsIdInfoFail1)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;

    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);
    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);
    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    UBSHcomNetUdsIdInfo idInfo {};
    result = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(NN_UDS_ID_INFO_NOT_SUPPORT, result);
    NN_LOG_INFO("=======new endpoint remote uds ids, pid: " << idInfo.pid << " uid: " << idInfo.uid << " gid: " <<
        idInfo.gid << " result:" << result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}

TEST_F(TestShmSyncEndpoint, GetRemoteUdsIdInfoFail2)
{
    UBSHcomNetEndpointPtr ep = nullptr;
    NResult result;
    UBSHcomNetDriver *clientDriver = nullptr;
    UBSHcomNetDriver *serverDriver = nullptr;
    UBSHcomNetDriverOptions options {};
    options.mode = NET_EVENT_POLLING;

    bool res = CreateServerDriver(serverDriver, RequestReceivedSend, options);
    EXPECT_TRUE(res);
    res = CreateSyncClientDriver(clientDriver, options);
    EXPECT_TRUE(res);
    clientDriver->Connect(UDSNAME, 0, "hello server", ep, NET_EP_SELF_POLLING);

    UBSHcomNetUdsIdInfo idInfo {};
    MOCKER_CPP(&UBSHcomNetAtomicState<UBSHcomNetEndPointState>::Compare).defaults().will(returnValue(false));
    result = syncEp->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(NN_EP_NOT_ESTABLISHED, result);
    NN_LOG_INFO("=======new endpoint remote uds ids, pid: " << idInfo.pid << " uid: " << idInfo.uid << " gid: " <<
        idInfo.gid << " result:" << result);

    ep->Close();
    closeShmDriver(clientDriver, serverDriver);
}
