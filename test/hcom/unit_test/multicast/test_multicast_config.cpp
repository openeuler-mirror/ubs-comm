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

#include "multicast_config_imp.h"

namespace ock {
namespace hcom {
namespace {
constexpr char TEST_CONFIG_NAME[] = "ut-multicast";
constexpr uint32_t TEST_DETECT_THREAD_NUM_INVALID_LOW = 0;
constexpr uint32_t TEST_DETECT_THREAD_NUM_INVALID_HIGH = 5;
constexpr uint32_t TEST_DETECT_THREAD_NUM_VALID = 2;
constexpr uint16_t TEST_QUEUE_DEPTH = 2048;
constexpr uint32_t TEST_QUEUE_SIZE = 1024;
constexpr uint16_t TEST_PRE_POST_SIZE = 256;
constexpr uint16_t TEST_POLL_BATCH = 10;
constexpr uint16_t TEST_POLL_TIMEOUT_US = 100;
constexpr uint32_t TEST_MAX_DATA_COUNT = 4096;
constexpr uint32_t TEST_MAX_DATA_SIZE = 1024;
constexpr uint32_t TEST_MAX_SUBSCRIBER_NUM = 8;
constexpr uint8_t TEST_PUBLISHER_GROUP_NO = 3;
constexpr int TEST_PERIODIC_CPU_ID = 7;
constexpr uint32_t TEST_PERIODIC_THREAD_NUM = 2;
constexpr uint16_t TEST_WORKER_GROUP_ID = 1;
constexpr uint32_t TEST_WORKER_THREAD_COUNT = 2;
constexpr int8_t TEST_WORKER_PRIORITY = -5;
} // namespace

class TestMulticastConfig : public testing::Test {
public:
    TestMulticastConfig() = default;
    ~TestMulticastConfig() override = default;

protected:
    virtual void SetUp(void);
    virtual void TearDown(void);
};

void TestMulticastConfig::SetUp() {}

void TestMulticastConfig::TearDown() {}

TEST_F(TestMulticastConfig, TestInitCopiesOptions)
{
    MulticastConfigImp config;
    MulticastServiceOptions opt;
    opt.protocol = UBSHcomNetDriverProtocol::TCP;
    opt.maxSendRecvDataSize = TEST_MAX_DATA_SIZE;
    opt.maxSendRecvDataCount = TEST_MAX_DATA_COUNT;
    opt.multicastIoContextCount = TEST_MAX_DATA_COUNT;
    opt.workerGroupThreadCount = TEST_WORKER_THREAD_COUNT;
    opt.qpSendQueueSize = TEST_QUEUE_SIZE;
    opt.qpRecvQueueSize = TEST_QUEUE_SIZE;
    opt.qpPrePostSize = TEST_PRE_POST_SIZE;
    opt.completionQueueDepth = TEST_QUEUE_DEPTH;
    opt.maxSubscriberNum = TEST_MAX_SUBSCRIBER_NUM;
    opt.publisherWrkGroupNo = TEST_PUBLISHER_GROUP_NO;
    opt.periodicCpuId = TEST_PERIODIC_CPU_ID;

    EXPECT_TRUE(config.Init(TEST_CONFIG_NAME, opt));
    EXPECT_EQ(config.GetName(), TEST_CONFIG_NAME);
    EXPECT_EQ(config.GetProtocol(), UBSHcomNetDriverProtocol::TCP);
    EXPECT_EQ(config.GetMaxSendRecvDataSize(), TEST_MAX_DATA_SIZE);
    EXPECT_EQ(config.GetMaxSendRecvDataCount(), TEST_MAX_DATA_COUNT);
    EXPECT_EQ(config.GetMulticastIoContextCount(), TEST_MAX_DATA_COUNT);
    EXPECT_EQ(config.GetSendQueueSize(), TEST_QUEUE_SIZE);
    EXPECT_EQ(config.GetRecvQueueSize(), TEST_QUEUE_SIZE);
    EXPECT_EQ(config.GetQueuePrePostSize(), TEST_PRE_POST_SIZE);
    EXPECT_EQ(config.GetCompletionQueueDepth(), TEST_QUEUE_DEPTH);
    EXPECT_EQ(config.GetMaxSubscriberNum(), TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(config.GetPublisherWkrGroupNo(), TEST_PUBLISHER_GROUP_NO);
    EXPECT_EQ(config.GetPeriodicCpuId(), TEST_PERIODIC_CPU_ID);
    // worker group with non-zero thread count is collected
    EXPECT_EQ(config.GetWorkerGroupInfo().size(), 1);
}

TEST_F(TestMulticastConfig, TestValidateDetectThreadNum)
{
    MulticastConfigImp config;
    config.mOptions.timeOutDetectThreadNum = TEST_DETECT_THREAD_NUM_INVALID_LOW;
    EXPECT_EQ(config.ValidateMulticastServiceOption(), SER_INVALID_PARAM);
    config.mOptions.timeOutDetectThreadNum = TEST_DETECT_THREAD_NUM_INVALID_HIGH;
    EXPECT_EQ(config.ValidateMulticastServiceOption(), SER_INVALID_PARAM);
    config.mOptions.timeOutDetectThreadNum = TEST_DETECT_THREAD_NUM_VALID;
    EXPECT_EQ(config.ValidateMulticastServiceOption(), SER_OK);
}

TEST_F(TestMulticastConfig, TestSettersAndGetters)
{
    MulticastConfigImp config;

    std::vector<std::string> masks = {"127.0.0.0/8"};
    config.SetDeviceIpMask(masks);
    EXPECT_EQ(config.GetDeviceIpMask().size(), 1);

    config.SetCompletionQueueDepth(TEST_QUEUE_DEPTH);
    EXPECT_EQ(config.GetCompletionQueueDepth(), TEST_QUEUE_DEPTH);
    config.SetSendQueueSize(TEST_QUEUE_SIZE);
    EXPECT_EQ(config.GetSendQueueSize(), TEST_QUEUE_SIZE);
    config.SetRecvQueueSize(TEST_QUEUE_SIZE);
    EXPECT_EQ(config.GetRecvQueueSize(), TEST_QUEUE_SIZE);
    config.SetQueuePrePostSize(TEST_PRE_POST_SIZE);
    EXPECT_EQ(config.GetQueuePrePostSize(), TEST_PRE_POST_SIZE);
    config.SetPollingBatchSize(TEST_POLL_BATCH);
    EXPECT_EQ(config.GetPollingBatchSize(), TEST_POLL_BATCH);
    config.SetEventPollingTimeOutUs(TEST_POLL_TIMEOUT_US);
    EXPECT_EQ(config.GetEventPollingTimeOutUs(), TEST_POLL_TIMEOUT_US);
    config.SetMaxSendRecvDataCount(TEST_MAX_DATA_COUNT);
    EXPECT_EQ(config.GetMaxSendRecvDataCount(), TEST_MAX_DATA_COUNT);
    config.SetMaxSendRecvDataSize(TEST_MAX_DATA_SIZE);
    EXPECT_EQ(config.GetMaxSendRecvDataSize(), TEST_MAX_DATA_SIZE);
    config.SetMaxSubscriberNum(TEST_MAX_SUBSCRIBER_NUM);
    EXPECT_EQ(config.GetMaxSubscriberNum(), TEST_MAX_SUBSCRIBER_NUM);
    config.SetPublisherWkrGroupNo(TEST_PUBLISHER_GROUP_NO);
    EXPECT_EQ(config.GetPublisherWkrGroupNo(), TEST_PUBLISHER_GROUP_NO);
    config.SetPeriodicCpuId(TEST_PERIODIC_CPU_ID);
    EXPECT_EQ(config.GetPeriodicCpuId(), TEST_PERIODIC_CPU_ID);
    config.SetPeriodicThreadNum(TEST_PERIODIC_THREAD_NUM);
    EXPECT_EQ(config.GetPeriodicThreadNum(), TEST_PERIODIC_THREAD_NUM);

    MulticastHeartBeatOptions heartbeat;
    heartbeat.heartBeatIdleSec = 6;
    config.SetHeartBeatOptions(heartbeat);
    EXPECT_EQ(config.GetHeartBeatOptions().heartBeatIdleSec, 6);

    EXPECT_FALSE(config.GetStartOobServer());
    config.SetStartOobServer(true);
    EXPECT_TRUE(config.GetStartOobServer());

    config.SetOobType(NET_OOB_TCP);
    EXPECT_EQ(config.GetOobType(), NET_OOB_TCP);

    UBSHcomNetOobListenerOptions oobOption;
    config.AddOobOption("127.0.0.1:9981", oobOption);
    EXPECT_EQ(config.GetOobOption().size(), 1);
    EXPECT_NE(config.GetOobOption().find("127.0.0.1:9981"), config.GetOobOption().end());

    UBSHcomWorkerGroupInfo groupInfo;
    std::vector<UBSHcomWorkerGroupInfo> groups = {groupInfo};
    config.SetWorkerGroupInfo(groups);
    EXPECT_EQ(config.GetWorkerGroupInfo().size(), 1);
    config.AddWorkerGroup(groupInfo);
    EXPECT_EQ(config.GetWorkerGroupInfo().size(), 2);
}

TEST_F(TestMulticastConfig, TestFillNetDriverOpt)
{
    MulticastConfigImp config;
    UBSHcomNetDriverOptions driverOpt;
    // SetNetDeviceIpMask does not validate entries, filling succeeds with any mask string
    std::vector<std::string> masks = {"127.0.0.0/8"};
    config.SetDeviceIpMask(masks);
    MulticastServiceOptions opt;
    opt.enableTls = false;
    opt.workerGroupThreadCount = TEST_WORKER_THREAD_COUNT;
    (void)config.Init(TEST_CONFIG_NAME, opt);
    MulticastHeartBeatOptions heartbeat;
    heartbeat.heartBeatIdleSec = 30;
    heartbeat.heartBeatProbeTimes = 3;
    heartbeat.heartBeatProbeIntervalSec = 2;
    config.SetHeartBeatOptions(heartbeat);
    config.SetPollingBatchSize(TEST_POLL_BATCH);
    config.SetEventPollingTimeOutUs(TEST_POLL_TIMEOUT_US);
    config.SetCompletionQueueDepth(TEST_QUEUE_DEPTH);
    config.SetSendQueueSize(TEST_QUEUE_SIZE);
    config.SetRecvQueueSize(TEST_QUEUE_SIZE);
    config.SetQueuePrePostSize(TEST_PRE_POST_SIZE);

    UBSHcomNetDriverOptions driverOpt2;
    EXPECT_TRUE(config.FillNetDriverOpt(driverOpt2));
    EXPECT_FALSE(driverOpt2.enableTls);
    EXPECT_EQ(driverOpt2.heartBeatIdleTime, 30);
    EXPECT_EQ(driverOpt2.heartBeatProbeTimes, 3);
    EXPECT_EQ(driverOpt2.heartBeatProbeInterval, 2);
    EXPECT_EQ(driverOpt2.mrSendReceiveSegSize, opt.maxSendRecvDataSize);
    EXPECT_EQ(driverOpt2.pollingBatchSize, TEST_POLL_BATCH);
    EXPECT_EQ(driverOpt2.eventPollingTimeout, TEST_POLL_TIMEOUT_US);
    EXPECT_EQ(driverOpt2.completionQueueDepth, TEST_QUEUE_DEPTH);
    EXPECT_EQ(driverOpt2.qpSendQueueSize, TEST_QUEUE_SIZE);
    EXPECT_EQ(driverOpt2.qpReceiveQueueSize, TEST_QUEUE_SIZE);
    EXPECT_EQ(driverOpt2.prePostReceiveSizePerQP, TEST_PRE_POST_SIZE);
    // worker groups exist, workers must be started by the driver
    EXPECT_FALSE(driverOpt2.dontStartWorkers);
    EXPECT_TRUE(driverOpt2.tcpSendZCopy);
    EXPECT_TRUE(driverOpt2.tcpEpollLT);
}
} // namespace hcom
} // namespace ock
