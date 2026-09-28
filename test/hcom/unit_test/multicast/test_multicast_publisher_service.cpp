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

#include "multicast/include/multicast_publisher.h"
#include "multicast_periodic_manager.h"
#include "multicast_publisher_service_imp.h"

namespace ock {
namespace hcom {
namespace {
constexpr uint64_t TEST_EP_ID_A = 101;
constexpr uint32_t TEST_SUB_ID_A = 11;
constexpr uint16_t TEST_SUB_PORT = 9981;
constexpr char SUBSCRIBER_IP_A[] = "10.10.0.1";
constexpr char LISTEN_URL[] = "127.0.0.1:9981";
constexpr char TCP_LISTEN_URL[] = "tcp://127.0.0.1:9981";
constexpr char RDMA_LISTEN_URL[] = "rdma://127.0.0.1:9981";
constexpr char BAD_LISTEN_URL[] = "not-a-url";
constexpr char SERVICE_NAME[] = "ut-pub-service";
constexpr uint32_t TEST_WORKER_GROUP_ID = 1;
constexpr uint32_t TEST_WORKER_THREAD_COUNT = 1;
constexpr int8_t TEST_WORKER_PRIORITY = 0;
constexpr uint32_t INVALID_THREAD_NUM = 5;
constexpr uint32_t VALID_THREAD_NUM = 1;
constexpr uint16_t DELAY_TIME_1S = 1;

// minimal endpoint stub reused from the publisher test (kept local to stay self-contained)
class ServiceTestEndpoint : public UBSHcomNetEndpoint {
public:
    explicit ServiceTestEndpoint(uint64_t id) : UBSHcomNetEndpoint(id, UBSHcomNetWorkerIndex()) {}

    ~ServiceTestEndpoint() override = default;

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

class TestMulticastPublisherService : public testing::Test {
public:
    TestMulticastPublisherService() = default;
    ~TestMulticastPublisherService() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);

    static SubscriptionInfoPtr MakeSubscription(UBSHcomNetEndpoint *ep);
};

void TestMulticastPublisherService::SetUp() {}

void TestMulticastPublisherService::TearDown() {}

SubscriptionInfoPtr TestMulticastPublisherService::MakeSubscription(UBSHcomNetEndpoint *ep)
{
    std::string ip = SUBSCRIBER_IP_A;
    return SubscriptionInfoPtr(new SubscriptionInfo(TEST_EP_ID_A, "sub", ip, TEST_SUB_PORT, UBSHcomNetEndpointPtr(ep)));
}

TEST_F(TestMulticastPublisherService, TestRegisterHandlersAndWorkerGroup)
{
    PublisherServiceImp service;
    service.RegisterBrokenHandler([](const UBSHcomNetEndpointPtr &) {});
    service.RegisterPubRecvHandler([](PublisherContext &) { return 0; });
    service.RegisterSendHandler([](UBSHcomServiceContext &) { return 0; });
    service.RegisterTLSCaCallback(nullptr);
    service.RegisterTLSCertificationCallback(nullptr);
    service.RegisterTLSPrivateKeyCallback(nullptr);
    service.RegisterSubscriptionExceptionHandler([](SubscriptionInfo &) {});
    service.AddWorkerGroup(TEST_WORKER_GROUP_ID, TEST_WORKER_THREAD_COUNT, {UINT32_MAX, UINT32_MAX},
                           TEST_WORKER_PRIORITY);
    EXPECT_EQ(service.GetConfig().GetWorkerGroupInfo().size(), 1);
}

TEST_F(TestMulticastPublisherService, TestBindInvalidParams)
{
    PublisherServiceImp service;
    NewSubscriptionHandler handler = [](SubscriptionInfoPtr &) {
        return 0;
    };
    EXPECT_EQ(service.Bind("", handler), SER_INVALID_PARAM);
    EXPECT_EQ(service.Bind(TCP_LISTEN_URL, nullptr), SER_INVALID_PARAM);
    EXPECT_EQ(service.Bind(BAD_LISTEN_URL, handler), SER_INVALID_PARAM);
    // only tcp protocol is supported
    EXPECT_EQ(service.Bind(RDMA_LISTEN_URL, handler), SER_INVALID_PARAM);
    EXPECT_EQ(service.Bind(TCP_LISTEN_URL, handler), SER_OK);
    // duplicated listener is rejected
    EXPECT_EQ(service.Bind(TCP_LISTEN_URL, handler), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPublisherService, TestStartInvalidParams)
{
    PublisherServiceImp service;
    // invalid timeout detect thread num is rejected first
    service.mCfg.mOptions.timeOutDetectThreadNum = 0;
    EXPECT_EQ(service.Start(), SER_INVALID_PARAM);
    service.mCfg.mOptions.timeOutDetectThreadNum = 1;
    // new subscription handler must be registered before start
    EXPECT_EQ(service.Start(), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPublisherService, TestCreateResourceInvalidParams)
{
    PublisherServiceImp service;
    EXPECT_EQ(service.CreateResource(0), SER_INVALID_PARAM);
    EXPECT_EQ(service.CreateResource(INVALID_THREAD_NUM), SER_INVALID_PARAM);
}

TEST_F(TestMulticastPublisherService, TestCreateResourceAndRelease)
{
    PublisherServiceImp service;
    ASSERT_EQ(service.CreateResource(VALID_THREAD_NUM), SER_OK);
    EXPECT_NE(service.mPeriodicMgr.Get(), nullptr);
    EXPECT_NE(service.mCtxMemPool.Get(), nullptr);
    service.mPeriodicMgr->Stop();
    service.mPeriodicMgr.Set(nullptr);
    service.mCtxMemPool.Set(nullptr);
}

TEST_F(TestMulticastPublisherService, TestRegisterMemoryRegionWithoutDriver)
{
    PublisherServiceImp service;
    UBSHcomNetMemoryRegionPtr mr;
    EXPECT_EQ(service.RegisterMemoryRegion(static_cast<uint64_t>(1024), mr), NN_ERROR);
    EXPECT_EQ(service.RegisterMemoryRegion(reinterpret_cast<uintptr_t>(nullptr), 1024, mr), NN_ERROR);
    service.DestroyMemoryRegion(mr);
}

TEST_F(TestMulticastPublisherService, TestNewSubscriptionCallbackPaths)
{
    PublisherServiceImp service;
    auto *ep = new ServiceTestEndpoint(TEST_EP_ID_A);
    UBSHcomNetEndpointPtr epPtr(ep);
    // publisher not ready
    EXPECT_EQ(service.NewSubscriptionCallback(TCP_LISTEN_URL, epPtr, ""), SER_ERROR);

    service.mPublisher = PublisherPtr(new Publisher(SERVICE_NAME));
    // subscriber limit reached
    service.mCfg.mOptions.maxSubscriberNum = 0;
    EXPECT_EQ(service.NewSubscriptionCallback(TCP_LISTEN_URL, epPtr, ""), SER_ERROR);
    service.mCfg.mOptions.maxSubscriberNum = 8;
    // bad ip:port
    EXPECT_EQ(service.NewSubscriptionCallback(BAD_LISTEN_URL, epPtr, ""), SER_INVALID_PARAM);
    // handler reports failure
    service.mNewSubScriptionHandler = [](SubscriptionInfoPtr &) {
        return SER_ERROR;
    };
    EXPECT_EQ(service.NewSubscriptionCallback(TCP_LISTEN_URL, epPtr, ""), SER_ERROR);
    // happy path
    service.mNewSubScriptionHandler = [&service](SubscriptionInfoPtr &info) {
        return service.mPublisher->AddSubscription(info) ? SER_OK : SER_ERROR;
    };
    EXPECT_EQ(service.NewSubscriptionCallback(TCP_LISTEN_URL, epPtr, ""), SER_OK);
    EXPECT_EQ(service.mPublisher->GetSubscriberNum(), 1);
}

TEST_F(TestMulticastPublisherService, TestEpBrokenAndDelayEraseWithoutCtxStore)
{
    PublisherServiceImp service;
    auto *ep = new ServiceTestEndpoint(TEST_EP_ID_A);
    UBSHcomNetEndpointPtr epPtr(ep);
    // broken handler must be registered first
    EXPECT_EQ(service.EpBrokenCallback(epPtr), SER_ERROR);

    service.mPublisher = PublisherPtr(new Publisher(SERVICE_NAME));
    int brokenCount = 0;
    service.mPubBrokenHandler = [&brokenCount](const UBSHcomNetEndpointPtr &) {
        brokenCount++;
    };
    // ctx store is not initialized, delayed erase falls back to an error
    EXPECT_EQ(service.EpBrokenCallback(epPtr), SER_ERROR);
    EXPECT_EQ(brokenCount, 1);
    // direct delay erase hits the same path
    EXPECT_EQ(service.DelayEraseEp(epPtr, DELAY_TIME_1S), SER_ERROR);
}

TEST_F(TestMulticastPublisherService, TestEraseEpCb)
{
    PublisherServiceImp service;
    service.mPublisher = PublisherPtr(new Publisher(SERVICE_NAME));
    auto *ep = new ServiceTestEndpoint(TEST_EP_ID_A);
    UBSHcomNetEndpointPtr epPtr(ep);
    PublisherContext dummyCtx;
    // erasing an unknown ep only logs
    service.EraseEpCb(dummyCtx, epPtr);

    SubscriptionInfoPtr subA = MakeSubscription(ep);
    ASSERT_TRUE(service.mPublisher->AddSubscription(subA));
    EXPECT_EQ(service.mPublisher->GetSubscriberNum(), 1);
    service.EraseEpCb(dummyCtx, epPtr);
    EXPECT_EQ(service.mPublisher->GetSubscriberNum(), 0);
    // null ep is a no-op
    service.EraseEpCb(dummyCtx, nullptr);
}

TEST_F(TestMulticastPublisherService, TestDestroyPublisher)
{
    PublisherServiceImp service;
    NetRef<Publisher> publisher(new Publisher(SERVICE_NAME));
    service.DestroyPublisher(publisher);
    EXPECT_EQ(publisher.Get(), nullptr);
    // destroying a null publisher is a no-op
    service.DestroyPublisher(publisher);
}

TEST_F(TestMulticastPublisherService, TestServiceSingletonLifecycle)
{
    MulticastServiceOptions badSize;
    badSize.maxSendRecvDataSize = 0;
    EXPECT_EQ(PublisherService::Create(SERVICE_NAME, badSize), nullptr);

    MulticastServiceOptions badMode;
    badMode.workerGroupMode = static_cast<UBSHcomNetDriverWorkingMode>(99);
    EXPECT_EQ(PublisherService::Create(SERVICE_NAME, badMode), nullptr);

    MulticastServiceOptions badPriority;
    badPriority.workerThreadPriority = -21;
    EXPECT_EQ(PublisherService::Create(SERVICE_NAME, badPriority), nullptr);

    std::string longName(NN_NO64 + 1, 'a');
    MulticastServiceOptions opt;
    EXPECT_EQ(PublisherService::Create(longName, opt), nullptr);

    PublisherService *service = PublisherService::Create(SERVICE_NAME, opt);
    ASSERT_NE(service, nullptr);
    // duplicated create returns the same instance
    EXPECT_EQ(PublisherService::Create(SERVICE_NAME, opt), service);
    // destroy an unknown service fails
    EXPECT_EQ(PublisherService::Destroy("no-such-service"), SER_ERROR);
    EXPECT_EQ(PublisherService::Destroy(SERVICE_NAME), SER_OK);
    // the instance is gone after destroy
    EXPECT_EQ(PublisherService::Destroy(SERVICE_NAME), SER_ERROR);
}

TEST_F(TestMulticastPublisherService, TestCreatePublisherFlow)
{
    PublisherServiceImp service;
    // periodic manager and context memory pool are required before creating a publisher
    ASSERT_EQ(service.CreateResource(VALID_THREAD_NUM), SER_OK);
    NetRef<Publisher> publisher;
    EXPECT_EQ(service.mCtxStoreCapacity > 0, true);
    EXPECT_EQ(service.CreatePublisher(publisher), SER_OK);
    ASSERT_NE(publisher.Get(), nullptr);
    EXPECT_EQ(publisher->GetSubscriberNum(), 0);

    // stopping the periodic manager releases the resources in order
    service.mPeriodicMgr->Stop();
    service.mPeriodicMgr.Set(nullptr);
    service.mCtxMemPool.Set(nullptr);
    service.DestroyPublisher(publisher);
}
} // namespace hcom
} // namespace ock
