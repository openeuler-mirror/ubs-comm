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
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <sys/epoll.h>
#include <sys/mman.h>

#include "hcom.h"
#include "net_mem_pool_fixed.h"
#include "openssl_api_wrapper.h"
#include "shm_common.h"
#include "shm_handle.h"
#include "shm_composed_endpoint.h"
#include "shm_mr_pool.h"
#include "test_shm_common.h"
#include "test_shm_driver_oob.h"

using namespace ock::hcom;
TestShmDriverOob::TestShmDriverOob() {}

UBSHcomNetEndpointPtr shmEp = nullptr;
UBSHcomNetDriverOptions shmOptions {};
static int port = 8091;
UBSHcomNetDriver *shmServerDriver;
UBSHcomNetDriver *shmClientDriver;
UBSHcomNetTransSgeIov iovPtrShmServer[4];
UBSHcomNetTransSgeIov iovPtrShmClient[4];
static int g_nameSeed = 0;

int shmOobNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort);
    shmEp = newEP;
    return 0;
}

void shmOobEndPointBroken(const UBSHcomNetEndpointPtr &brokenEp)
{
    NN_LOG_INFO("end point " << brokenEp->Id());
}

int shmOobRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);
    return 0;
}

int shmOobRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted");
    return 0;
}


int shmOobOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done");
    return 0;
}


void SetCallBack(UBSHcomNetDriver *driver)
{
    driver->RegisterNewEPHandler(
        std::bind(&shmOobNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterEPBrokenHandler(std::bind(&shmOobEndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(std::bind(&shmOobRequestReceived, std::placeholders::_1));
    driver->RegisterReqPostedHandler(std::bind(&shmOobRequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&shmOobOneSideDone, std::placeholders::_1));
}

bool RegisterShmMemory(UBSHcomNetDriver *driver, UBSHcomNetTransSgeIov iovs[],
    std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    for (int i = 0; i < 4; i++) {
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
        mrs.push_back(mr);
        memset(reinterpret_cast<void *>(iov.lAddress), 0, iov.size);
    }
    return true;
}

static void DestoryShmMemory(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    while (!mrs.empty()) {
        driver->DestroyMemoryRegion(mrs.back());
        mrs.pop_back();
    }
}

void TestShmDriverOob::SetUp()
{
    shmOptions.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    shmOptions.SetNetDeviceIpMask(IP_SEG);
    shmOptions.pollingBatchSize = 16;
    shmOptions.SetWorkerGroups("1");
    shmOptions.SetWorkerGroupsCpuSet("1-1");
    shmOptions.dontStartWorkers = false;
    shmOptions.oobType = ock::hcom::NET_OOB_UDS;
    shmOptions.enableTls = false;

    shmServerDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM,
        "shm_oob" + std::to_string(g_nameSeed++), true);
    UBSHcomNetOobUDSListenerOptions listenOpt;
    listenOpt.Name(UDSNAME);
    listenOpt.perm = 0;
    shmServerDriver->AddOobUdsOptions(listenOpt);

    shmClientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM,
        "shm_oob" + std::to_string(g_nameSeed++), false);
    shmServerDriver->OobIpAndPort(BASE_IP, port);
    shmClientDriver->OobIpAndPort(BASE_IP, port++);
    SetCallBack(shmServerDriver);
    SetCallBack(shmClientDriver);
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestShmDriverOob::TearDown()
{
    if (shmEp != nullptr) {
        shmEp.Set(nullptr);
    }
    std::string serverName = shmServerDriver->Name();
    std::string clientName = shmClientDriver->Name();
    if (shmServerDriver->IsStarted()) {
        shmServerDriver->Stop();
    }
    if (shmServerDriver->IsInited()) {
        shmServerDriver->UnInitialize();
    }
    if (shmClientDriver->IsStarted()) {
        shmClientDriver->Stop();
    }
    if (shmClientDriver->IsInited()) {
        shmClientDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(serverName);
    UBSHcomNetDriver::DestroyInstance(clientName);

    GlobalMockObject::verify();
}

TEST_F(TestShmDriverOob, InitSuccess)
{
    NResult result = shmServerDriver->Initialize(shmOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
    shmServerDriver->UnInitialize();
}

TEST_F(TestShmDriverOob, InitSuccessTwice)
{
    shmServerDriver->Initialize(shmOptions);
    NResult result = shmServerDriver->Initialize(shmOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestShmDriverOob, InitSuccessWithBusyPolling)
{
    shmOptions.mode = UBSHcomNetDriverWorkingMode::NET_BUSY_POLLING;
    NResult result = shmServerDriver->Initialize(shmOptions);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestShmDriverOob, InitFailWithInvaildParam)
{
    shmOptions.qpSendQueueSize = 4;
    NResult result = shmServerDriver->Initialize(shmOptions);
    shmOptions.qpSendQueueSize = NN_NO256;
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestShmDriverOob, InitSuccessWithoutSetWorkGroup)
{
    shmOptions.SetWorkerGroups("");
    NResult result = shmServerDriver->Initialize(shmOptions);
    shmOptions.SetWorkerGroups("1");
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestShmDriverOob, InitFailWithFailToInitWorker)
{
    MOCKER((int(*)(int))syscall).defaults().will(returnValue(-1));
    NResult result = shmServerDriver->Initialize(shmOptions);
    EXPECT_EQ(ShCode::SH_PARAM_INVALID, result);
}

TEST_F(TestShmDriverOob, ConnectFailWithCreateChannelFail)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    MOCKER((int(*)(int))syscall).defaults().will(returnValue(-1));
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp);
    EXPECT_EQ(ShCode::SH_FILE_OP_FAILED, result);
}

TEST_F(TestShmDriverOob, InitFailWithWorkGroupHasZeroWorker)
{
    shmOptions.SetWorkerGroups("0");
    NResult result = shmServerDriver->Initialize(shmOptions);
    shmOptions.SetWorkerGroups("1");
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestShmDriverOob, StartFailWithStartOobServerFail)
{
    shmServerDriver->Initialize(shmOptions);
    MOCKER(::socket).defaults().will(returnValue(-1));
    NResult result = shmServerDriver->Start();
    EXPECT_EQ(NNCode::NN_OOB_LISTEN_SOCKET_ERROR, result);
}

/*  CreateMemoryRegion  */
TEST_F(TestShmDriverOob, CreateMemoryRegionSuccess)
{
    shmServerDriver->Initialize(shmOptions);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = shmServerDriver->CreateMemoryRegion(16, mr);
    EXPECT_EQ(NNCode::NN_OK, result);
    shmServerDriver->DestroyMemoryRegion(mr);
}

TEST_F(TestShmDriverOob, DestoryMemoryRegionSuccess)
{
    shmServerDriver->Initialize(shmOptions);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = shmServerDriver->CreateMemoryRegion(16, mr);
    EXPECT_EQ(NNCode::NN_OK, result);
    shmServerDriver->DestroyMemoryRegion(mr);
}

TEST_F(TestShmDriverOob, CreateMemoryRegionFail)
{
    shmServerDriver->Initialize(shmOptions);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = shmServerDriver->CreateMemoryRegion(0, mr);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestShmDriverOob, CreateMemoryRegionInitializeFail)
{
    shmServerDriver->Initialize(shmOptions);
    UBSHcomNetMemoryRegionPtr mr;
    MOCKER_CPP(&ShmHandle::Initialize).defaults().will(returnValue(301));
    NResult result = shmServerDriver->CreateMemoryRegion(16, mr);
    EXPECT_EQ(NNCode::NN_NOT_INITIALIZED, result);
}

TEST_F(TestShmDriverOob, CreateMemoryRegionMrHandleZero)
{
    shmServerDriver->Initialize(shmOptions);
    UBSHcomNetMemoryRegionPtr mr;
    uintptr_t mAddress = 0;
    MOCKER_CPP(&ShmHandle::ShmAddress).defaults().will(returnValue(mAddress));
    NResult result = shmServerDriver->CreateMemoryRegion(16, mr);
    EXPECT_EQ(NNCode::NN_MALLOC_FAILED, result);
}

TEST_F(TestShmDriverOob, DestoryMemoryRegionFail)
{
    shmServerDriver->Initialize(shmOptions);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = shmServerDriver->CreateMemoryRegion(0, mr);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
    shmServerDriver->DestroyMemoryRegion(mr);
}

TEST_F(TestShmDriverOob, CreateMemoryRegionWithAddressFail)
{
    shmServerDriver->Initialize(shmOptions);
    auto tmpBuf = memalign(NN_NO4096, 10);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = shmServerDriver->CreateMemoryRegion(reinterpret_cast<uintptr_t>(tmpBuf), 16, mr);
    EXPECT_EQ(NNCode::NN_INVALID_OPERATION, result);
    free(tmpBuf);
}

TEST_F(TestShmDriverOob, CreateMemoryRegionWithAddressFailWithAddressIsZero)
{
    shmServerDriver->Initialize(shmOptions);
    UBSHcomNetMemoryRegionPtr mr;
    NResult result = shmServerDriver->CreateMemoryRegion(0, 16, mr);
    EXPECT_EQ(NNCode::NN_INVALID_OPERATION, result);
}

/* connect */
TEST_F(TestShmDriverOob, ConnectSuccess)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestShmDriverOob, ConnectTcpFail)
{
    shmOptions.oobType = ock::hcom::NET_OOB_TCP;
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    NResult result = shmClientDriver->Connect("halo", shmEp);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestShmDriverOob, ConnectFailWithoutInit)
{
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp);
    EXPECT_EQ(NNCode::NN_NOT_INITIALIZED, result);
}

TEST_F(TestShmDriverOob, ConnectFailWithPayloadOversize)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    char payload[1030];
    for (char &i : payload) {
        i = '1';
    }
    payload[1029] = '\0';
    NResult result = shmClientDriver->Connect(UDSNAME, 0, payload, shmEp);
    EXPECT_EQ(NNCode::NN_INVALID_PARAM, result);
}

TEST_F(TestShmDriverOob, ConnectFail)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    MOCKER(::connect).defaults().will(returnValue(-1));
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp);
    EXPECT_EQ(NNCode::NN_OOB_CLIENT_SOCKET_ERROR, result);
}

TEST_F(TestShmDriverOob, ConnectFailWithMagicMismatch)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmOptions.magic = 104;
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp);
    shmOptions.magic = NN_NO256;
    EXPECT_EQ(NNCode::NN_CONNECT_REFUSED, result);
}

TEST_F(TestShmDriverOob, ConnectCreateShmHandleFail)
{
    ShmHandle *tmpHandle = nullptr;
    MOCKER_CPP(&NetRef<ShmHandle>::Get).defaults().will(returnValue(tmpHandle));
    NResult result = shmServerDriver->Initialize(shmOptions);
    EXPECT_EQ(SH_NEW_OBJECT_FAILED, result);
}

TEST_F(TestShmDriverOob, ConnectCreateEventQueueFail)
{
    ShmEventQueue *tmpQueue = nullptr;
    MOCKER_CPP(&NetRef<ShmEventQueue>::Get).defaults().will(returnValue(tmpQueue));
    NResult result = shmServerDriver->Initialize(shmOptions);
    EXPECT_EQ(SH_NEW_OBJECT_FAILED, result);
}

/* sync connect */
TEST_F(TestShmDriverOob, ConnectSyncSuccess)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "hello world", shmEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestShmDriverOob, ConnectSyncFail1)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    MOCKER(::connect).defaults().will(returnValue(-1));
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "hello world", shmEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_OOB_CLIENT_SOCKET_ERROR, result);
}

TEST_F(TestShmDriverOob, ConnectSyncFailWithMagicMismatch)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmOptions.magic = 104;
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_CONNECT_REFUSED, result);
}

TEST_F(TestShmDriverOob, ConnectSyncWithCreateFail)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    ShmSyncEndpoint *tmpEp = nullptr;
    MOCKER_CPP(&NetRef<ShmSyncEndpoint>::Get).defaults().will(returnValue(tmpEp));
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "hello world", shmEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(SH_NEW_OBJECT_FAILED, result);
}

TEST_F(TestShmDriverOob, ConnectSyncCreateShmHandleFail)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    ShmHandle *tmpHandle = nullptr;
    MOCKER_CPP(&NetRef<ShmHandle>::Get).defaults().will(returnValue(tmpHandle));
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "hello world", shmEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(SH_NEW_OBJECT_FAILED, result);
}

TEST_F(TestShmDriverOob, ConnectSyncCreateEventQueueFail)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    ShmEventQueue *tmpQueue = nullptr;
    MOCKER_CPP(&NetRef<ShmEventQueue>::Get).defaults().will(returnValue(tmpQueue));
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "hello world", shmEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(SH_NEW_OBJECT_FAILED, result);
}

TEST_F(TestShmDriverOob, SendSuccess)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp);
    static char data[900] = {};
    UBSHcomNetTransRequest req((void *)(data), sizeof(data), 0);
    req.upCtxSize = NN_NO16;
    for (auto i = 0; i < 16; i++) {
        req.upCtxData[i] = 'a';
    }
    NResult result = shmEp->PostSend(1, req);
    EXPECT_EQ(NNCode::NN_OK, result);
}

TEST_F(TestShmDriverOob, SendRawSglSuccess)
{
    shmServerDriver->RegisterReqPostedHandler(std::bind(&shmOobRequestPosted, std::placeholders::_1));
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    shmClientDriver->Connect(UDSNAME, 0, "halo", shmEp);

    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    RegisterShmMemory(shmServerDriver, iovPtrShmServer, mrServer);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    RegisterShmMemory(shmClientDriver, iovPtrShmClient, mrClient);
    UBSHcomNetTransSgeIov iov[4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = iovPtrShmClient[i].lAddress;
        iov[i].rAddress = iovPtrShmServer[i].lAddress;
        iov[i].lKey = iovPtrShmClient[i].lKey;
        iov[i].rKey = iovPtrShmServer[i].lKey;
        iov[i].size = NN_NO4;
    }
    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);
    req.upCtxSize = NN_NO16;
    for (auto i = 0; i < 16; i++) {
        req.upCtxData[i] = 'a';
    }
    NResult result = shmEp->PostSendRawSgl(req, 1);
    EXPECT_EQ(NNCode::NN_OK, result);

    DestoryShmMemory(shmServerDriver, mrServer);
    DestoryShmMemory(shmClientDriver, mrClient);
}

TEST_F(TestShmDriverOob, DestoryEp)
{
    shmServerDriver->Initialize(shmOptions);
    shmServerDriver->Start();
    shmClientDriver->Initialize(shmOptions);
    shmClientDriver->Start();
    NResult result = shmClientDriver->Connect(UDSNAME, 0, "hello world", shmEp, NET_EP_SELF_POLLING);
    EXPECT_EQ(NNCode::NN_OK, result);
    shmClientDriver->DestroyEndpoint(shmEp);
}

TEST_F(TestShmDriverOob, DestoryEpFail)
{
    UBSHcomNetEndpointPtr shmEp2 = nullptr;
    shmClientDriver->DestroyEndpoint(shmEp2);
}
