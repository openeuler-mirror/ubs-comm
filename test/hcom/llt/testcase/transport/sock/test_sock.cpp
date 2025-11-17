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
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "hcom.h"
#include "ut_helper.h"
#include "test_sock.h"

using namespace ock::hcom;
TestCaseSock::TestCaseSock() {}

void TestCaseSock::SetUp()
{
    setenv("HCOM_CONNECTION_RETRY_TIMES", "1", 1);
}

void TestCaseSock::TearDown()
{
    GlobalMockObject::verify();
}

static std::string ipSeg = IP_SEG;
using TestRegMrInfo = struct _reg_sgl_info_test_ {
    uintptr_t lAddress = 0;
    uint32_t lKey = 0;
    uint32_t size = 0;
} __attribute__((packed));

#define SOCK_CHECK_RESULT_TRUE(result)   \
    EXPECT_EQ(true, (result));           \
    if (!(result)) {                     \
        return;                          \
    }

static UBSHcomNetDriver *sockServerDriver = nullptr;
static UBSHcomNetEndpointPtr sockServerEp = nullptr;
constexpr uint64_t SOCK_PORT = 9925;

static TestRegMrInfo localMrInfo[4];

int SockServerNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEP, const std::string &payload)
{
    NN_LOG_INFO("new endpoint from " << ipPort << " payload " << payload);
    sockServerEp = newEP;
    return 0;
}

void SockServerEndPointBroken(const UBSHcomNetEndpointPtr &sockServerEp1)
{
    NN_LOG_INFO("end point " << sockServerEp1->Id());
    if (sockServerEp != nullptr) {
        sockServerEp.Set(nullptr);
    }
}

int SockServerRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request received - " << ctx.Header().opCode << ", dataLen " << ctx.Header().dataLength);

    int result = 0;
    UBSHcomNetTransRequest rsp((void *)(localMrInfo), sizeof(localMrInfo), 0);
    if ((result = sockServerEp->PostSend(1, rsp)) != 0) {
        NN_LOG_ERROR("failed to post message to data to server, result " << result);
        return result;
    }

    NN_LOG_INFO("request rsp Mr info");
    for (uint16_t i = 0; i < 4; i++) {
        NN_LOG_TRACE_INFO("idx:" << i << " key:" << localMrInfo[i].lKey << " address:" << localMrInfo[i].lAddress <<
            " size" << localMrInfo[i].size);
    }
    return 0;
}

int SockServerRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("request posted");
    return 0;
}

int SockServerOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    NN_LOG_INFO("one side done");
    return 0;
}


bool SockServerCreateDriver()
{
    if (sockServerDriver != nullptr) {
        NN_LOG_ERROR("sockServerDriver already created");
        return false;
    }

    sockServerDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::UDS, "sockServer", true);
    if (sockServerDriver == nullptr) {
        NN_LOG_ERROR("failed to create sockServerDriver already created");
        return false;
    }

    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.enableTls = false;
    options.SetNetDeviceIpMask(ipSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    sockServerDriver->RegisterNewEPHandler(
        std::bind(&SockServerNewEndPoint, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    sockServerDriver->RegisterEPBrokenHandler(std::bind(&SockServerEndPointBroken, std::placeholders::_1));
    sockServerDriver->RegisterNewReqHandler(std::bind(&SockServerRequestReceived, std::placeholders::_1));
    sockServerDriver->RegisterReqPostedHandler(std::bind(&SockServerRequestPosted, std::placeholders::_1));
    sockServerDriver->RegisterOneSideDoneHandler(std::bind(&SockServerOneSideDone, std::placeholders::_1));

    sockServerDriver->OobIpAndPort(BASE_IP, SOCK_PORT);

    int result = 0;
    if ((result = sockServerDriver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize sockServerDriver " << result);
        return false;
    }
    NN_LOG_ERROR("sockServerDriver initialized");

    if ((result = sockServerDriver->Start()) != 0) {
        NN_LOG_ERROR("failed to start sockServerDriver " << result);
        return false;
    }
    NN_LOG_ERROR("sockServerDriver started");

    return true;
}

bool SockServerRegSglMem()
{
    for (uint16_t i = 0; i < 4; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = sockServerDriver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        localMrInfo[i].lAddress = mr->GetAddress();
        localMrInfo[i].lKey = mr->GetLKey();
        localMrInfo[i].size = NN_NO16;
        memset(reinterpret_cast<void *>(localMrInfo[i].lAddress), 0, NN_NO16);
    }

    return true;
}

// client
static UBSHcomNetDriver *sockClientDriver = nullptr;
static UBSHcomNetEndpointPtr sockClientEp = nullptr;

static TestRegMrInfo ClientLocalMrInfo[NN_NO4];
static TestRegMrInfo remoteMrInfo[NN_NO4];
static sem_t sem;

void SockClientEndPointBroken(const UBSHcomNetEndpointPtr &sockClientEp1)
{
    NN_LOG_INFO("end point " << sockClientEp1->Id() << " broken");
    if (sockClientEp != nullptr) {
        sockClientEp.Set(nullptr);
    }
}

int SockClientRequestReceived(const UBSHcomNetRequestContext &ctx)
{
    memcpy(remoteMrInfo, ctx.Message()->Data(), ctx.Message()->DataLen());
    NN_LOG_INFO("get remote Mr info");
    for (uint16_t i = 0; i < NN_NO4; i++) {
        NN_LOG_TRACE_INFO("idx:" << i << " key:" << remoteMrInfo[i].lKey << " address:" << remoteMrInfo[i].lAddress <<
            " size" << remoteMrInfo[i].size);
    }

    sem_post(&sem);
    return 0;
}

int SockClientRequestPosted(const UBSHcomNetRequestContext &ctx)
{
    return 0;
}

int SockClientOneSideDone(const UBSHcomNetRequestContext &ctx)
{
    sem_post(&sem);
    return 0;
}

bool SockClientCreateDriver()
{
    if (sockClientDriver != nullptr) {
        NN_LOG_ERROR("sockClientDriver already created");
        return false;
    }

    sockClientDriver = UBSHcomNetDriver::Instance(UBSHcomNetDriverProtocol::UDS, "sockClient", false);
    if (sockClientDriver == nullptr) {
        NN_LOG_ERROR("failed to create sockClientDriver already created");
        return false;
    }

    UBSHcomNetDriverOptions options {};
    options.mode = UBSHcomNetDriverWorkingMode::NET_EVENT_POLLING;
    options.enableTls = false;
    options.SetNetDeviceIpMask(ipSeg);
    NN_LOG_INFO("set ip mask " << options.netDeviceIpMask);

    sockClientDriver->RegisterEPBrokenHandler(std::bind(&SockClientEndPointBroken, std::placeholders::_1));
    sockClientDriver->RegisterNewReqHandler(std::bind(&SockClientRequestReceived, std::placeholders::_1));
    sockClientDriver->RegisterReqPostedHandler(std::bind(&SockClientRequestPosted, std::placeholders::_1));
    sockClientDriver->RegisterOneSideDoneHandler(std::bind(&SockClientOneSideDone, std::placeholders::_1));

    sockClientDriver->OobIpAndPort(BASE_IP, SOCK_PORT);

    int result = 0;
    if ((result = sockClientDriver->Initialize(options)) != 0) {
        NN_LOG_ERROR("failed to initialize sockClientDriver " << result);
        return false;
    }
    NN_LOG_ERROR("sockClientDriver initialized");

    if ((result = sockClientDriver->Start()) != 0) {
        NN_LOG_ERROR("failed to start sockClientDriver " << result);
        return false;
    }
    NN_LOG_ERROR("sockClientDriver started");

    return true;
}

bool SockClientConnect()
{
    if (sockClientDriver == nullptr) {
        NN_LOG_ERROR("sockClientDriver is null");
        return false;
    }

    int result = 0;
    if ((result = sockClientDriver->Connect("hello world", sockClientEp, 0)) != 0) {
        NN_LOG_ERROR("failed to connect to server, result " << result);
        return false;
    }

    sem_init(&sem, 0, 0);

    std::string value = "hello world";
    UBSHcomNetTransRequest req((void *)(const_cast<char *>(value.c_str())), value.length(), 0);
    if ((result = sockClientEp->PostSend(1, req)) != 0) {
        NN_LOG_INFO("failed to post message to data to server");
        return false;
    }

    sem_wait(&sem);
    return true;
}

void SockSendSingleRequest(UBSHcomNetTransSgeIov *iov, uint64_t index)
{
    int result = 0;

    UBSHcomNetTransSglRequest reqRead(iov, NN_NO4, 0);
    result = sockClientEp->PostRead(reqRead);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_INFO("failed to read data from server");
        return;
    }
    sem_wait(&sem);
    for (uint16_t i = 0; i < NN_NO4; i++) {
        uint64_t *readValue = reinterpret_cast<uint64_t *>((void *)(ClientLocalMrInfo[i].lAddress));
        uint64_t value = *readValue;
        NN_LOG_TRACE_INFO("value[" << i << "]=" << *readValue);
        EXPECT_EQ(value, index);
        *readValue = ++value;
    }

    UBSHcomNetTransSglRequest reqWrite(iov, NN_NO4, 0);
    result = sockClientEp->PostWrite(reqWrite);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_INFO("failed to read data from server");
        return;
    }
    sem_wait(&sem);

    UBSHcomNetTransRequest buffReq(iov[0].lAddress, iov[0].rAddress, iov[0].lKey, iov[0].rKey, iov[0].size, 0);
    result = sockClientEp->PostRead(buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_INFO("failed to read data from server");
        return;
    }
    sem_wait(&sem);
    uint64_t *readBuff = reinterpret_cast<uint64_t *>((void *)(iov[0].lAddress));
    uint64_t readValue = *readBuff;
    EXPECT_EQ(readValue, index + 1);

    result = sockClientEp->PostWrite(buffReq);
    EXPECT_EQ(result, 0);
    if (result != 0) {
        NN_LOG_INFO("failed to read data from server");
        return;
    }
    sem_wait(&sem);
}

void SockSendRequest()
{
    UBSHcomNetTransSgeIov iov[NN_NO4];
    for (uint16_t i = 0; i < NN_NO4; i++) {
        iov[i].lAddress = ClientLocalMrInfo[i].lAddress;
        iov[i].rAddress = remoteMrInfo[i].lAddress;
        iov[i].lKey = ClientLocalMrInfo[i].lKey;
        iov[i].rKey = remoteMrInfo[i].lKey;
        iov[i].size = NN_NO8;
    }
    sem_init(&sem, 0, 0);
    for (int i = 0; i < 4; i++) {
        SockSendSingleRequest(iov, i);
    }
}

bool SockClientRegSglMem()
{
    for (uint16_t i = 0; i < NN_NO4; i++) {
        UBSHcomNetMemoryRegionPtr mr;
        auto result = sockClientDriver->CreateMemoryRegion(NN_NO16, mr);
        if (result != NN_OK) {
            NN_LOG_ERROR("reg mr failed");
            return false;
        }
        ClientLocalMrInfo[i].lAddress = mr->GetAddress();
        ClientLocalMrInfo[i].lKey = mr->GetLKey();
        ClientLocalMrInfo[i].size = NN_NO16;
        memset(reinterpret_cast<void *>(ClientLocalMrInfo[i].lAddress), 0, NN_NO16);
    }

    return true;
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

TEST_F(TestCaseSock, SOCK_CASE_TCP_READWRITE)
{
    bool result = SockServerCreateDriver();
    SOCK_CHECK_RESULT_TRUE(result);

    result = SockServerRegSglMem();
    SOCK_CHECK_RESULT_TRUE(result);

    result = SockClientCreateDriver();
    SOCK_CHECK_RESULT_TRUE(result);
    result = SockClientConnect();
    SOCK_CHECK_RESULT_TRUE(result);
    result = SockClientRegSglMem();
    SOCK_CHECK_RESULT_TRUE(result);
    SockSendRequest();
    CloseDriver(sockClientDriver);
    CloseDriver(sockServerDriver);
}
