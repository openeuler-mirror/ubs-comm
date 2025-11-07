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
#include <sys/epoll.h>
#include <sys/mman.h>

#include "hcom.h"
#include "net_mem_pool_fixed.h"
#include "openssl_api_wrapper.h"
#include "shm_common.h"
#include "shm_channel.h"
#include "shm_handle.h"
#include "test_shm_common.h"
#include "test_shm_send_recv_msg.h"

using namespace ock::hcom;
TestShmSendRecvMsg::TestShmSendRecvMsg() {}

UBSHcomNetEndpointPtr clientEp = nullptr;
UBSHcomNetEndpointPtr serverEp = nullptr;
UBSHcomNetDriverOptions shmMsgOptions {};
static int port = 8091;
UBSHcomNetDriver *shmMsgServerDriver;
UBSHcomNetDriver *shmMsgClientDriver;
UBSHcomNetTransSgeIov iovPtrMsgServer[4];
UBSHcomNetTransSgeIov iovPtrMsgClient[4];
static int g_nameSeed = 0;
uint32_t fdsLen = 3;


int ShmMsgNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    serverEp = newEP;
    return 0;
}

void ShmMsgEndPointBroken(const UBSHcomNetEndpointPtr &brokenEp)
{
    NN_LOG_INFO("end point " << brokenEp->Id());
}

int ShmMsgRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);
    return 0;
}

int ShmMsgRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted");
    return 0;
}


int ShmMsgOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done");
    return 0;
}


void SetShmCallBack(UBSHcomNetDriver *driver)
{
    driver->RegisterNewEPHandler(
        std::bind(&ShmMsgNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    driver->RegisterEPBrokenHandler(std::bind(&ShmMsgEndPointBroken, std::placeholders::_1));
    driver->RegisterNewReqHandler(std::bind(&ShmMsgRequestReceived, std::placeholders::_1));
    driver->RegisterReqPostedHandler(std::bind(&ShmMsgRequestPosted, std::placeholders::_1));
    driver->RegisterOneSideDoneHandler(std::bind(&ShmMsgOneSideDone, std::placeholders::_1));
}

bool MsgRegisterShmMemory(UBSHcomNetDriver *driver, UBSHcomNetTransSgeIov iovs[],
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

static void DestorySglMem(UBSHcomNetDriver *driver, std::vector<UBSHcomNetMemoryRegionPtr> &mrs)
{
    while (!mrs.empty()) {
        driver->DestroyMemoryRegion(mrs.back());
        mrs.pop_back();
    }
}

void CreateFds(int shmFds[])
{
    for (uint32_t i = 0; i < fdsLen; i++) {
        std::string name = "example_shm_fd_" + std::to_string(i);
        auto tmpFd = shm_open(name.c_str(), O_CREAT | O_RDWR, 0755);
        if (tmpFd < 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to create shm file error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE)););
        }
        shmFds[i] = tmpFd;
    }
}

void DestoryFds(int outFds[])
{
    NN_LOG_INFO("Destory fds len:" << fdsLen << " fds[0]:" << outFds[0] << " fds[1]:" << outFds[1] << " fds[2]:" <<
        outFds[2]);

    for (uint32_t i = 0; i < fdsLen; i++) {
        auto mappedAddress = mmap(nullptr, 10, PROT_READ | PROT_WRITE, MAP_SHARED, outFds[i], 0);
        if (mappedAddress == MAP_FAILED) {
            close(outFds[i]);
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to mmap file error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE)););
        }
        NN_LOG_INFO("shm map fds:" << outFds[i]);

        close(outFds[i]);
        outFds[i] = -1;
    }
}

void TestShmSendRecvMsg::SetUp()
{
    shmMsgOptions.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    shmMsgOptions.SetNetDeviceIpMask(IP_SEG);
    shmMsgOptions.pollingBatchSize = 16;
    shmMsgOptions.SetWorkerGroups("1");
    shmMsgOptions.SetWorkerGroupsCpuSet("1-1");
    shmMsgOptions.dontStartWorkers = false;
    shmMsgOptions.oobType = ock::hcom::NET_OOB_UDS;
    shmMsgOptions.enableTls = false;

    shmMsgServerDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM,
        "shm_msg" + std::to_string(g_nameSeed++), true);
    UBSHcomNetOobUDSListenerOptions listenOpt;
    listenOpt.Name(UDSNAME);
    listenOpt.perm = 0;
    shmMsgServerDriver->AddOobUdsOptions(listenOpt);

    shmMsgClientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::SHM,
        "shm_msg" + std::to_string(g_nameSeed++), false);
    shmMsgServerDriver->OobIpAndPort(BASE_IP, port);
    shmMsgClientDriver->OobIpAndPort(BASE_IP, port++);
    SetShmCallBack(shmMsgServerDriver);
    SetShmCallBack(shmMsgClientDriver);
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestShmSendRecvMsg::TearDown()
{
    clientEp->Close();
    serverEp->Close();
    if (serverEp != nullptr) {
        serverEp.Set(nullptr);
    }
    if (clientEp != nullptr) {
        clientEp.Set(nullptr);
    }
    std::string serverName = shmMsgServerDriver->Name();
    std::string clientName = shmMsgClientDriver->Name();
    if (shmMsgClientDriver->IsStarted()) {
        shmMsgClientDriver->Stop();
    }
    if (shmMsgClientDriver->IsInited()) {
        shmMsgClientDriver->UnInitialize();
    }

    if (shmMsgServerDriver->IsStarted()) {
        shmMsgServerDriver->Stop();
    }
    if (shmMsgServerDriver->IsInited()) {
        shmMsgServerDriver->UnInitialize();
    }
    UBSHcomNetDriver::DestroyInstance(serverName);
    UBSHcomNetDriver::DestroyInstance(clientName);
    GlobalMockObject::verify();
}

TEST_F(TestShmSendRecvMsg, SendMsgRecvMsgSuccess)
{
    shmMsgServerDriver->Initialize(shmMsgOptions);
    shmMsgServerDriver->Start();
    shmMsgClientDriver->Initialize(shmMsgOptions);
    shmMsgClientDriver->Start();
    NResult result = shmMsgClientDriver->Connect(UDSNAME, 0, "halo", clientEp);
    int shmFds[3];
    int outFds[3];
    CreateFds(shmFds);
    result = clientEp->SendFds(shmFds, fdsLen);
    EXPECT_EQ(NNCode::NN_OK, result);
    result = serverEp->ReceiveFds(outFds, fdsLen, 1);
    EXPECT_EQ(NNCode::NN_OK, result);
    DestoryFds(outFds);
}

TEST_F(TestShmSendRecvMsg, SendMsgFail1)
{
    shmMsgServerDriver->Initialize(shmMsgOptions);
    shmMsgServerDriver->Start();
    shmMsgClientDriver->Initialize(shmMsgOptions);
    shmMsgClientDriver->Start();
    NResult result = shmMsgClientDriver->Connect(UDSNAME, 0, "halo", clientEp);
    int shmFds[3];
    int outFds[3];
    CreateFds(shmFds);
    result = clientEp->SendFds(shmFds, NN_NO5);
    EXPECT_EQ(NN_PARAM_INVALID, result);

    DestoryFds(shmFds);
}

TEST_F(TestShmSendRecvMsg, SendMsgFail2)
{
    shmMsgServerDriver->Initialize(shmMsgOptions);
    shmMsgServerDriver->Start();
    shmMsgClientDriver->Initialize(shmMsgOptions);
    shmMsgClientDriver->Start();
    NResult result = shmMsgClientDriver->Connect(UDSNAME, 0, "halo", clientEp);
    int shmFds[3];

    result = clientEp->SendFds(shmFds, fdsLen);
    EXPECT_EQ(NN_INVALID_PARAM, result);
}

TEST_F(TestShmSendRecvMsg, SendMsgFail3)
{
    shmMsgServerDriver->Initialize(shmMsgOptions);
    shmMsgServerDriver->Start();
    shmMsgClientDriver->Initialize(shmMsgOptions);
    shmMsgClientDriver->Start();
    NResult result = shmMsgClientDriver->Connect(UDSNAME, 0, "halo", clientEp);
    int shmFds[3];
    CreateFds(shmFds);
    MOCKER_CPP(&UBSHcomNetAtomicState<UBSHcomNetEndPointState>::Compare).defaults().will(returnValue(false));
    result = clientEp->SendFds(shmFds, fdsLen);
    EXPECT_EQ(NN_EP_NOT_ESTABLISHED, result);

    DestoryFds(shmFds);
}

TEST_F(TestShmSendRecvMsg, RecvMsgFail1)
{
    shmMsgServerDriver->Initialize(shmMsgOptions);
    shmMsgServerDriver->Start();
    shmMsgClientDriver->Initialize(shmMsgOptions);
    shmMsgClientDriver->Start();
    NResult result = shmMsgClientDriver->Connect(UDSNAME, 0, "halo", clientEp);
    int shmFds[3];
    int outFds[3];
    CreateFds(shmFds);
    result = clientEp->SendFds(shmFds, fdsLen);
    EXPECT_EQ(NNCode::NN_OK, result);
    result = serverEp->ReceiveFds(outFds, NN_NO5, 1);
    EXPECT_EQ(NN_PARAM_INVALID, result);

    DestoryFds(shmFds);
}

TEST_F(TestShmSendRecvMsg, RecvMsgFail2)
{
    shmMsgServerDriver->Initialize(shmMsgOptions);
    shmMsgServerDriver->Start();
    shmMsgClientDriver->Initialize(shmMsgOptions);
    shmMsgClientDriver->Start();
    NResult result = shmMsgClientDriver->Connect(UDSNAME, 0, "halo", clientEp);
    int shmFds[3];
    int outFds[3];
    CreateFds(shmFds);
    result = clientEp->SendFds(shmFds, fdsLen);
    EXPECT_EQ(NNCode::NN_OK, result);
    MOCKER_CPP(&UBSHcomNetAtomicState<UBSHcomNetEndPointState>::Compare).defaults().will(returnValue(false));
    result = serverEp->ReceiveFds(outFds, fdsLen, 1);
    EXPECT_EQ(NN_EP_NOT_ESTABLISHED, result);
    DestoryFds(shmFds);
}

TEST_F(TestShmSendRecvMsg, ReadWriteSglSuccess)
{
    shmMsgClientDriver->RegisterOneSideDoneHandler(std::bind(&ShmMsgOneSideDone, std::placeholders::_1));
    shmMsgServerDriver->RegisterOneSideDoneHandler(std::bind(&ShmMsgOneSideDone, std::placeholders::_1));
    shmMsgServerDriver->Initialize(shmMsgOptions);
    shmMsgServerDriver->Start();
    shmMsgClientDriver->Initialize(shmMsgOptions);
    shmMsgClientDriver->Start();
    shmMsgClientDriver->Connect(UDSNAME, 0, "halo", clientEp);
    std::vector<UBSHcomNetMemoryRegionPtr> mrServer;
    MsgRegisterShmMemory(shmMsgServerDriver, iovPtrMsgServer, mrServer);
    std::vector<UBSHcomNetMemoryRegionPtr> mrClient;
    MsgRegisterShmMemory(shmMsgClientDriver, iovPtrMsgClient, mrClient);
    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < 4; i++) {
        iov[i].lAddress = iovPtrMsgClient[i].lAddress;
        iov[i].rAddress = iovPtrMsgServer[i].lAddress;
        iov[i].lKey = iovPtrMsgClient[i].lKey;
        iov[i].rKey = iovPtrMsgServer[i].lKey;
        iov[i].size = NN_NO4;
    }
    UBSHcomNetTransSglRequest req(iov, NN_NO4, 0);
    req.upCtxSize = NN_NO16;
    for (auto i = 0; i < 16; i++) {
        req.upCtxData[i] = 'a';
    }
    MOCKER_CPP(&ShmChannel::AddMrFd, HResult(ShmChannel::*)(int)).defaults().will(returnValue(315));
    HResult result = clientEp->PostRead(req);
    EXPECT_EQ(SH_TIME_OUT, result);

    DestorySglMem(shmMsgServerDriver, mrServer);
    DestorySglMem(shmMsgClientDriver, mrClient);
}
