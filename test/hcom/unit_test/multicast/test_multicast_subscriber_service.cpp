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

#include "multicast_subscriber_service_imp.h"

namespace ock {
namespace hcom {
namespace {
constexpr uint64_t TEST_EP_ID_A = 101;
constexpr char SUBSCRIBER_IP_A[] = "10.10.0.1";
constexpr char NO_SCHEME_URL[] = "127.0.0.1:9981";
constexpr uint16_t TEST_SUB_PORT = 9981;

class SubscriberTestEndpoint : public UBSHcomNetEndpoint {
public:
    SubscriberTestEndpoint() : UBSHcomNetEndpoint(TEST_EP_ID_A, UBSHcomNetWorkerIndex()) {}

    ~SubscriberTestEndpoint() override = default;

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

    void Close() {}

private:
    std::string mPeer = SUBSCRIBER_IP_A;
};
} // namespace

class TestMulticastSubscriberService : public testing::Test {
public:
    TestMulticastSubscriberService() = default;
    ~TestMulticastSubscriberService() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestMulticastSubscriberService::SetUp() {}

void TestMulticastSubscriberService::TearDown() {}

TEST_F(TestMulticastSubscriberService, TestRegisterHandlers)
{
    SubscriberServiceImp service;
    int recvCount = 0;
    int brokenCount = 0;
    service.RegisterRecvHandler([&recvCount](UBSHcomServiceContext &) {
        recvCount++;
        return 0;
    });
    service.RegisterBrokenHandler([&brokenCount](const UBSHcomNetEndpointPtr &) { brokenCount++; });
    service.RegisterTLSCaCallback(nullptr);
    service.RegisterTLSCertificationCallback(nullptr);
    service.RegisterTLSPrivateKeyCallback(nullptr);
    EXPECT_EQ(service.GetConfig().GetName(), "");
}

TEST_F(TestMulticastSubscriberService, TestServiceEndPointBroken)
{
    SubscriberServiceImp service;
    auto *ep = new SubscriberTestEndpoint();
    UBSHcomNetEndpointPtr epPtr(ep);
    // no handler registered, nothing happens
    service.ServiceEndPointBroken(epPtr);
    int brokenCount = 0;
    service.mEpBrokenHandler = [&brokenCount](const UBSHcomNetEndpointPtr &) {
        brokenCount++;
    };
    service.ServiceEndPointBroken(epPtr);
    EXPECT_EQ(brokenCount, 1);
}

TEST_F(TestMulticastSubscriberService, TestStartInvalidConfig)
{
    SubscriberServiceImp service;
    service.mCfg.mOptions.timeOutDetectThreadNum = 0;
    EXPECT_EQ(service.Start(), SER_INVALID_PARAM);
}

TEST_F(TestMulticastSubscriberService, TestCreateSubscriberInvalidStates)
{
    SubscriberServiceImp service;
    NetRef<Subscriber> subscriber;
    // service not started
    EXPECT_EQ(service.CreateSubscriber("tcp://127.0.0.1:9981", subscriber), SER_STOP);

    // started, but the url is not parseable; driver is absent so stop before connect
    service.mStarted = true;
    SerResult ret = service.CreateSubscriber(NO_SCHEME_URL, subscriber);
    EXPECT_TRUE(ret == NN_PARAM_INVALID || ret == NN_INVALID_PARAM);
    service.mStarted = false;
}

TEST_F(TestMulticastSubscriberService, TestDestroySubscriberNull)
{
    SubscriberServiceImp service;
    NetRef<Subscriber> subscriber;
    service.DestroySubscriber(subscriber);
}

TEST_F(TestMulticastSubscriberService, TestReleaseDriverWithoutDriver)
{
    SubscriberServiceImp service;
    service.ReleaseDriver();
}

TEST_F(TestMulticastSubscriberService, TestSubscriberBasics)
{
    // null endpoint subscriber
    auto *nullEpSub = new Subscriber(SUBSCRIBER_IP_A, TEST_SUB_PORT, nullptr);
    EXPECT_EQ(nullEpSub->GetIp(), SUBSCRIBER_IP_A);
    EXPECT_EQ(nullEpSub->GetPort(), TEST_SUB_PORT);
    EXPECT_EQ(nullEpSub->GetEp().Get(), nullptr);
    nullEpSub->Close(); // no endpoint, close is a no-op
    delete nullEpSub;

    // subscriber with endpoint, close delegates to the endpoint
    auto *ep = new SubscriberTestEndpoint();
    auto *sub = new Subscriber(SUBSCRIBER_IP_A, TEST_SUB_PORT, UBSHcomNetEndpointPtr(ep));
    EXPECT_EQ(sub->GetEp().Get(), ep);
    sub->Close();
    sub->DecreaseRef(); // refcount back to zero, object frees itself
}

TEST_F(TestMulticastSubscriberService, TestDestroySubscriberWithEndpoint)
{
    SubscriberServiceImp service;
    auto *ep = new SubscriberTestEndpoint();
    // note: DestroySubscriber takes a const NetRef and releases the object,
    // the caller's NetRef is not cleared (kept non-null here on purpose)
    NetRef<Subscriber> subscriber(new Subscriber(SUBSCRIBER_IP_A, TEST_SUB_PORT, UBSHcomNetEndpointPtr(ep)));
    Subscriber *raw = subscriber.Get();
    service.DestroySubscriber(subscriber);
    EXPECT_EQ(subscriber.Get(), raw);
}

TEST_F(TestMulticastSubscriberService, TestSubscriberServiceSingletonLifecycle)
{
    MulticastServiceOptions badSize;
    badSize.maxSendRecvDataSize = 0;
    EXPECT_EQ(SubscriberService::Create("ut-sub-service", badSize), nullptr);

    MulticastServiceOptions opt;
    SubscriberService *service = SubscriberService::Create("ut-sub-service", opt);
    ASSERT_NE(service, nullptr);
    EXPECT_EQ(SubscriberService::Create("ut-sub-service", opt), service);
    EXPECT_EQ(SubscriberService::Destroy("no-such-service"), SER_ERROR);
    EXPECT_EQ(SubscriberService::Destroy("ut-sub-service"), SER_OK);
    EXPECT_EQ(SubscriberService::Destroy("ut-sub-service"), SER_ERROR);
}
} // namespace hcom
} // namespace ock
