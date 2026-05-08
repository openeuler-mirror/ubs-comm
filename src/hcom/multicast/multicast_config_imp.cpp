/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#include "multicast_config_imp.h"

#include "net_common.h"
#include "net_oob.h"

namespace ock {
namespace hcom {
constexpr uint16_t MAX_TIME_OUT_DETECT_THREAD_NUM = 4;

bool MulticastConfigImp::Init(const std::string &name, const MulticastServiceOptions &opt)
{
    mOptions.protocol = opt.protocol;
    mOptions.name = name;
    mOptions.maxSendRecvDataSize = opt.maxSendRecvDataSize;
    mOptions.maxSendRecvDataCount = opt.maxSendRecvDataCount;
    mOptions.workerGroupMode = opt.workerGroupMode;
    if (NN_LIKELY(opt.workerGroupThreadCount != 0)) {
        UBSHcomWorkerGroupInfo groupInfo;
        groupInfo.threadPriority = opt.workerThreadPriority;
        groupInfo.threadCount = opt.workerGroupThreadCount;
        groupInfo.groupId = opt.workerGroupId;
        groupInfo.cpuIdsRange = opt.workerGroupCpuIdsRange;
        mOptions.workerGroupInfos.emplace_back(groupInfo); // HcomService::Instance lock
    }
    mOptions.qpSendQueueSize = opt.qpSendQueueSize;
    mOptions.qpRecvQueueSize = opt.qpRecvQueueSize;
    mOptions.qpPrePostSize = opt.qpPrePostSize;
    mOptions.completionQueueDepth = opt.completionQueueDepth;
    mOptions.maxSubscriberNum = opt.maxSubscriberNum;
    mOptions.publisherGroupNo = opt.publisherWrkGroupNo;
    mOptions.qpBatchRePostSize = opt.qpBatchRePostSize;
    mOptions.enableTls = opt.enableTls;
    mOptions.cipherSuite = opt.cipherSuite;
    mOptions.periodicCpuId = opt.periodicCpuId;
    return true;
}

const std::string &MulticastConfigImp::GetName() const
{
    return mOptions.name;
}

int32_t MulticastConfigImp::ValidateMulticastServiceOption()
{
    if (NN_UNLIKELY(mOptions.timeOutDetectThreadNum == 0 ||
        mOptions.timeOutDetectThreadNum > MAX_TIME_OUT_DETECT_THREAD_NUM)) {
        NN_LOG_ERROR("Invalid time out detect thread num " << mOptions.timeOutDetectThreadNum << ", must range [1, 4]");
        return SER_INVALID_PARAM;
    }
    return SER_OK;
}

void MulticastConfigImp::SetDeviceIpMask(const std::vector<std::string> &ipMasks)
{
    mOptions.ipMasks = ipMasks;
}

const std::vector<std::string> &MulticastConfigImp::GetDeviceIpMask() const
{
    return mOptions.ipMasks;
}

void MulticastConfigImp::SetCompletionQueueDepth(uint16_t depth)
{
    mOptions.completionQueueDepth = depth;
}

const uint16_t MulticastConfigImp::GetCompletionQueueDepth() const
{
    return mOptions.completionQueueDepth;
}

void MulticastConfigImp::SetSendQueueSize(uint32_t sqSize)
{
    mOptions.qpSendQueueSize = sqSize;
}

const uint32_t MulticastConfigImp::GetSendQueueSize() const
{
    return mOptions.qpSendQueueSize;
}

void MulticastConfigImp::SetRecvQueueSize(uint32_t rqSize)
{
    mOptions.qpRecvQueueSize = rqSize;
}

const uint32_t MulticastConfigImp::GetRecvQueueSize() const
{
    return mOptions.qpRecvQueueSize;
}

void MulticastConfigImp::SetQueuePrePostSize(uint32_t prePostSize)
{
    mOptions.qpPrePostSize = prePostSize;
}

const uint32_t MulticastConfigImp::GetQueuePrePostSize() const
{
    return mOptions.qpPrePostSize;
}

void MulticastConfigImp::SetPollingBatchSize(uint16_t pollSize)
{
    mOptions.pollingBatchSize = pollSize;
}

const uint16_t MulticastConfigImp::GetPollingBatchSize() const
{
    return mOptions.pollingBatchSize;
}

void MulticastConfigImp::SetEventPollingTimeOutUs(uint16_t pollTimeout)
{
    mOptions.eventPollingTimeOutUs = pollTimeout;
}

const uint16_t MulticastConfigImp::GetEventPollingTimeOutUs() const
{
    return mOptions.eventPollingTimeOutUs;
}

void MulticastConfigImp::SetHeartBeatOptions(const MulticastHeartBeatOptions &opt)
{
    mOptions.heartBeatOption = opt;
}

const MulticastHeartBeatOptions &MulticastConfigImp::GetHeartBeatOptions() const
{
    return mOptions.heartBeatOption;
}

void MulticastConfigImp::SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount)
{
    mOptions.maxSendRecvDataCount = maxSendRecvDataCount;
}

const uint32_t MulticastConfigImp::GetMaxSendRecvDataCount() const
{
    return mOptions.maxSendRecvDataCount;
}

void MulticastConfigImp::SetMaxSendRecvDataSize(uint32_t maxSendRecvDataSize)
{
    mOptions.maxSendRecvDataSize = maxSendRecvDataSize;
}

const uint32_t MulticastConfigImp::GetMaxSendRecvDataSize() const
{
    return mOptions.maxSendRecvDataSize;
}

void MulticastConfigImp::AddWorkerGroup(UBSHcomWorkerGroupInfo &groupInfo)
{
    mOptions.workerGroupInfos.emplace_back(groupInfo);
}

bool MulticastConfigImp::FillNetDriverOpt(ock::hcom::UBSHcomNetDriverOptions &driverOpt)
{
    if (!driverOpt.SetNetDeviceIpMask(mOptions.ipMasks)) {
        NN_LOG_ERROR("SetNetDeviceIpMask Failed!");
        return false;
    }
    if (!driverOpt.SetNetDeviceIpGroup(mOptions.ipGroups)) {
        NN_LOG_ERROR("SetNetDeviceIpGroup Failed!");
        return false;
    }
    driverOpt.enableTls = mOptions.enableTls;
    driverOpt.cipherSuite = mOptions.cipherSuite;
    driverOpt.dontStartWorkers = mOptions.workerGroupInfos.empty();
    driverOpt.mode = mOptions.workerGroupMode;
    driverOpt.oobType = mOptions.oobType;
    driverOpt.lbPolicy = NET_ROUND_ROBIN;
    driverOpt.heartBeatIdleTime = mOptions.heartBeatOption.heartBeatIdleSec;
    driverOpt.heartBeatProbeTimes = mOptions.heartBeatOption.heartBeatProbeTimes;
    driverOpt.heartBeatProbeInterval = mOptions.heartBeatOption.heartBeatProbeIntervalSec;

    driverOpt.mrSendReceiveSegSize = mOptions.maxSendRecvDataSize;
    driverOpt.mrSendReceiveSegCount = mOptions.maxSendRecvDataCount;
    driverOpt.completionQueueDepth = mOptions.completionQueueDepth;
    driverOpt.pollingBatchSize = mOptions.pollingBatchSize;
    driverOpt.eventPollingTimeout = mOptions.eventPollingTimeOutUs;
    driverOpt.qpSendQueueSize = mOptions.qpSendQueueSize;
    driverOpt.qpReceiveQueueSize = mOptions.qpRecvQueueSize;
    driverOpt.prePostReceiveSizePerQP = mOptions.qpPrePostSize;
    driverOpt.maxConnectionNum = mOptions.maxConnCount;
    driverOpt.qpBatchRePostSize = mOptions.qpBatchRePostSize;

    driverOpt.tcpSendZCopy = true;
    driverOpt.tcpEpollLT = true;
    return true;
}

void MulticastConfigImp::SetStartOobServer(bool isStartOobServer)
{
    mOptions.startOobSvr = isStartOobServer;
}

bool MulticastConfigImp::GetStartOobServer() const
{
    return mOptions.startOobSvr;
}

void MulticastConfigImp::AddOobOption(const std::string &name, const UBSHcomNetOobListenerOptions &opt)
{
    mOptions.oobOption[name] = opt;
}

const std::unordered_map<std::string, UBSHcomNetOobListenerOptions> &MulticastConfigImp::GetOobOption() const
{
    return mOptions.oobOption;
}

void MulticastConfigImp::SetWorkerGroupInfo(std::vector<UBSHcomWorkerGroupInfo> &info)
{
    mOptions.workerGroupInfos = info;
}

const std::vector<UBSHcomWorkerGroupInfo> &MulticastConfigImp::GetWorkerGroupInfo() const
{
    return mOptions.workerGroupInfos;
}

void MulticastConfigImp::SetOobType(NetDriverOobType type)
{
    mOptions.oobType = type;
}

NetDriverOobType MulticastConfigImp::GetOobType() const
{
    return mOptions.oobType;
}

void MulticastConfigImp::SetPeriodicThreadNum(uint32_t threadNum)
{
    mOptions.periodicThreadNum = threadNum;
}

const uint32_t MulticastConfigImp::GetPeriodicThreadNum() const
{
    return mOptions.periodicThreadNum;
}

void MulticastConfigImp::SetMaxSubscriberNum(uint32_t maxSubscriberNum)
{
    mOptions.maxSubscriberNum = maxSubscriberNum;
}

const uint32_t MulticastConfigImp::GetMaxSubscriberNum() const
{
    return mOptions.maxSubscriberNum;
}

void MulticastConfigImp::SetPublisherWkrGroupNo(uint8_t groupNo)
{
    mOptions.publisherGroupNo = groupNo;
}

const uint8_t MulticastConfigImp::GetPublisherWkrGroupNo() const
{
    return mOptions.publisherGroupNo;
}

void MulticastConfigImp::SetPeriodicCpuId(int cpuId)
{
    mOptions.periodicCpuId = cpuId;
}

const int MulticastConfigImp::GetPeriodicCpuId() const
{
    return mOptions.periodicCpuId;
}

UBSHcomNetDriverProtocol MulticastConfigImp::GetProtocol() const
{
    return mOptions.protocol;
}
}
}