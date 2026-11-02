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
#include <sys/epoll.h>
#include <cstdint>
#include <map>
#include <mockcpp/mockcpp.hpp>
#include <mutex>

#include "hcom.h"
#include "net_rdma_async_endpoint.h"
#include "service_callback.h"
#include "service_channel_imp.h"
#include "service_common.h"
#include "service_ctx_store.h"
#include "service_periodic_manager.h"
#include "service_timer_trace.h"
#include "under_api/urma/urma_api_wrapper.h"

namespace ock {
namespace hcom {

class TestHcomServiceV2 : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestHcomServiceV2::SetUp() {}

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

TEST_F(TestHcomServiceV2, TestHcomServiceV2CreateInvalidOptions)
{
    UBSHcomServiceOptions options{};
    options.maxSendRecvDataSize = NN_NO1024;
    options.workerGroupMode = static_cast<UBSHcomWorkerMode>(NN_NO3);
    EXPECT_EQ(UBSHcomService::Create(UBSHcomNetDriverProtocol::RDMA, "client-invalid-mode", options), nullptr);

    options.workerGroupMode = NET_BUSY_POLLING;
    options.workerThreadPriority = static_cast<int8_t>(NN_NO20);
    EXPECT_EQ(UBSHcomService::Create(UBSHcomNetDriverProtocol::RDMA, "client-invalid-priority", options), nullptr);

    options.workerThreadPriority = static_cast<int8_t>(NN_NOF20);
    UBSHcomService *service = UBSHcomService::Create(UBSHcomNetDriverProtocol::RDMA, "client-valid-opt", options);
    EXPECT_NE(service, nullptr);
    EXPECT_EQ(UBSHcomService::Destroy("client-valid-opt"), SER_OK);
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
    std::string invalidInput = "1G"; // 'G' 不是有效的十六进制字符
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
                          "0000000000000000DEADBEEF"; // 大于sizeof(SerConnInfo)*2
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

    std::string payload2 = "1A2B"; // 长度不足 sizeof(SerConnInfo) * 2
    EXPECT_EQ(SerConnInfo::Deserialize(payload2, connInfo, userPayload), SER_INVALID_PARAM);

    std::string payload3 = "1A2B3C4DGG"; // 包含无效字符 'GG'
    EXPECT_EQ(SerConnInfo::Deserialize(payload3, connInfo, userPayload), SER_INVALID_PARAM);

    std::string payload4 = "00000000FFFFFFFF0000000000000000"; // CRC 校验失败
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
    SerTransContext ctxData{};
    char *ctx = reinterpret_cast<char *>(&ctxData);
    EXPECT_EQ(GetServiceTransNeedPostedCall(ctx), true);
}

TEST_F(TestHcomServiceV2, TestIsNeedInvokeCallback)
{
    UBSHcomRequestContext ctx{};
    MOCKER_CPP(&GetServiceTransNeedPostedCall).stubs().will(returnValue(true));
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

TEST_F(TestHcomServiceV2, TestHcomConnectTimestamp)
{
    HcomConnectTimestamp ts(NN_NO100, NN_NO200, NN_NO10);

    // 超时时间非正时直接返回 0
    EXPECT_EQ(ts.GetRemoteTimestamp(static_cast<int16_t>(0)), NN_NO0);
    EXPECT_EQ(ts.GetRemoteTimestamp(static_cast<int16_t>(-1)), NN_NO0);

    // 超时时间大于 0 时按对端时间轴推算，结果非 0
    EXPECT_NE(ts.GetRemoteTimestamp(static_cast<int16_t>(60)), NN_NO0);
}

TEST_F(TestHcomServiceV2, TestHcomServiceRndvMessageIsTimeout)
{
    HcomServiceRndvMessage message;

    // timestamp 为 0 表示不超时
    EXPECT_FALSE(message.IsTimeout());

    // 远大于当前时间的 timestamp，未超时
    message.timestamp = UINT64_MAX;
    EXPECT_FALSE(message.IsTimeout());

    // 极小的 timestamp 必然已过期（除非系统刚启动不到 1us）
    message.timestamp = NN_NO1;
    EXPECT_TRUE(message.IsTimeout());
}

TEST_F(TestHcomServiceV2, TestHcomServiceGlobalObjectBuildTimeOutCtx)
{
    UBSHcomServiceContext ctx;
    EXPECT_NO_FATAL_FAILURE(HcomServiceGlobalObject::BuildTimeOutCtx(ctx));
    EXPECT_EQ(ctx.Result(), SER_TIMEOUT);
    EXPECT_EQ(ctx.Channel().Get(), nullptr);
}

TEST_F(TestHcomServiceV2, TestServiceContextCopyData)
{
    UBSHcomServiceContext ctx;

    // dataLen 为 0：清空数据，置为无效
    EXPECT_EQ(ctx.CopyData(nullptr, NN_NO0), SER_OK);
    EXPECT_EQ(ctx.MessageData(), nullptr);
    EXPECT_EQ(ctx.MessageDataLen(), NN_NO0);

    // data 为空但 dataLen 大于 0：非法入参
    EXPECT_EQ(ctx.CopyData(nullptr, NN_NO8), SER_INVALID_PARAM);

    // 正常拷贝：数据被 malloc 复制，长度被更新
    char buf[NN_NO8] = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_EQ(ctx.CopyData(buf, sizeof(buf)), SER_OK);
    EXPECT_NE(ctx.MessageData(), nullptr);
    EXPECT_NE(ctx.MessageData(), static_cast<void *>(buf));
    EXPECT_EQ(ctx.MessageDataLen(), static_cast<uint32_t>(sizeof(buf)));

    // 再次以 dataLen 为 0 调用，释放上次的堆内存
    EXPECT_EQ(ctx.CopyData(nullptr, NN_NO0), SER_OK);
    EXPECT_EQ(ctx.MessageData(), nullptr);
    EXPECT_EQ(ctx.MessageDataLen(), NN_NO0);
}

TEST_F(TestHcomServiceV2, TestServiceContextClone)
{
    UBSHcomServiceContext oldOne;
    char buf[NN_NO8] = {1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_EQ(oldOne.CopyData(buf, sizeof(buf)), SER_OK);
    ASSERT_EQ(oldOne.MessageDataLen(), static_cast<uint32_t>(sizeof(buf)));

    // copyData = false：只同步元数据，不复制数据内容
    UBSHcomServiceContext noData;
    EXPECT_EQ(UBSHcomServiceContext::Clone(noData, oldOne, false), SER_OK);
    EXPECT_EQ(noData.MessageData(), nullptr);
    EXPECT_EQ(noData.MessageDataLen(), NN_NO0);

    // copyData = true：独立分配并复制数据，地址与源不同
    UBSHcomServiceContext withData;
    EXPECT_EQ(UBSHcomServiceContext::Clone(withData, oldOne, true), SER_OK);
    EXPECT_NE(withData.MessageData(), nullptr);
    EXPECT_NE(withData.MessageData(), oldOne.MessageData());
    EXPECT_EQ(withData.MessageDataLen(), static_cast<uint32_t>(sizeof(buf)));

    // newOne 与 oldOne 为同一对象时走自拷贝分支
    EXPECT_EQ(UBSHcomServiceContext::Clone(oldOne, oldOne, true), SER_OK);
    EXPECT_NE(oldOne.MessageData(), nullptr);
    EXPECT_EQ(oldOne.MessageDataLen(), static_cast<uint32_t>(sizeof(buf)));

    // 源对象标记为带数据但指针为空：直接返回非法参数
    UBSHcomServiceContext broken;
    broken.mDataLen = NN_NO8;
    broken.mData = nullptr;
    UBSHcomServiceContext target;
    EXPECT_EQ(UBSHcomServiceContext::Clone(target, broken, true), SER_INVALID_PARAM);
}

// 周期线程循环的退出开关由桩驱动：绝不依赖真实 epoll_wait 超时，避免用例阻塞
HcomPeriodicManager *g_periodicMgrForStop = nullptr;
uint32_t g_epollWaitCallCount = 0;
uint32_t g_maybeDumpAllCallCount = 0;
uint32_t g_callbackRunCount = 0;
uint32_t g_nullCbTraceCount = 0;

// seqNo -> timer 映射：让被 mock 的 GetSeqNoAndRemove 把出参写成 timer 自身，
// 以命中 EraseSeqNoWithRet 中 `timer != this` 的判定；未登记的 seqNo 返回 false 语义
std::map<uint32_t, HcomServiceTimer *> g_mockSeqTimerMap;

int MockEpollWaitStopLoop(int epFd, struct epoll_event *events, int maxEvents, int timeoutMs)
{
    ++g_epollWaitCallCount;
    if (g_periodicMgrForStop != nullptr) {
        g_periodicMgrForStop->mNeedStop = true;
    }
    return 0;
}

void MockMaybeDumpAllCount()
{
    ++g_maybeDumpAllCallCount;
}

void MockTraceMarkCountNullCb(HcomTimerEvent event)
{
    if (event == HcomTimerEvent::TIMEOUT_NULL_CB) {
        ++g_nullCbTraceCount;
    }
}

NResult MockGetSeqNoAndRemoveBySeqNo(uint32_t seqNo, HcomServiceTimer *&out)
{
    auto iter = g_mockSeqTimerMap.find(seqNo);
    if (iter == g_mockSeqTimerMap.end()) {
        out = nullptr; // timer != this，EraseSeqNoWithRet 返回 false
        return SER_OK;
    }
    out = iter->second;
    return SER_OK;
}

class TestHcomPeriodicManager : public testing::Test {
public:
    virtual void SetUp(void);
    virtual void TearDown(void);

    HcomPeriodicManager *mgr = nullptr;
    HcomServiceCtxStore *store = nullptr;
};

void TestHcomPeriodicManager::SetUp()
{
    mgr = new (std::nothrow) HcomPeriodicManager(NN_NO1, "periodic-manager-ut");
    ASSERT_NE(mgr, nullptr);
    // 默认 true：显式置位，保证任何用例都不会进入 RunInThread 的循环等待
    mgr->mNeedStop = true;

    g_periodicMgrForStop = nullptr;
    g_epollWaitCallCount = 0;
    g_maybeDumpAllCallCount = 0;
    g_callbackRunCount = 0;
    g_nullCbTraceCount = 0;
    g_mockSeqTimerMap.clear();
}

void TestHcomPeriodicManager::TearDown()
{
    GlobalMockObject::verify();
    g_periodicMgrForStop = nullptr;
    g_mockSeqTimerMap.clear();

    if (store != nullptr) {
        delete store;
        store = nullptr;
    }

    if (mgr != nullptr) {
        delete mgr; // 未 Start，析构中的 Stop 直接返回
        mgr = nullptr;
    }
}

HcomServiceTimer *NewTimer(uint32_t seqNo, uint64_t timeoutSecond)
{
    HcomServiceTimer *timer = new (std::nothrow) HcomServiceTimer();
    if (timer == nullptr) {
        return nullptr;
    }
    timer->SeqNo(seqNo);
    timer->mTimeout = timeoutSecond;
    return timer;
}

// 引用计数初值为 0，DecreaseRef 归零才自释放；预留 decrements 次 + 1，使断言期间对象仍存活
void AddTimerRefs(HcomServiceTimer *timer, int32_t decrements)
{
    for (int32_t i = 0; i <= decrements; i++) {
        timer->IncreaseRef();
    }
}

void PushTimerToQueue(HcomPeriodicManager *manager, uint16_t tId, uint32_t index, HcomServiceTimer *timer)
{
    std::lock_guard<std::mutex> guard(manager->mQueue[tId].lock[index]);
    manager->mQueue[tId].queue[index].push_back(timer);
}

Callback *NewCountingCallback()
{
    return UBSHcomNewCallback([](UBSHcomServiceContext &context) { ++g_callbackRunCount; }, std::placeholders::_1);
}

HcomServiceCtxStore *NewStoreForTimer()
{
    return new (std::nothrow) HcomServiceCtxStore(NN_NO1, nullptr, UBSHcomNetDriverProtocol::RDMA);
}

TEST_F(TestHcomPeriodicManager, TestProcessCleanUpInvalidTId)
{
    // tId 越界：直接返回，不做任何队列处理
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessCleanUp(static_cast<uint16_t>(M_MAX_THREAD_NUM)));
    EXPECT_TRUE(mgr->mHandleQueue[NN_NO0].empty());
}

TEST_F(TestHcomPeriodicManager, TestProcessCleanUpEmptyQueue)
{
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessCleanUp(NN_NO0));
    for (uint32_t i = 0; i < static_cast<uint32_t>(M_MAX_BATCH_NUM); i++) {
        EXPECT_TRUE(mgr->mQueue[NN_NO0].queue[i].empty());
    }
}

TEST_F(TestHcomPeriodicManager, TestProcessCleanUpWithCallback)
{
    store = NewStoreForTimer();
    ASSERT_NE(store, nullptr);
    MOCKER_CPP(&HcomServiceCtxStore::TraceMark).stubs();
    MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<HcomServiceTimer>)
        .stubs()
        .will(invoke(MockGetSeqNoAndRemoveBySeqNo));

    HcomServiceTimer *timer = NewTimer(NN_NO1, NN_NO1); // 已超时时间（仅用于标记）
    ASSERT_NE(timer, nullptr);
    timer->mCtxStore = store;
    timer->mCallback = reinterpret_cast<uintptr_t>(NewCountingCallback());
    ASSERT_NE(timer->mCallback, static_cast<uintptr_t>(NN_NO0));
    g_mockSeqTimerMap[timer->SeqNo()] = timer;
    AddTimerRefs(timer, NN_NO2); // ProcessCleanUp 会 DecreaseRef 2 次
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, timer);

    mgr->ProcessCleanUp(NN_NO0);

    EXPECT_EQ(g_callbackRunCount, static_cast<uint32_t>(NN_NO1));
    EXPECT_EQ(timer->State(), HcomAsyncCBState::CBS_TIMEOUT);
    EXPECT_TRUE(mgr->mQueue[NN_NO0].queue[NN_NO0].empty());
    timer->DecreaseRef(); // 归零自释放
}

TEST_F(TestHcomPeriodicManager, TestProcessCleanUpNullCallback)
{
    store = NewStoreForTimer();
    ASSERT_NE(store, nullptr);
    MOCKER_CPP(&HcomServiceCtxStore::TraceMark).stubs().will(invoke(MockTraceMarkCountNullCb));
    MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<HcomServiceTimer>)
        .stubs()
        .will(invoke(MockGetSeqNoAndRemoveBySeqNo));

    HcomServiceTimer *timer = NewTimer(NN_NO1, NN_NO1);
    ASSERT_NE(timer, nullptr);
    timer->mCtxStore = store;
    timer->mCallback = NN_NO0; // 空回调：走 TIMEOUT_NULL_CB 分支，不解引用空指针
    g_mockSeqTimerMap[timer->SeqNo()] = timer;
    AddTimerRefs(timer, NN_NO2);
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, timer);

    mgr->ProcessCleanUp(NN_NO0);

    EXPECT_EQ(g_callbackRunCount, static_cast<uint32_t>(NN_NO0));
    EXPECT_EQ(g_nullCbTraceCount, static_cast<uint32_t>(NN_NO1));
    EXPECT_EQ(timer->State(), HcomAsyncCBState::CBS_TIMEOUT);
    EXPECT_TRUE(mgr->mQueue[NN_NO0].queue[NN_NO0].empty());
    timer->DecreaseRef();
}

TEST_F(TestHcomPeriodicManager, TestProcessCleanUpEraseSeqNoFail)
{
    HcomServiceTimer *timer = NewTimer(NN_NO1, NN_NO1);
    ASSERT_NE(timer, nullptr);
    timer->mCtxStore = nullptr;  // EraseSeqNoWithRet 直接返回 false
    timer->mCallback = NN_NO0;   // 回调不会被触发，不构造自删除回调以免泄漏
    AddTimerRefs(timer, NN_NO1); // 只 DecreaseRef 1 次
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, timer);

    mgr->ProcessCleanUp(NN_NO0);

    EXPECT_EQ(g_callbackRunCount, static_cast<uint32_t>(NN_NO0));
    EXPECT_EQ(timer->State(), HcomAsyncCBState::CBS_INIT); // 未标记超时
    EXPECT_TRUE(mgr->mQueue[NN_NO0].queue[NN_NO0].empty());
    timer->DecreaseRef();
}

TEST_F(TestHcomPeriodicManager, TestProcessTimeOutCompressAndCollect)
{
    MOCKER_CPP(&HcomServiceCtxStore::TraceMark).stubs();

    uint64_t notTimeout = NetMonotonic::TimeSec() + NN_NO60; // 未来 60s：本用例内不会超时
    HcomServiceTimer *inflight1 = NewTimer(NN_NO1, notTimeout);
    HcomServiceTimer *finished = NewTimer(NN_NO2, notTimeout);
    HcomServiceTimer *inflight2 = NewTimer(NN_NO3, notTimeout);
    ASSERT_NE(inflight1, nullptr);
    ASSERT_NE(finished, nullptr);
    ASSERT_NE(inflight2, nullptr);
    finished->MarkFinished();
    AddTimerRefs(finished, NN_NO1); // 被摘走时 DecreaseRef 1 次
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, inflight1);
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, finished);
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, inflight2);

    mgr->ProcessTimeOut(NN_NO0);

    // 已完成的被摘走；在途的原地压缩保留且顺序不变
    ASSERT_EQ(mgr->mHandleQueue[NN_NO0].size(), static_cast<size_t>(NN_NO1));
    EXPECT_EQ(mgr->mHandleQueue[NN_NO0][NN_NO0], finished);
    ASSERT_EQ(mgr->mQueue[NN_NO0].queue[NN_NO0].size(), static_cast<size_t>(NN_NO2));
    EXPECT_EQ(mgr->mQueue[NN_NO0].queue[NN_NO0][NN_NO0], inflight1);
    EXPECT_EQ(mgr->mQueue[NN_NO0].queue[NN_NO0][NN_NO1], inflight2);
    EXPECT_EQ(inflight1->State(), HcomAsyncCBState::CBS_INIT);
    // mCtxStore 为空：摘走后不会重新标记状态
    EXPECT_EQ(finished->State(), HcomAsyncCBState::CBS_FINISHED);

    finished->DecreaseRef();
    delete inflight1;
    delete inflight2;
}

TEST_F(TestHcomPeriodicManager, TestProcessTimeOutTimeoutFired)
{
    store = NewStoreForTimer();
    ASSERT_NE(store, nullptr);
    MOCKER_CPP(&HcomServiceCtxStore::TraceMark).stubs();
    MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<HcomServiceTimer>)
        .stubs()
        .will(invoke(MockGetSeqNoAndRemoveBySeqNo));

    HcomServiceTimer *fired = NewTimer(NN_NO1, NN_NO1); // 已超时
    HcomServiceTimer *skipped = NewTimer(NN_NO2, NN_NO1);
    ASSERT_NE(fired, nullptr);
    ASSERT_NE(skipped, nullptr);
    fired->mCtxStore = store;
    fired->mCallback = reinterpret_cast<uintptr_t>(NewCountingCallback());
    skipped->mCtxStore = store;
    skipped->mCallback = NN_NO0;               // erase 失败不会触发回调，不构造自删除回调以免泄漏
    g_mockSeqTimerMap[fired->SeqNo()] = fired; // skipped 未登记 -> EraseSeqNoWithRet false
    AddTimerRefs(fired, NN_NO2);               // erase 成功：DecreaseRef 2 次
    AddTimerRefs(skipped, NN_NO1);             // erase 失败：DecreaseRef 1 次
    // 同一批次按插入顺序处理，保证与 seqNo 映射的行为一一对应
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, fired);
    PushTimerToQueue(mgr, NN_NO0, NN_NO0, skipped);

    mgr->ProcessTimeOut(NN_NO0);

    EXPECT_EQ(g_callbackRunCount, static_cast<uint32_t>(NN_NO1)); // 只有 fired 触发回调
    EXPECT_EQ(fired->State(), HcomAsyncCBState::CBS_TIMEOUT);
    EXPECT_EQ(skipped->State(), HcomAsyncCBState::CBS_INIT);                  // 未登记 seqNo，跳过标记与回调
    EXPECT_EQ(mgr->mHandleQueue[NN_NO0].size(), static_cast<size_t>(NN_NO2)); // 超时的都被摘走
    EXPECT_TRUE(mgr->mQueue[NN_NO0].queue[NN_NO0].empty());
    fired->DecreaseRef();
    skipped->DecreaseRef();
}

TEST_F(TestHcomPeriodicManager, TestProcessTimeOutMultiBatchNullCallback)
{
    store = NewStoreForTimer();
    ASSERT_NE(store, nullptr);
    MOCKER_CPP(&HcomServiceCtxStore::TraceMark).stubs().will(invoke(MockTraceMarkCountNullCb));
    MOCKER_CPP(&HcomServiceCtxStore::GetSeqNoAndRemove<HcomServiceTimer>)
        .stubs()
        .will(invoke(MockGetSeqNoAndRemoveBySeqNo));

    HcomServiceTimer *timer3 = NewTimer(NN_NO3, NN_NO1);
    HcomServiceTimer *timer15 = NewTimer(NN_NO4, NN_NO1);
    ASSERT_NE(timer3, nullptr);
    ASSERT_NE(timer15, nullptr);
    timer3->mCtxStore = store;
    timer3->mCallback = NN_NO0;
    timer15->mCtxStore = store;
    timer15->mCallback = NN_NO0;
    g_mockSeqTimerMap[timer3->SeqNo()] = timer3;
    g_mockSeqTimerMap[timer15->SeqNo()] = timer15;
    AddTimerRefs(timer3, NN_NO2);
    AddTimerRefs(timer15, NN_NO2);
    // 分散在不同批次：覆盖 15 -> 0 的逆序扫描
    PushTimerToQueue(mgr, NN_NO0, NN_NO3, timer3);
    PushTimerToQueue(mgr, NN_NO0, NN_NO15, timer15);

    mgr->ProcessTimeOut(NN_NO0);

    EXPECT_EQ(g_callbackRunCount, static_cast<uint32_t>(NN_NO0));
    EXPECT_EQ(g_nullCbTraceCount, static_cast<uint32_t>(NN_NO2)); // 两个空回调都被标记
    EXPECT_EQ(mgr->mHandleQueue[NN_NO0].size(), static_cast<size_t>(NN_NO2));
    EXPECT_TRUE(mgr->mQueue[NN_NO0].queue[NN_NO3].empty());
    EXPECT_TRUE(mgr->mQueue[NN_NO0].queue[NN_NO15].empty());
    timer3->DecreaseRef();
    timer15->DecreaseRef();
}

TEST_F(TestHcomPeriodicManager, TestProcessTimeOutInvalidTId)
{
    EXPECT_NO_FATAL_FAILURE(mgr->ProcessTimeOut(static_cast<uint16_t>(M_MAX_THREAD_NUM)));
    EXPECT_TRUE(mgr->mHandleQueue[NN_NO0].empty());
}

TEST_F(TestHcomPeriodicManager, TestRunInThreadInvalidTId)
{
    mgr->mThreadCount = NN_NO1;
    int16_t before = mgr->mStartedWorkingThreads.load();
    EXPECT_NO_FATAL_FAILURE(mgr->RunInThread(static_cast<int16_t>(NN_NO1))); // tId >= mThreadCount
    // 线程计数在越界判断之前自增
    EXPECT_EQ(mgr->mStartedWorkingThreads.load(), static_cast<int16_t>(before + NN_NO1));
    EXPECT_EQ(g_epollWaitCallCount, static_cast<uint32_t>(NN_NO0));
}

TEST_F(TestHcomPeriodicManager, TestRunInThreadEpollCreateFail)
{
    // 涉外系统调用打桩：epoll_create 失败分支
    MOCKER(::epoll_create).stubs().will(returnValue(static_cast<int>(NN_NOF1)));
    int16_t before = mgr->mStartedWorkingThreads.load();

    EXPECT_NO_FATAL_FAILURE(mgr->RunInThread(NN_NO0));

    EXPECT_EQ(mgr->mStartedWorkingThreads.load(), static_cast<int16_t>(before + NN_NO1));
    EXPECT_EQ(g_epollWaitCallCount, static_cast<uint32_t>(NN_NO0)); // 未进入循环
}

TEST_F(TestHcomPeriodicManager, TestRunInThreadOnceAndExit)
{
    // 循环内可能阻塞的接口全部打桩，并由桩驱动 mNeedStop 退出循环
    // epoll_create 不阻塞，返回真实 fd 后由 NN_SafeCloseFd 正常关闭，故不打桩
    MOCKER(::epoll_wait).stubs().will(invoke(MockEpollWaitStopLoop));
    MOCKER(HcomServiceCtxStore::MaybeDumpAll).stubs().will(invoke(MockMaybeDumpAllCount));
    g_periodicMgrForStop = mgr;
    mgr->mNeedStop = false;
    int16_t before = mgr->mStartedWorkingThreads.load();

    EXPECT_NO_FATAL_FAILURE(mgr->RunInThread(NN_NO0)); // tId 0：额外覆盖 VERSION banner 分支

    EXPECT_EQ(g_epollWaitCallCount, static_cast<uint32_t>(NN_NO1)); // 恰好 1 轮循环
    EXPECT_EQ(g_maybeDumpAllCallCount, static_cast<uint32_t>(NN_NO1));
    EXPECT_EQ(mgr->mStartedWorkingThreads.load(), static_cast<int16_t>(before + NN_NO1));
    EXPECT_TRUE(mgr->mNeedStop);
}

TEST_F(TestHcomPeriodicManager, TestRunInThreadNonZeroTId)
{
    // tId != 0 且进入循环：覆盖 banner 的假分支与循环内 MaybeDumpAll 的假分支
    MOCKER(::epoll_wait).stubs().will(invoke(MockEpollWaitStopLoop));
    MOCKER(HcomServiceCtxStore::MaybeDumpAll).stubs().will(invoke(MockMaybeDumpAllCount));
    g_periodicMgrForStop = mgr;
    mgr->mThreadCount = NN_NO2;
    mgr->mNeedStop = false;
    int16_t before = mgr->mStartedWorkingThreads.load();

    EXPECT_NO_FATAL_FAILURE(mgr->RunInThread(static_cast<int16_t>(NN_NO1)));

    EXPECT_EQ(g_epollWaitCallCount, static_cast<uint32_t>(NN_NO1));    // 恰好 1 轮循环
    EXPECT_EQ(g_maybeDumpAllCallCount, static_cast<uint32_t>(NN_NO0)); // tId != 0 不 dump
    EXPECT_EQ(mgr->mStartedWorkingThreads.load(), static_cast<int16_t>(before + NN_NO1));
    EXPECT_TRUE(mgr->mNeedStop);
}

TEST_F(TestHcomPeriodicManager, TestRunInThreadNeedStopTrue)
{
    MOCKER(::epoll_wait).stubs().will(invoke(MockEpollWaitStopLoop));
    mgr->mNeedStop = true; // 循环条件为假：不进入循环体
    int16_t before = mgr->mStartedWorkingThreads.load();

    // tId 必须小于 mThreadCount 才会走到循环判断（否则命中越界分支）
    EXPECT_NO_FATAL_FAILURE(mgr->RunInThread(NN_NO0));

    EXPECT_EQ(g_epollWaitCallCount, static_cast<uint32_t>(NN_NO0));
    EXPECT_EQ(g_maybeDumpAllCallCount, static_cast<uint32_t>(NN_NO0));
    EXPECT_EQ(mgr->mStartedWorkingThreads.load(), static_cast<int16_t>(before + NN_NO1));
}

} // namespace hcom
} // namespace ock
