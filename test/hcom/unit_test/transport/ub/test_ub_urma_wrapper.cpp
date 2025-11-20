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

#ifdef UB_BUILD_ENABLED

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <sys/poll.h>

#include "net_monotonic.h"
#include "ub_common.h"
#include "ub_mr_fixed_buf.h"
#include "ub_urma_wrapper_jetty.h"
#include "under_api/urma/urma_api_wrapper.h"

namespace ock {
namespace hcom {
urma_device_t **resList = nullptr;

class TestUbUrmaWrapper : public testing::Test {
public:
    TestUbUrmaWrapper();
    virtual void SetUp(void);
    virtual void TearDown(void);
    std::string name = "test";
    UBEId eid{};
    UBDeviceHelper *mUBDeviceHelper = nullptr;
    UBContext *ctx = nullptr;
    UBJfc *jfc = nullptr;
};

TestUbUrmaWrapper::TestUbUrmaWrapper() {}

void TestUbUrmaWrapper::SetUp()
{
    mUBDeviceHelper = new (std::nothrow) UBDeviceHelper();
    ctx = new (std::nothrow) UBContext("ctx", eid);
    jfc = new (std::nothrow) UBJfc(name, ctx, false, 0);
    resList = (urma_device_t **)malloc(sizeof(urma_device_t *));
}

void TestUbUrmaWrapper::TearDown()
{
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(ignoreReturnValue());
    MOCKER_CPP(&UBDeviceHelper::UnInitialize).stubs().will(ignoreReturnValue());

    jfc->mUBContext = nullptr;

    if (mUBDeviceHelper != nullptr) {
        delete mUBDeviceHelper;
        mUBDeviceHelper = nullptr;
    }

    if (ctx != nullptr) {
        delete ctx;
        ctx = nullptr;
    }

    if (jfc != nullptr) {
        delete jfc;
        jfc = nullptr;
    }
    free(resList);
    GlobalMockObject::verify();
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperInitialize)
{
    mUBDeviceHelper->G_InitRef = 1;
    UResult ret = mUBDeviceHelper->Initialize();
    EXPECT_EQ(ret, UB_OK);

    mUBDeviceHelper->G_InitRef = 0;
    MOCKER_CPP(&UBDeviceHelper::DoInitialize).stubs().will(returnValue(0));
    ret = mUBDeviceHelper->Initialize();
    EXPECT_EQ(ret, UB_OK);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperUnInitialize)
{
    MOCKER_CPP(HcomUrma::Uninit).stubs().will(returnValue(0));
    EXPECT_NO_FATAL_FAILURE(mUBDeviceHelper->UnInitialize());
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperDoInitialize)
{
    MOCKER_CPP(&UBDeviceHelper::DoUpdate).stubs().will(returnValue(1)).then(returnValue(0));
    UResult ret = mUBDeviceHelper->DoInitialize();
    EXPECT_EQ(ret, 1);
    ret = mUBDeviceHelper->DoInitialize();
    EXPECT_EQ(ret, UB_OK);
    mUBDeviceHelper->G_InitRef = 0;
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperDoUpdate)
{
    MOCKER_CPP(HcomUrma::Init).stubs().will(returnValue(0)).then(returnValue(1));
    urma_device_t **devList = nullptr;
    MOCKER_CPP(HcomUrma::GetDeviceList).stubs().will(returnValue(devList));
    UResult ret = mUBDeviceHelper->DoUpdate();
    EXPECT_EQ(ret, UB_DEVICE_FAILED_OPEN);
    ret = mUBDeviceHelper->DoUpdate();
    EXPECT_EQ(ret, 1);
}

void MockFreeDeviceList(urma_device_t **device_list)
{
    return;
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperDoUpdateErr)
{
    MOCKER_CPP(&HcomUrma::Init).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::GetDeviceList).stubs().will(returnValue(resList));
    void *devAttr = nullptr;
    MOCKER(malloc).stubs().will(returnValue(devAttr));
    MOCKER_CPP(&HcomUrma::FreeDeviceList).stubs().will(invoke(MockFreeDeviceList));
    UResult ret = mUBDeviceHelper->DoUpdate();
    EXPECT_EQ(ret, UB_NEW_OBJECT_FAILED);
}

urma_device_t **MockGetDeviceList(int *num_devices)
{
    *num_devices = NN_NO8;
    return resList;
}

TEST_F(TestUbUrmaWrapper, UBContextInitErr)
{
    MOCKER_CPP(&HcomUrma::Init).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::GetDeviceList).stubs().will(invoke(MockGetDeviceList));
    urma_context_t tmpCtx{};
    MOCKER_CPP(&HcomUrma::CreateContext).stubs().will(returnValue(&tmpCtx));
    void *devAttr = nullptr;
    MOCKER(malloc).stubs().will(returnValue(devAttr));
    MOCKER_CPP(&HcomUrma::DeleteContext).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::FreeDeviceList).stubs().will(invoke(MockFreeDeviceList));
    UResult ret = ctx->Initialize();
    EXPECT_EQ(ret, UB_MEMORY_ALLOCATE_FAILED);
}

TEST_F(TestUbUrmaWrapper, UBContextInitErrTwo)
{
    MOCKER_CPP(&HcomUrma::Init).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::GetDeviceList).stubs().will(invoke(MockGetDeviceList));
    urma_context_t tmpCtx{};
    MOCKER_CPP(&HcomUrma::CreateContext).stubs().will(returnValue(&tmpCtx));
    MOCKER_CPP(&HcomUrma::QueryDevice).stubs().will(returnValue(1));
    MOCKER_CPP(&HcomUrma::DeleteContext).stubs().will(returnValue(0));
    MOCKER_CPP(&HcomUrma::FreeDeviceList).stubs().will(invoke(MockFreeDeviceList));
    UResult ret = ctx->Initialize();
    EXPECT_EQ(ret, 1);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetEidVec)
{
    std::string devName = "device";
    uint16_t devIndex = 0;
    uint32_t eidCnt = 1;
    urma_eid_info_t eidInfoList{};
    eidInfoList.eid.in6.interface_id = 1;
    std::vector<UBEId> outGidVec;
    uint8_t bandWidth = 1;
    EXPECT_NO_FATAL_FAILURE(mUBDeviceHelper->GetEidVec(devName, devIndex, eidCnt, &eidInfoList, outGidVec, bandWidth));
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetDeviceCountInitializeFailed)
{
    uint16_t deviceCount = 0;
    std::vector<UBDeviceSimpleInfo> enabledDevices;
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(1));
    UResult ret = mUBDeviceHelper->GetDeviceCount(deviceCount, enabledDevices);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetDeviceCount)
{
    uint16_t deviceCount = 0;
    std::vector<UBDeviceSimpleInfo> enabledDevices;
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(0));
    UResult ret = mUBDeviceHelper->GetDeviceCount(deviceCount, enabledDevices);
    EXPECT_EQ(ret, UB_OK);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetEnableDeviceCountInvalidIPMaskOrNoMatchedIP)
{
    uint16_t enableDevCount = 0;
    std::string ipMask = "";
    std::string ipGroup = "";
    std::vector<std::string> enableIps;
    UResult ret = mUBDeviceHelper->GetEnableDeviceCount(ipMask, enableDevCount, enableIps, ipGroup);
    EXPECT_EQ(ret, NN_INVALID_IP);

    ipMask = "10.0.0.0/24";
    ret = mUBDeviceHelper->GetEnableDeviceCount(ipMask, enableDevCount, enableIps, ipGroup);
    EXPECT_EQ(ret, UB_DEVICE_NO_IP_MATCHED);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetEnableDeviceCountInitializeFailed)
{
    uint16_t enableDevCount = 0;
    std::string ipMask = "";
    std::string ipGroup = "192.168.0.1;192.168.0.2";
    std::vector<std::string> enableIps;
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(1));
    UResult ret = mUBDeviceHelper->GetEnableDeviceCount(ipMask, enableDevCount, enableIps, ipGroup);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetEnableDeviceCount)
{
    uint16_t enableDevCount = 0;
    std::string ipMask = "";
    std::string ipGroup = "192.168.0.1;192.168.0.2";
    std::vector<std::string> enableIps;
    UBDeviceHelper::G_UBDevMap[0].active = true;
    UBDeviceHelper::G_UBDevMap[1].active = true;
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(UBDeviceHelper::GetDeviceByIp).stubs().will(returnValue(1)).then(returnValue(0));
    UResult ret = mUBDeviceHelper->GetEnableDeviceCount(ipMask, enableDevCount, enableIps, ipGroup);
    EXPECT_EQ(ret, 0);
    UBDeviceHelper::G_UBDevMap.clear();
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetEnableDeviceCountNoMatchedFail)
{
    uint16_t enableDevCount = 0;
    std::string ipMask = "";
    std::string ipGroup = "192.168.0.1;192.168.0.2";
    std::vector<std::string> enableIps;
    UBDeviceHelper::G_UBDevMap[0].active = true;
    UBDeviceHelper::G_UBDevMap[1].active = true;
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(0));
    MOCKER_CPP(UBDeviceHelper::GetDeviceByIp).stubs().will(returnValue(1));
    UResult ret = mUBDeviceHelper->GetEnableDeviceCount(ipMask, enableDevCount, enableIps, ipGroup);
    EXPECT_EQ(ret, UB_DEVICE_NO_IP_MATCHED);
    UBDeviceHelper::G_UBDevMap.clear();
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetIfAddressByIp)
{
    std::string ip = "192.168.0.1";
    struct sockaddr_in address;
    MOCKER_CPP(&getifaddrs).stubs().will(returnValue(1));
    UResult ret = mUBDeviceHelper->GetIfAddressByIp(ip, address);
    EXPECT_EQ(ret, UB_DEVICE_FAILED_GET_IP_ADDRESS);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetDeviceByAddressInitializeFailed)
{
    std::string ip = "192.168.0.1";
    struct sockaddr_in address;
    UBEId eid;
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(1));
    UResult ret = mUBDeviceHelper->GetDeviceByAddress(ip, address, eid);
    EXPECT_EQ(ret, 1);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetDeviceByAddressNoDeviceFound)
{
    std::string ip = "192.168.0.1";
    struct sockaddr_in address;
    address.sin_addr.s_addr = inet_addr(ip.c_str());
    UBEId eid;
    UBEId testEid;
    testEid.devIndex = 1;
    testEid.eidIndex = 1;
    std::string key = std::to_string(testEid.devIndex);
    UBDeviceHelper::G_UBDevEidTable[key].push_back(testEid);
    MOCKER_CPP(UBDeviceHelper::Initialize).stubs().will(returnValue(0));
    UResult ret = mUBDeviceHelper->GetDeviceByAddress(ip, address, eid);
    EXPECT_EQ(ret, UB_DEVICE_NO_IP_TO_GID_MATCHED);
    UBDeviceHelper::G_UBDevEidTable.clear();
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetDeviceByEidNotFound)
{
    UBEId eid;
    uint8_t user[URMA_EID_SIZE] = {};
    EXPECT_EQ(UBDeviceHelper::GetDeviceByEid(user, eid), UB_DEVICE_NO_IP_TO_GID_MATCHED);
}

TEST_F(TestUbUrmaWrapper, UBDeviceHelperGetPortNumber)
{
    uint32_t ret = mUBDeviceHelper->GetPortNumber();
    EXPECT_EQ(ret, mUBDeviceHelper->PORT_NUMBER);
}

TEST_F(TestUbUrmaWrapper, UBJfcCreateEventCqCreatJfceFail)
{
    urma_jfce_t *jfcePtr = nullptr;
    MOCKER_CPP(HcomUrma::CreateJfce).stubs().will(returnValue(jfcePtr));
    UResult ret = jfc->CreateEventCq();
    EXPECT_EQ(ret, UB_NEW_OBJECT_FAILED);
}

TEST_F(TestUbUrmaWrapper, UBJfcCreateEventCqCreatJfcFail)
{
    urma_jfce_t jfce{};
    urma_jfce_t *jfcePtr = &jfce;
    urma_jfc_t *jfcPtr = nullptr;
    MOCKER_CPP(HcomUrma::CreateJfce).stubs().will(returnValue(jfcePtr));
    MOCKER_CPP(HcomUrma::CreateJfc).stubs().will(returnValue(jfcPtr));
    MOCKER_CPP(HcomUrma::DeleteJfce).stubs().will(returnValue(0));
    UResult ret = jfc->CreateEventCq();
    EXPECT_EQ(ret, UB_NEW_OBJECT_FAILED);
}

TEST_F(TestUbUrmaWrapper, UBJfcCreateEventCqRearmJfcFail)
{
    urma_jfce_t jfce{};
    urma_jfce_t *jfcePtr = &jfce;
    urma_jfc_t testJfc{};
    urma_jfc_t *jfcPtr = &testJfc;
    MOCKER_CPP(HcomUrma::CreateJfce).stubs().will(returnValue(jfcePtr));
    MOCKER_CPP(HcomUrma::CreateJfc).stubs().will(returnValue(jfcPtr));
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(1));
    MOCKER_CPP(HcomUrma::DeleteJfce).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::DeleteJfc).stubs().will(returnValue(0));
    UResult ret = jfc->CreateEventCq();
    EXPECT_EQ(ret, UB_NEW_OBJECT_FAILED);
}

TEST_F(TestUbUrmaWrapper, UBJfcCreateEventCqSuccess)
{
    urma_jfce_t jfce{};
    urma_jfce_t *jfcePtr = &jfce;
    urma_jfc_t testJfc{};
    urma_jfc_t *jfcPtr = &testJfc;
    MOCKER_CPP(HcomUrma::CreateJfce).stubs().will(returnValue(jfcePtr));
    MOCKER_CPP(HcomUrma::CreateJfc).stubs().will(returnValue(jfcPtr));
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(0));
    UResult ret = jfc->CreateEventCq();
    EXPECT_EQ(ret, UB_OK);

    jfc->mUrmaJfc = nullptr;
    jfc->mUrmaJfcEvent = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcInitialize)
{
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    UResult ret = jfc->Initialize();
    EXPECT_EQ(ret, UB_OK);
    jfc->mUrmaJfc = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcInitializeCtxNull)
{
    jfc->mUrmaJfc = nullptr;
    jfc->mUBContext = nullptr;
    UResult ret = jfc->Initialize();
    EXPECT_EQ(ret, UB_PARAM_INVALID);
}

TEST_F(TestUbUrmaWrapper, UBJfcInitializeCreateEventCq)
{
    jfc->mUrmaJfc = nullptr;
    jfc->mUBContext = ctx;
    urma_context_t urmaCtx{};
    jfc->mUBContext->mUrmaContext = &urmaCtx;
    jfc->mCreateCompletionChannel = true;
    MOCKER_CPP(UBJfc::CreateEventCq).stubs().will(returnValue(0));
    UResult ret = jfc->Initialize();
    EXPECT_EQ(ret, UB_OK);
    jfc->mUBContext->mUrmaContext = nullptr;
    jfc->mUBContext = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcInitializeCreatePollingCq)
{
    jfc->mUrmaJfc = nullptr;
    jfc->mUBContext = ctx;
    urma_context_t urmaCtx{};
    jfc->mUBContext->mUrmaContext = &urmaCtx;
    jfc->mCreateCompletionChannel = false;
    MOCKER_CPP(UBJfc::CreatePollingCq).stubs().will(returnValue(0));
    UResult ret = jfc->Initialize();
    EXPECT_EQ(ret, UB_OK);
    jfc->mUBContext->mUrmaContext = nullptr;
    jfc->mUBContext = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcUnInitialize)
{
    urma_jfc_t testJfc{};
    urma_jfce_t jfce{};
    jfc->mUrmaJfc = &testJfc;
    jfc->mUrmaJfcEvent = &jfce;
    jfc->mUBContext = nullptr;
    MOCKER_CPP(HcomUrma::DeleteJfc).stubs().will(returnValue(11));
    MOCKER_CPP(HcomUrma::DeleteJfce).stubs().will(returnValue(0));
    UResult ret = jfc->UnInitialize();
    EXPECT_EQ(ret, UB_OK);
}

TEST_F(TestUbUrmaWrapper, UBJfcProgressVPollJfcFail)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(-1));
    UResult ret = jfc->ProgressV(&cr, countInOut);
    EXPECT_EQ(ret, UB_CQ_POLLING_FAILED);
    jfc->mUrmaJfc = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcProgressVSuccess)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(0)).then(returnValue(1));
    UResult ret = jfc->ProgressV(&cr, countInOut);
    EXPECT_EQ(ret, UB_OK);
    jfc->mUrmaJfc = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcEventProgressVUrmaJfcNull)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    int32_t timeoutInMs = 0;
    jfc->mUrmaJfc = nullptr;
    UResult ret = jfc->EventProgressV(&cr, countInOut, timeoutInMs);
    EXPECT_EQ(ret, UB_CQ_NOT_INITIALIZED);
}

TEST_F(TestUbUrmaWrapper, UBJfcEventProgressVRearmJfcFail)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    int32_t timeoutInMs = 0;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    urma_jfce_t jfce{};
    jfc->mUrmaJfcEvent = &jfce;
    MOCKER_CPP(HcomUrma::WaitJfc).stubs().will(returnValue(1));
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(1));
    MOCKER_CPP(HcomUrma::AckJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(1));
    UResult ret = jfc->EventProgressV(&cr, countInOut, timeoutInMs);
    EXPECT_EQ(ret, UB_CQ_EVENT_NOTIFY_FAILED);
    jfc->mUrmaJfc = nullptr;
    jfc->mUrmaJfcEvent = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcEventProgressVWaitJfcTimeOut)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    int32_t timeoutInMs = 0;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    urma_jfce_t jfce{};
    jfc->mUrmaJfcEvent = &jfce;
    MOCKER_CPP(HcomUrma::WaitJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::AckJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(0));
    UResult ret = jfc->EventProgressV(&cr, countInOut, timeoutInMs);
    EXPECT_EQ(ret, UB_OK);
    jfc->mUrmaJfc = nullptr;
    jfc->mUrmaJfcEvent = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcEventProgressVWaitJfcFail)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    int32_t timeoutInMs = -1;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    urma_jfce_t jfce{};
    jfc->mUrmaJfcEvent = &jfce;
    MOCKER_CPP(HcomUrma::WaitJfc).stubs().will(returnValue(-1));
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::AckJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(0));
    UResult ret = jfc->EventProgressV(&cr, countInOut, timeoutInMs);
    EXPECT_EQ(ret, UB_CQ_EVENT_GET_FAILED);
    jfc->mUrmaJfc = nullptr;
    jfc->mUrmaJfcEvent = nullptr;
}

int FakeWaitJfc1(urma_jfce_t *jfce, uint32_t jfc_cnt, int time_out, urma_jfc_t *jfc[])
{
    jfc[0] = (urma_jfc_t *)0xdeadbabe;
    return 1;
}

TEST_F(TestUbUrmaWrapper, UBJfcEventProgressVAckJfc)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    int32_t timeoutInMs = -1;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    urma_jfce_t jfce{};
    jfc->mUrmaJfcEvent = &jfce;
    MOCKER_CPP(HcomUrma::WaitJfc).stubs().will(invoke(FakeWaitJfc1));
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::AckJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(0));
    UResult ret = jfc->EventProgressV(&cr, countInOut, timeoutInMs);
    EXPECT_EQ(ret, UB_OK);
    jfc->mUrmaJfc = nullptr;
    jfc->mUrmaJfcEvent = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcEventProgressVPollJfcFail)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    int32_t timeoutInMs = -1;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    urma_jfce_t jfce{};
    jfc->mUrmaJfcEvent = &jfce;
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(0));
    MOCKER_CPP(&poll).stubs().will(returnValue(1));
    MOCKER_CPP(HcomUrma::WaitJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(-1));
    UResult ret = jfc->EventProgressV(&cr, countInOut, timeoutInMs);
    EXPECT_EQ(ret, UB_CQ_POLLING_FAILED);
    jfc->mUrmaJfc = nullptr;
    jfc->mUrmaJfcEvent = nullptr;
}

TEST_F(TestUbUrmaWrapper, UBJfcEventProgressVSuccess)
{
    urma_cr_t cr{};
    uint32_t countInOut = 0;
    int32_t timeoutInMs = -1;
    urma_jfc_t testJfc{};
    jfc->mUrmaJfc = &testJfc;
    urma_jfce_t jfce{};
    jfc->mUrmaJfcEvent = &jfce;
    MOCKER_CPP(HcomUrma::RearmJfc).stubs().will(returnValue(0));
    MOCKER_CPP(&poll).stubs().will(returnValue(1));
    MOCKER_CPP(HcomUrma::WaitJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::PollJfc).stubs().will(returnValue(0));
    MOCKER_CPP(HcomUrma::AckJfc).stubs().will(returnValue(0));
    UResult ret = jfc->EventProgressV(&cr, countInOut, timeoutInMs);
    EXPECT_EQ(ret, UB_OK);
    jfc->mUrmaJfc = nullptr;
    jfc->mUrmaJfcEvent = nullptr;
}

TEST_F(TestUbUrmaWrapper, HasInternalError)
{
    UBOpContextInfo info;

    info.opResultType = UBOpContextInfo::OpResultType::SUCCESS;
    EXPECT_FALSE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::ERR_TIMEOUT;
    EXPECT_TRUE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::ERR_CANCELED;
    EXPECT_TRUE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::ERR_IO_ERROR;
    EXPECT_TRUE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::ERR_EP_BROKEN;
    EXPECT_TRUE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::ERR_EP_CLOSE;
    EXPECT_TRUE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::ERR_ACCESS_ABRT;
    EXPECT_FALSE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::ERR_ACK_TIMEOUT;
    EXPECT_FALSE(info.HasInternalError());

    info.opResultType = UBOpContextInfo::OpResultType::INVALID_MAGIC;
    EXPECT_TRUE(info.HasInternalError());
}

TEST_F(TestUbUrmaWrapper, OpResult)
{
    urma_cr_t result;
    result.status = URMA_CR_SUCCESS;
    EXPECT_EQ(UBOpContextInfo::OpResult(result), UBOpContextInfo::OpResultType::SUCCESS);
    result.status = URMA_CR_RNR_RETRY_CNT_EXC_ERR;
    EXPECT_EQ(UBOpContextInfo::OpResult(result), UBOpContextInfo::OpResultType::ERR_TIMEOUT);
    result.status = URMA_CR_WR_FLUSH_ERR;
    EXPECT_EQ(UBOpContextInfo::OpResult(result), UBOpContextInfo::OpResultType::ERR_CANCELED);
    result.status = URMA_CR_WR_FLUSH_ERR_DONE;
    EXPECT_EQ(UBOpContextInfo::OpResult(result), UBOpContextInfo::OpResultType::ERR_CANCELED);
    result.status = URMA_CR_REM_OPERATION_ERR;
    EXPECT_EQ(UBOpContextInfo::OpResult(result), UBOpContextInfo::OpResultType::ERR_IO_ERROR);
    result.status = URMA_CR_REM_ACCESS_ABORT_ERR;
    EXPECT_EQ(UBOpContextInfo::OpResult(result), UBOpContextInfo::OpResultType::ERR_ACCESS_ABRT);
    result.status = URMA_CR_ACK_TIMEOUT_ERR;
    EXPECT_EQ(UBOpContextInfo::OpResult(result), UBOpContextInfo::OpResultType::ERR_ACK_TIMEOUT);
}

TEST_F(TestUbUrmaWrapper, GetNResult)
{
    UBOpContextInfo context{};
    UBOpContextInfo::OpResultType opResult = UBOpContextInfo::OpResultType::ERR_TIMEOUT;
    EXPECT_EQ(UBOpContextInfo::GetNResult(opResult), NN_MSG_TIMEOUT);
    opResult = UBOpContextInfo::OpResultType::ERR_CANCELED;
    EXPECT_EQ(UBOpContextInfo::GetNResult(opResult), NN_MSG_CANCELED);
    opResult = UBOpContextInfo::OpResultType::ERR_EP_BROKEN;
    EXPECT_EQ(UBOpContextInfo::GetNResult(opResult), NN_EP_BROKEN);
    opResult = UBOpContextInfo::OpResultType::ERR_EP_CLOSE;
    EXPECT_EQ(UBOpContextInfo::GetNResult(opResult), NN_EP_CLOSE);
    opResult = UBOpContextInfo::OpResultType::ERR_IO_ERROR;
    EXPECT_EQ(UBOpContextInfo::GetNResult(opResult), NN_MSG_ERROR);
    opResult = UBOpContextInfo::OpResultType::ERR_ACCESS_ABRT;
    EXPECT_EQ(UBOpContextInfo::GetNResult(opResult), NN_URMA_ACCESS_ABRT);
    opResult = UBOpContextInfo::OpResultType::ERR_ACK_TIMEOUT;
    EXPECT_EQ(UBOpContextInfo::GetNResult(opResult), NN_URMA_ACK_TIMEOUT);
}


TEST_F(TestUbUrmaWrapper, GetIfAddressByIp)
{
    struct sockaddr_in addr {};
    EXPECT_EQ(UBDeviceHelper::GetIfAddressByIp("127.0.0.1", addr), 0);
}
}
}
#endif
