/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>

#include "multicast/include/multicast_publisher.h"
#include "multicast/include/multicast_service_callback.h"
#include "multicast_periodic_manager.h"
#include "net_common.h"

namespace ock {
namespace hcom {
namespace {
constexpr uint64_t TEST_EP_ID_A = 101;
constexpr uint64_t TEST_EP_ID_B = 102;
constexpr uint32_t TEST_SUB_ID_A = 11;
constexpr uint32_t TEST_SUB_ID_B = 12;
constexpr uint16_t TEST_SUB_PORT = 9981;
constexpr uint32_t TEST_MAX_SUBSCRIBER_NUM = 8;
constexpr char SUBSCRIBER_IP_A[] = "10.10.0.1";
constexpr char SUBSCRIBER_IP_B[] = "10.10.0.2";
constexpr char UNKNOWN_REMOTE_IP[] = "10.10.0.99";
constexpr int16_t TEST_IO_TIMEOUT = -1;
constexpr uint64_t TEST_UNKNOWN_EP_ID = 999;
constexpr uint16_t CALL_FLOW_THREAD_COUNT = 1;
constexpr int CALL_FLOW_NO_CPU_BIND = -1;
constexpr uint32_t CALL_FLOW_IO_CAPACITY = 8;
constexpr uint32_t CALL_FLOW_MAX_SUBSCRIBER_NUM = 8;

void CountRun(int *counter, PublisherContext &)
{
    (*counter)++;
}
} // namespace

namespace {
// minimal endpoint stub, only used as a non-null placeholder in subscription info
class TestEndpoint : public UBSHcomNetEndpoint {
public:
    TestEndpoint() : UBSHcomNetEndpoint(TEST_EP_ID_A, UBSHcomNetWorkerIndex()) {}

    ~TestEndpoint() override = default;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override
    {
        (void)epOptions;
        return NN_OK;
    }

    uint32_t GetSendQueueCount() override
    {
        return 0;
    }

    const std::string &PeerIpAndPort() override
    {
        return mPeer;
    }

    const std::string &UdsName() override
    {
        return mPeer;
    }

    bool GetPeerIpPort(std::string &ip, uint16_t &port) override
    {
        ip = mPeer;
        port = TEST_SUB_PORT;
        return false;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNo) override
    {
        (void)opCode;
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
                     const UBSHcomNetTransOpInfo &opInfo) override
    {
        (void)opCode;
        (void)request;
        (void)opInfo;
        return NN_ERROR;
    }

    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo) override
    {
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo) override
    {
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostSendRawNoCpy(const UBSHcomNetTransRequest &request, uint32_t seqNo) override
    {
        (void)request;
        (void)seqNo;
        return NN_ERROR;
    }

    NResult PostRead(const UBSHcomNetTransRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult PostRead(const UBSHcomNetTransSglRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult PostWrite(const UBSHcomNetTransRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult PostWrite(const UBSHcomNetTransSglRequest &request) override
    {
        (void)request;
        return NN_ERROR;
    }

    NResult WaitCompletion(int32_t timeout) override
    {
        (void)timeout;
        return NN_ERROR;
    }

    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        (void)timeout;
        (void)ctx;
        return NN_ERROR;
    }

    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override
    {
        (void)timeout;
        (void)ctx;
        return NN_ERROR;
    }

private:
    std::string mPeer = SUBSCRIBER_IP_A;
};
} // namespace

class TestMulticastPublisher : public testing::Test {
public:
    TestMulticastPublisher() = default;
    ~TestMulticastPublisher() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);

    SubscriptionInfoPtr MakeSubscription(uint64_t epId, uint32_t subId, std::string &ip, UBSHcomNetEndpoint *ep);
};

void TestMulticastPublisher::SetUp() {}

void TestMulticastPublisher::TearDown() {}

SubscriptionInfoPtr TestMulticastPublisher::MakeSubscription(uint64_t epId, uint32_t subId, std::string &ip,
                                                             UBSHcomNetEndpoint *ep)
{
    return SubscriptionInfoPtr(
        new SubscriptionInfo(epId, "sub" + std::to_string(subId), ip, TEST_SUB_PORT, UBSHcomNetEndpointPtr(ep)));
}

TEST_F(TestMulticastPublisher, TestEmptyPublisherQueries)
{
    Publisher publisher("ut-publisher");
    EXPECT_EQ(publisher.GetSubscriberNum(), 0);
    EXPECT_TRUE(publisher.GetAllSubscriberInfo().empty());
    EXPECT_EQ(publisher.GetSubscribeByEpId(TEST_EP_ID_A), nullptr);
}

TEST_F(TestMulticastPublisher, TestAddSubscriptionInvalidParams)
{
    Publisher publisher("ut-publisher");
    SubscriptionInfoPtr nullInfo = nullptr;
    EXPECT_FALSE(publisher.AddSubscription(nullInfo));
    // subscription without endpoint is rejected
    std::string ip = SUBSCRIBER_IP_A;
    SubscriptionInfoPtr noEpSub(new SubscriptionInfo(TEST_SUB_ID_A, "subA", ip, TEST_SUB_PORT, nullptr));
    EXPECT_FALSE(publisher.AddSubscription(noEpSub));
    EXPECT_FALSE(publisher.DelSubscription(nullInfo));
    EXPECT_FALSE(publisher.DelSubscription(noEpSub));
    EXPECT_EQ(publisher.GetSubscriberNum(), 0);
}

TEST_F(TestMulticastPublisher, TestAddAndDelSubscription)
{
    Publisher publisher("ut-publisher");
    // endpoints are owned by NetRef inside subscription info, allocate on heap
    auto *epA = new TestEndpoint();
    auto *epB = new TestEndpoint();
    std::string ipA = SUBSCRIBER_IP_A;
    std::string ipB = SUBSCRIBER_IP_B;
    SubscriptionInfoPtr subA = MakeSubscription(TEST_EP_ID_A, TEST_SUB_ID_A, ipA, epA);
    SubscriptionInfoPtr subB = MakeSubscription(TEST_EP_ID_B, TEST_SUB_ID_B, ipB, epB);
    ASSERT_TRUE(publisher.AddSubscription(subA));
    ASSERT_TRUE(publisher.AddSubscription(subB));
    EXPECT_EQ(publisher.GetSubscriberNum(), 2);

    SubscriptionInfoPtr found = publisher.GetSubscribeByEpId(TEST_EP_ID_A);
    ASSERT_NE(found.Get(), nullptr);
    EXPECT_EQ(found->GetId(), TEST_EP_ID_A);
    EXPECT_EQ(found->GetIp(), SUBSCRIBER_IP_A);
    EXPECT_EQ(publisher.GetSubscribeByEpId(TEST_UNKNOWN_EP_ID), nullptr);

    auto all = publisher.GetAllSubscriberInfo();
    EXPECT_EQ(all.size(), 2);

    // add twice is rejected by map emplace, keeps single entry
    EXPECT_TRUE(publisher.AddSubscription(subA));
    EXPECT_EQ(publisher.GetSubscriberNum(), 2);

    EXPECT_TRUE(publisher.DelSubscription(subB));
    EXPECT_EQ(publisher.GetSubscriberNum(), 1);
    EXPECT_EQ(publisher.GetSubscribeByEpId(TEST_EP_ID_B), nullptr);
}

TEST_F(TestMulticastPublisher, TestSelectSubscriptionRoundRobin)
{
    SubscriptionGroup group;
    std::string ip = SUBSCRIBER_IP_A;
    SubscriptionInfoPtr subA(new SubscriptionInfo(TEST_EP_ID_A, "subA", ip, TEST_SUB_PORT, nullptr));
    SubscriptionInfoPtr subB(new SubscriptionInfo(TEST_EP_ID_B, "subB", ip, TEST_SUB_PORT, nullptr));
    // empty group returns null
    EXPECT_EQ(Publisher::SelectSubscription(group), nullptr);
    group.subscribers.emplace_back(subA);
    group.subscribers.emplace_back(subB);
    EXPECT_EQ(Publisher::SelectSubscription(group).Get(), subA.Get());
    EXPECT_EQ(Publisher::SelectSubscription(group).Get(), subB.Get());
    // round robin wraps back to the first subscriber
    EXPECT_EQ(Publisher::SelectSubscription(group).Get(), subA.Get());
}

TEST_F(TestMulticastPublisher, TestCallWithoutSubscriber)
{
    Publisher publisher("ut-publisher");
    UBSHcomNetTransOpInfo opInfo(0, TEST_IO_TIMEOUT);
    MultiRequest req(nullptr, 0);
    // no subscriber
    EXPECT_EQ(publisher.Call(opInfo, req, nullptr), SER_INVALID_PARAM);
    // null callback
    int runCount = 0;
    MultiCastCallback *done = NewMultiCastCallback(&CountRun, &runCount, std::placeholders::_1);
    EXPECT_EQ(publisher.Call(opInfo, req, done), SER_INVALID_PARAM);
    // done is destroyed as non-permanent, run never triggered
    EXPECT_EQ(runCount, 0);
}

TEST_F(TestMulticastPublisher, TestCallByIpWithoutSubscriber)
{
    Publisher publisher("ut-publisher");
    UBSHcomNetTransOpInfo opInfo(0, TEST_IO_TIMEOUT);
    MultiRequest req(nullptr, 0);
    // empty remote ip
    EXPECT_EQ(publisher.Call(opInfo, "", req, nullptr), SER_INVALID_PARAM);
    // unknown remote ip, no subscriber registered
    EXPECT_EQ(publisher.Call(opInfo, UNKNOWN_REMOTE_IP, req, nullptr), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPublisher, TestInitIoSubscribersWithoutSnapshot)
{
    Publisher publisher("ut-publisher");
    MultiCastIoContext ctx;
    ctx.Initialize(0, TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(publisher.InitIoSubscribers(ctx), SER_INVALID_PARAM);
    EXPECT_EQ(publisher.InitIoSubscriber(ctx, UNKNOWN_REMOTE_IP), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPublisher, TestInitIoSubscribersWithSubscription)
{
    Publisher publisher("ut-publisher");
    auto *epA = new TestEndpoint();
    auto *epB = new TestEndpoint();
    std::string ipA = SUBSCRIBER_IP_A;
    std::string ipB = SUBSCRIBER_IP_B;
    SubscriptionInfoPtr subA = MakeSubscription(TEST_EP_ID_A, TEST_SUB_ID_A, ipA, epA);
    SubscriptionInfoPtr subB = MakeSubscription(TEST_EP_ID_B, TEST_SUB_ID_B, ipB, epB);
    ASSERT_TRUE(publisher.AddSubscription(subA));
    ASSERT_TRUE(publisher.AddSubscription(subB));

    MultiCastIoContext ctx;
    ctx.Initialize(0, TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(publisher.InitIoSubscribers(ctx), SER_OK);
    EXPECT_EQ(ctx.GetPublisherContext().GetSubscriberRspInfo().size(), 2);

    // reset and select subscriber on one remote ip only
    ctx.GetPublisherContext().Reset();
    EXPECT_EQ(publisher.InitIoSubscriber(ctx, SUBSCRIBER_IP_A), SER_OK);
    EXPECT_EQ(ctx.GetPublisherContext().GetSubscriberRspInfo().size(), 1);

    ctx.GetPublisherContext().Reset();
    EXPECT_EQ(publisher.InitIoSubscriber(ctx, UNKNOWN_REMOTE_IP), SER_INVALID_PARAM);
    EXPECT_TRUE(ctx.GetPublisherContext().GetSubscriberRspInfo().empty());
}

namespace {
// endpoint whose raw no-copy send succeeds, used for the Call flow test
class TestEndpointOk : public TestEndpoint {
public:
    NResult PostSendRawNoCpy(const UBSHcomNetTransRequest &request, uint32_t seqNo) override
    {
        (void)request;
        (void)seqNo;
        return NN_OK;
    }
};
} // namespace

TEST_F(TestMulticastPublisher, TestCallFlowSendAllFailed)
{
    MultiCastPeriodicManager mgr(CALL_FLOW_THREAD_COUNT, "ut-mgr", CALL_FLOW_NO_CPU_BIND, CALL_FLOW_IO_CAPACITY,
                                 CALL_FLOW_MAX_SUBSCRIBER_NUM);
    ASSERT_EQ(mgr.InitializeIoContexts(), SER_OK);
    Publisher *publisher = new Publisher("ut-publisher");
    publisher->IncreaseRef();
    // wire the periodic manager so Call can acquire io contexts
    publisher->mPeriodicMgr = reinterpret_cast<uintptr_t>(&mgr);
    auto *ep = new TestEndpoint(); // raw send always fails
    std::string ipA = SUBSCRIBER_IP_A;
    SubscriptionInfoPtr subA = MakeSubscription(TEST_EP_ID_A, TEST_SUB_ID_A, ipA, ep);
    ASSERT_TRUE(publisher->AddSubscription(subA));

    UBSHcomNetTransOpInfo opInfo(0, TEST_IO_TIMEOUT);
    MultiRequest req(nullptr, 0);
    int runCount = 0;
    MultiCastCallback *done = NewMultiCastCallback(&CountRun, &runCount, std::placeholders::_1);
    // the only subscriber fails to send: callback destroyed, no run
    EXPECT_EQ(publisher->Call(opInfo, req, done), SER_MULTICAST_SEND_ALL_FAILED);
    EXPECT_EQ(runCount, 0);
    mgr.UnInitializeIoContexts();
    publisher->DecreaseRef();
}

TEST_F(TestMulticastPublisher, TestCallFlowSendPending)
{
    MultiCastPeriodicManager mgr(CALL_FLOW_THREAD_COUNT, "ut-mgr", CALL_FLOW_NO_CPU_BIND, CALL_FLOW_IO_CAPACITY,
                                 CALL_FLOW_MAX_SUBSCRIBER_NUM);
    ASSERT_EQ(mgr.InitializeIoContexts(), SER_OK);
    Publisher *publisher = new Publisher("ut-publisher");
    publisher->IncreaseRef();
    publisher->mPeriodicMgr = reinterpret_cast<uintptr_t>(&mgr);
    auto *ep = new TestEndpointOk(); // raw send succeeds
    std::string ipA = SUBSCRIBER_IP_A;
    SubscriptionInfoPtr subA = MakeSubscription(TEST_EP_ID_A, TEST_SUB_ID_A, ipA, ep);
    ASSERT_TRUE(publisher->AddSubscription(subA));

    UBSHcomNetTransOpInfo opInfo(0, TEST_IO_TIMEOUT);
    MultiRequest req(nullptr, 0);
    int runCount = 0;
    MultiCastCallback *done = NewMultiCastCallback(&CountRun, &runCount, std::placeholders::_1);
    // send succeeded but no reply yet: call returns ok and the request stays pending
    EXPECT_EQ(publisher->Call(opInfo, req, done), SER_OK);
    EXPECT_EQ(runCount, 0);
    // teardown finalizes the pending context and runs the callback with broken status
    mgr.UnInitializeIoContexts();
    EXPECT_EQ(runCount, 1);
    publisher->DecreaseRef();
}
} // namespace hcom
} // namespace ock
