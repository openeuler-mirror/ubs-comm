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
#include <cstdint>
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "hcom.h"
#include "service_channel_imp.h"
#include "service_callback.h"
#include "net_rdma_async_endpoint.h"
#include "under_api/urma/urma_api_wrapper.h"

namespace ock {
namespace hcom {

class TestHcomServiceV2 : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestHcomServiceV2::SetUp()
{}

void TestHcomServiceV2::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestHcomServiceV2, TestHcomServiceV2Create)
{
    UBSHcomServiceOptions options{};
    options.maxSendRecvDataSize = 0;
    UBSHcomService *service = UBSHcomService::Create(UBSHcomNetDriverProtocol::RDMA, "client1", options);
    EXPECT_EQ(service, nullptr);
    options.maxSendRecvDataSize = NN_NO1024;
    std::string longName(NN_NO64 + 1, 'a');
    service = UBSHcomService::Create(UBSHcomNetDriverProtocol::RDMA, longName, options);
    EXPECT_EQ(service, nullptr);
    service = UBSHcomService::Create(UBSHcomNetDriverProtocol::RDMA, "client1", options);
    EXPECT_NE(service, nullptr);
    UBSHcomService *service1 = UBSHcomService::Create(UBSHcomNetDriverProtocol::RDMA, "client1", options);
    EXPECT_NE(service1, nullptr);
    EXPECT_EQ(service, service1);

    EXPECT_EQ(UBSHcomService::Destroy("client1"), SER_OK);
}

TEST_F(TestHcomServiceV2, TestHcomServiceV2Destroy)
{
    EXPECT_EQ(UBSHcomService::Destroy("client1"), SER_ERROR);
    UBSHcomServiceOptions options{};
    UBSHcomService *service = new HcomServiceImp(UBSHcomNetDriverProtocol::RDMA, "client1", options);
    MOCKER_CPP_VIRTUAL(*service, &UBSHcomService::DoDestroy).stubs().will(returnValue(static_cast<int>(SER_ERROR)));
    EXPECT_EQ(UBSHcomService::Destroy("client1"), SER_ERROR);
    delete service;
}

TEST_F(TestHcomServiceV2, TestHcomServiceTimer)
{
    HcomServiceTimer *timer = new HcomServiceTimer();
    EXPECT_NE(timer, nullptr);
    EXPECT_EQ(timer->SeqNo(), 0);
    EXPECT_EQ(timer->Timeout(), 0);
    EXPECT_EQ(timer->Callback(), 0);
    EXPECT_NO_FATAL_FAILURE(timer->TimeoutDump());

    UBSHcomServiceContext ctx;
    EXPECT_NO_FATAL_FAILURE(timer->RunCallBack(ctx));
    EXPECT_NO_FATAL_FAILURE(timer->DeleteCallBack());
    delete timer;
}

TEST_F(TestHcomServiceV2, TestHcomServiceTimer2)
{
    HcomServiceTimer *timer = new HcomServiceTimer();
    EXPECT_NE(timer, nullptr);
    EXPECT_EQ(timer->IsFinished(), false);
    EXPECT_NO_FATAL_FAILURE(timer->MarkFinished());
    EXPECT_NO_FATAL_FAILURE(timer->MarkTimeout());
    EXPECT_EQ(timer->IsTimeOut(), false);

    timer->mTimeout = NN_NO10;
    ASSERT_EQ(timer->IsTimeOut(), true);
    delete timer;
}

TEST_F(TestHcomServiceV2, TestHcomServiceTimerFail)
{
    HcomServiceTimer *timer = new HcomServiceTimer();
    ASSERT_NE(timer, nullptr);
    EXPECT_NO_FATAL_FAILURE(timer->EraseSeqNo());
    EXPECT_EQ(timer->EraseSeqNoWithRet(), false);
    timer->mCtxStore = new (std::nothrow) HcomServiceCtxStore(NN_NO2097152, nullptr, UBSHcomNetDriverProtocol::RDMA);
    ASSERT_NE(timer->mCtxStore, nullptr);
    MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<uintptr_t>)
        .stubs()
        .will(returnValue(static_cast<int>(SER_STORE_SEQ_NO_FOUND)));
    EXPECT_NO_FATAL_FAILURE(timer->EraseSeqNo());
    EXPECT_EQ(timer->EraseSeqNoWithRet(), false);
    delete timer;
}

TEST_F(TestHcomServiceV2, TestHcomServiceTimerCompare)
{
    HcomServiceTimer *timer1 = new HcomServiceTimer();
    EXPECT_NE(timer1, nullptr);
    HcomServiceTimer *timer2 = new HcomServiceTimer();
    EXPECT_NE(timer2, nullptr);

    HcomServiceTimerCompare compare;
    timer1->mTimeout = 2000;
    timer2->mTimeout = 1000;
    EXPECT_TRUE(compare(timer1, timer2));
    timer1->mTimeout = 2000;
    timer2->mTimeout = 2000;
    timer1->mSeqNo = 2;
    timer2->mSeqNo = 1;
    EXPECT_TRUE(compare(timer1, timer2));
    timer1->mTimeout = 1000;
    timer2->mTimeout = 2000;
    EXPECT_FALSE(compare(timer1, timer2));

    delete timer2;
    delete timer1;
}

TEST_F(TestHcomServiceV2, TestHexStringToBuff)
{
    std::string input = "1A2B3C4D";
    uint8_t buff[NN_NO4] = {0};
    EXPECT_TRUE(HexStringToBuff(input, NN_NO4, buff));
    EXPECT_EQ(buff[NN_NO0], 0x1A);
    EXPECT_EQ(buff[NN_NO1], 0x2B);
    EXPECT_EQ(buff[NN_NO2], 0x3C);
    EXPECT_EQ(buff[NN_NO3], 0x4D);
}

TEST_F(TestHcomServiceV2, TestHexStringToBuff2)
{
    uint8_t *buff = nullptr;
    EXPECT_FALSE(HexStringToBuff("1A2B3C4D", NN_NO4, buff));

    uint8_t buff1[NN_NO4] = {0};
    EXPECT_FALSE(HexStringToBuff("1A2B3C", NN_NO4, buff1));
    uint8_t buff2[NN_NO4] = {0};
    EXPECT_FALSE(HexStringToBuff("1A2B3C5", NN_NO4, buff2));

    uint8_t buff3[NN_NO4] = {0};
    std::string invalidInput = "1G";  // 'G' 不是有效的十六进制字符
    EXPECT_FALSE(HexStringToBuff(invalidInput, NN_NO4, buff3));
}

TEST_F(TestHcomServiceV2, TestBuffToHexString)
{
    uint8_t *buff = nullptr;
    uint32_t bufferSize = 10;
    std::string output;
    EXPECT_FALSE(BuffToHexString(buff, bufferSize, output));
    EXPECT_TRUE(output.empty());
}

TEST_F(TestHcomServiceV2, TestSerialize)
{
    SerConnInfo connInfo;
    std::string payload = "1A2B3C4D";
    std::string out;
    EXPECT_EQ(SerConnInfo::Serialize(connInfo, payload, out), SER_OK);
}

TEST_F(TestHcomServiceV2, TestSerializeFail)
{
    SerConnInfo *connInfo = nullptr;
    std::string payload = "TestPayload";
    std::string out;
    EXPECT_EQ(SerConnInfo::Serialize(*connInfo, payload, out), SER_ERROR);
    EXPECT_TRUE(out.empty());
}

TEST_F(TestHcomServiceV2, TestDeserialize)
{
    SerConnInfo connInfo;
    std::string payload = "00000000000000000000000000000000"
                          "00000000000000000000000000000000"
                          "00000000000000000000000000000000"
                          "0000000000000000DEADBEEF";  // 大于sizeof(SerConnInfo)*2
    std::string userPayload;

    MOCKER_CPP(&SerConnInfo::Validate).stubs().will(returnValue(true));
    EXPECT_EQ(SerConnInfo::Deserialize(payload, connInfo, userPayload), NN_OK);
}

TEST_F(TestHcomServiceV2, TestDeserializeFail)
{
    SerConnInfo connInfo;
    std::string payload1 = "1A2B3C4D";
    std::string userPayload;
    EXPECT_EQ(SerConnInfo::Deserialize(payload1, connInfo, userPayload), SER_INVALID_PARAM);

    std::string payload2 = "1A2B";  // 长度不足 sizeof(SerConnInfo) * 2
    EXPECT_EQ(SerConnInfo::Deserialize(payload2, connInfo, userPayload), SER_INVALID_PARAM);

    std::string payload3 = "1A2B3C4DGG";  // 包含无效字符 'GG'
    EXPECT_EQ(SerConnInfo::Deserialize(payload3, connInfo, userPayload), SER_INVALID_PARAM);

    std::string payload4 = "00000000FFFFFFFF0000000000000000";  // CRC 校验失败
    EXPECT_EQ(SerConnInfo::Deserialize(payload4, connInfo, userPayload), SER_INVALID_PARAM);
}

TEST_F(TestHcomServiceV2, TestHcomServiceGlobalObjectInitialize)
{
    EXPECT_EQ(HcomServiceGlobalObject::Initialize(), SER_OK);
    EXPECT_TRUE(HcomServiceGlobalObject::gInited);
    EXPECT_EQ(HcomServiceGlobalObject::Initialize(), SER_OK);
    EXPECT_NE(HcomServiceGlobalObject::gEmptyCallback, nullptr);
    HcomServiceGlobalObject::UnInitialize();
    EXPECT_EQ(HcomServiceGlobalObject::gEmptyCallback, nullptr);
}

TEST_F(TestHcomServiceV2, TestHcomServiceGlobalObjectBuildCtx)
{
    UBSHcomServiceContext ctx;
    EXPECT_NO_FATAL_FAILURE(HcomServiceGlobalObject::BuildBrokenCtx(ctx));
}

TEST_F(TestHcomServiceV2, TestHcomConnectingEpInfoAllEPBroken)
{
    UBSHcomNetWorkerIndex workerIndex{};
    uint32_t workerIdx = NN_NO4;
    uint32_t gIdx = NN_NO6;
    uint16_t dIdx = NN_NO8;
    workerIndex.Set(workerIdx, gIdx, dIdx);
    UBSHcomNetEndpointPtr ep = new (std::nothrow) NetAsyncEndpoint(NN_NO100, nullptr, nullptr, workerIndex);
    SerConnInfo info{};
    std::string id = "123";
    HcomConnectingEpInfo *epInfo = new (std::nothrow) HcomConnectingEpInfo(id, ep, info);
    bool ret = epInfo->AllEPBroken(NN_NO6);
    ASSERT_EQ(ret, false);
    if (epInfo != nullptr) {
        delete epInfo;
        epInfo = nullptr;
    }
}

TEST_F(TestHcomServiceV2, TestHcomConnectingEpInfoCompareFail)
{
    HcomConnectingEpInfo *connectChannelInfo = new (std::nothrow) HcomConnectingEpInfo();
    SerConnInfo info;
    bool ret;

    // Fail 1
    connectChannelInfo->mConnInfo.version = NN_NO1;
    info.version = NN_NO3;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Fail 2
    info.version = NN_NO1;
    connectChannelInfo->mConnInfo.channelId = NN_NO1;
    info.channelId = NN_NO3;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Fail 3
    info.channelId = NN_NO1;
    connectChannelInfo->mConnInfo.policy = UBSHcomChannelBrokenPolicy::BROKEN_ALL;
    info.policy = UBSHcomChannelBrokenPolicy::RECONNECT;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Fail 4
    info.policy = UBSHcomChannelBrokenPolicy::BROKEN_ALL;
    info.index = NN_NO1;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Fail 5
    info.index = NN_NO0;
    connectChannelInfo->mConnInfo.options.linkCount = NN_NO1;
    info.options.linkCount = NN_NO3;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Fail 6
    info.options.linkCount = NN_NO1;
    connectChannelInfo->mConnInfo.options.cbType = UBSHcomChannelCallBackType::CHANNEL_FUNC_CB;
    info.options.cbType = UBSHcomChannelCallBackType::CHANNEL_GLOBAL_CB;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Fail 7
    info.options.cbType = UBSHcomChannelCallBackType::CHANNEL_FUNC_CB;
    connectChannelInfo->mConnInfo.options.clientGroupId = NN_NO1;
    info.options.clientGroupId = NN_NO3;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Fail 8
    info.options.clientGroupId = NN_NO1;
    connectChannelInfo->mConnInfo.options.serverGroupId = NN_NO1;
    info.options.serverGroupId = NN_NO3;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, false);

    // Success
    info.options.serverGroupId = NN_NO1;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, true);

    if (connectChannelInfo != nullptr) {
        delete connectChannelInfo;
        connectChannelInfo = nullptr;
    }
}

TEST_F(TestHcomServiceV2, TestHcomConnectingEpInfoCompare)
{
    HcomConnectingEpInfo *connectChannelInfo = new (std::nothrow) HcomConnectingEpInfo();
    SerConnInfo info;
    bool ret;

    connectChannelInfo->mConnInfo.version = NN_NO1;
    connectChannelInfo->mConnInfo.channelId = NN_NO1;
    connectChannelInfo->mConnInfo.policy = UBSHcomChannelBrokenPolicy::BROKEN_ALL;
    connectChannelInfo->mConnInfo.options.linkCount = NN_NO1;
    connectChannelInfo->mConnInfo.options.cbType = UBSHcomChannelCallBackType::CHANNEL_FUNC_CB;
    connectChannelInfo->mConnInfo.options.clientGroupId = NN_NO1;
    connectChannelInfo->mConnInfo.options.serverGroupId = NN_NO1;
    info.version = NN_NO1;
    info.channelId = NN_NO1;
    info.policy = UBSHcomChannelBrokenPolicy::BROKEN_ALL;
    info.index = NN_NO0;
    info.options.linkCount = NN_NO1;
    info.options.cbType = UBSHcomChannelCallBackType::CHANNEL_FUNC_CB;
    info.options.clientGroupId = NN_NO1;
    info.options.serverGroupId = NN_NO1;
    ret = connectChannelInfo->Compare(info);
    ASSERT_EQ(ret, true);

    if (connectChannelInfo != nullptr) {
        delete connectChannelInfo;
        connectChannelInfo = nullptr;
    }
}

TEST_F(TestHcomServiceV2, TestMemoryRegion)
{
    UBSHcomRegMemoryRegion region{};
    UBSHcomMemoryKey key{};
    EXPECT_NO_FATAL_FAILURE(region.GetMemoryKey(key));
    EXPECT_EQ(region.GetAddress(), 0);
    EXPECT_EQ(region.GetSize(), 0);
}

TEST_F(TestHcomServiceV2, TestGetServiceTransNeedPostedCall)
{
    SerTransContext ctxData {};
    char *ctx = reinterpret_cast<char*>(&ctxData);
    EXPECT_EQ(GetServiceTransNeedPostedCall(ctx), true);
}

TEST_F(TestHcomServiceV2, TestIsNeedInvokeCallback)
{
    UBSHcomRequestContext ctx{};
    MOCKER_CPP(&GetServiceTransNeedPostedCall)
        .stubs()
        .will(returnValue(true));
    EXPECT_EQ(IsNeedInvokeCallback(ctx), false);
    ctx.mResult = 1;
    ctx.mOpType = UBSHcomRequestContext::NN_SENT;
    EXPECT_EQ(IsNeedInvokeCallback(ctx), true);
    ctx.mOpType = UBSHcomRequestContext::NN_SENT_RAW_SGL;
    EXPECT_EQ(IsNeedInvokeCallback(ctx), true);
    ctx.mOpType = UBSHcomRequestContext::NN_INVALID_OP_TYPE;
    EXPECT_EQ(IsNeedInvokeCallback(ctx), true);
}

TEST_F(TestHcomServiceV2, TestSetTraceIdInner)
{
#ifdef UB_BUILD_ENABLED
    MOCKER(HcomUrma::IsLoaded).stubs().will(returnValue(false)).then(returnValue(true));
    MOCKER(HcomUrma::LogSetThreadTag).stubs().will(ignoreReturnValue());
    std::string traceId = "This is a test trace id";

    EXPECT_NO_FATAL_FAILURE(SetTraceIdInner(traceId));
    EXPECT_NO_FATAL_FAILURE(SetTraceIdInner(traceId));
#endif
}
}  // namespace hcom
}  // namespace ock
