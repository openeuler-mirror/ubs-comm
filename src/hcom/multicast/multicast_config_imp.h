/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_CONFIG_IMP_H
#define HCOM_MULTICAST_CONFIG_IMP_H

#include "include/multicast_def.h"
#include "include/multicast_config.h"

namespace ock {
namespace hcom {
struct MulticastServiceOptionsInner {
    UBSHcomNetDriverProtocol protocol;
    MulticastHeartBeatOptions heartBeatOption;
    uint32_t qpSendQueueSize = 256;
    uint32_t qpRecvQueueSize = 256;
    uint32_t qpPrePostSize = 64;
    uint32_t maxSendRecvDataSize = 1024;
    uint32_t maxSendRecvDataCount = 8192;
    uint32_t maxConnCount = 250;
    uint32_t timeOutDetectThreadNum = 1;
    uint16_t pollingBatchSize = 4;
    uint16_t eventPollingTimeOutUs = 500;
    uint16_t completionQueueDepth = 2048;
    std::string name;
    bool startOobSvr = false;
    NetDriverOobType oobType = NET_OOB_TCP;
    WorkerMode workerGroupMode = NET_BUSY_POLLING;
    std::vector<UBSHcomWorkerGroupInfo> workerGroupInfos;
    std::vector<std::string> ipMasks;
    std::vector<std::string> ipGroups;
    uint32_t periodicThreadNum = 1;
    std::unordered_map<std::string, UBSHcomNetOobListenerOptions> oobOption;
    uint32_t maxSubscriberNum = 7;
    uint8_t publisherGroupNo = 0;
    uint32_t qpBatchRePostSize = 10;
    bool enableTls = true;
    CipherSuite cipherSuite = AES_GCM_128;
};

class MulticastConfigImp : public MulticastConfig {
public:
    ~MulticastConfigImp() override = default;
    bool Init(const std::string &name, const MulticastServiceOptions &opt) override;
    const std::string &GetName() const override;

    void SetDeviceIpMask(const std::vector<std::string> &ipMasks) override;
    const std::vector<std::string> &GetDeviceIpMask() const override;

    void SetCompletionQueueDepth(uint16_t depth) override;
    const uint16_t GetCompletionQueueDepth() const override;

    void SetSendQueueSize(uint32_t sqSize) override;
    const uint32_t GetSendQueueSize() const override;

    void SetRecvQueueSize(uint32_t rqSize) override;
    const uint32_t GetRecvQueueSize() const override;

    void SetQueuePrePostSize(uint32_t prePostSize) override;
    const uint32_t GetQueuePrePostSize() const override;

    void SetPollingBatchSize(uint16_t pollSize) override;
    const uint16_t GetPollingBatchSize() const override;

    void SetEventPollingTimeOutUs(uint16_t pollTimeout) override;
    const uint16_t GetEventPollingTimeOutUs() const override;

    void SetHeartBeatOptions(const MulticastHeartBeatOptions &opt) override;
    const MulticastHeartBeatOptions &GetHeartBeatOptions() const override;

    void SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount) override;
    const uint32_t GetMaxSendRecvDataCount() const override;

    void SetMaxSendRecvDataSize(uint32_t maxSendRecvDataSize) override;
    const uint32_t GetMaxSendRecvDataSize() const override;

    void SetWorkerGroupInfo(std::vector<UBSHcomWorkerGroupInfo> &info) override;
    const std::vector<UBSHcomWorkerGroupInfo> &GetWorkerGroupInfo() const override;

    void SetPeriodicThreadNum(uint32_t threadNum) override;
    const uint32_t GetPeriodicThreadNum() const override;

    void SetMaxSubscriberNum(uint32_t maxSubscriberNum) override;
    const uint32_t GetMaxSubscriberNum() const override;

    void AddWorkerGroup(UBSHcomWorkerGroupInfo &groupInfo);
public:
    /*****************************************************************
     * 仅在内部类中使用，不对外暴露
     *****************************************************************/
    int32_t ValidateMulticastServiceOption();
    bool FillNetDriverOpt(ock::hcom::UBSHcomNetDriverOptions &driverOpt);

    void SetStartOobServer(bool isStartOobServer);
    bool GetStartOobServer() const;

    void AddOobOption(const std::string &name, const UBSHcomNetOobListenerOptions &opt);
    const std::unordered_map<std::string, UBSHcomNetOobListenerOptions> &GetOobOption() const;

    void SetOobType(NetDriverOobType type);

    void SetPublisherWkrGroupNo(uint8_t groupNo);
    const uint8_t GetPublisherWkrGroupNo() const;

    NetDriverOobType GetOobType() const;

private:
    MulticastServiceOptionsInner mOptions;
};
}
}

#endif