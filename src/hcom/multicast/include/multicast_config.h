/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_CONFIG_H
#define HCOM_MULTICAST_CONFIG_H

#include "multicast_def.h"

namespace ock {
namespace hcom {
class MulticastConfig {
public:
    virtual ~MulticastConfig() = default;
    virtual bool Init(const std::string &name, const MulticastServiceOptions &opt) = 0;
    virtual const std::string &GetName() const = 0;

    virtual void SetDeviceIpMask(const std::vector<std::string> &ipMasks) = 0;
    virtual const std::vector<std::string> &GetDeviceIpMask() const = 0;

    virtual void SetCompletionQueueDepth(uint16_t depth) = 0;
    virtual const uint16_t GetCompletionQueueDepth() const = 0;

    virtual void SetSendQueueSize(uint32_t sqSize) = 0;
    virtual const uint32_t GetSendQueueSize() const = 0;

    virtual void SetRecvQueueSize(uint32_t rqSize) = 0;
    virtual const uint32_t GetRecvQueueSize() const = 0;

    virtual void SetQueuePrePostSize(uint32_t prePostSize) = 0;
    virtual const uint32_t GetQueuePrePostSize() const = 0;

    virtual void SetPollingBatchSize(uint16_t pollSize) = 0;
    virtual const uint16_t GetPollingBatchSize() const = 0;

    virtual void SetEventPollingTimeOutUs(uint16_t pollTimeout) = 0;
    virtual const uint16_t GetEventPollingTimeOutUs() const = 0;

    virtual void SetHeartBeatOptions(const MulticastHeartBeatOptions &opt) = 0;
    virtual const MulticastHeartBeatOptions &GetHeartBeatOptions() const = 0;

    virtual void SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount) = 0;
    virtual const uint32_t GetMaxSendRecvDataCount() const = 0;

    virtual void SetMaxSendRecvDataSize(uint32_t maxSendRecvDataSize) = 0;
    virtual const uint32_t GetMaxSendRecvDataSize() const = 0;

    virtual void SetWorkerGroupInfo(std::vector<UBSHcomWorkerGroupInfo> &info) = 0;
    virtual const std::vector<UBSHcomWorkerGroupInfo> &GetWorkerGroupInfo() const = 0;

    virtual void SetPeriodicThreadNum(uint32_t threadNum) = 0;
    virtual const uint32_t GetPeriodicThreadNum() const = 0;

    virtual void SetMaxSubscriberNum(uint32_t maxSubscriberNum) = 0;
    virtual const uint32_t GetMaxSubscriberNum() const = 0;

    virtual void SetPeriodicCpuId(int cpuId) = 0;
    virtual const int GetPeriodicCpuId() const = 0;

    virtual UBSHcomNetDriverProtocol GetProtocol() const = 0;
};
}
}

#endif