/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_PUBLISHER_SERVICE_IMP_H
#define HCOM_MULTICAST_PUBLISHER_SERVICE_IMP_H

#include "include/multicast_publisher_service.h"
#include "multicast_config_imp.h"
#include "net_mem_pool_fixed.h"

namespace ock {
namespace hcom {
using PublisherPtr = NetRef<Publisher>;
class PublisherServiceImp : public PublisherService {
public:
    ~PublisherServiceImp() override;
    SerResult Start() override;
    void Stop() override;

    SerResult CreatePublisher(NetRef<Publisher> &publisher) override;
    void DestroyPublisher(NetRef<Publisher> &publisher) override;

    SerResult Bind(const std::string &listenerUrl, const NewSubscriptionHandler &handler, const int cpuId) override;
    MulticastConfig &GetConfig() override;
    void RegisterSubscriptionExceptionHandler(const SubscriptionExceptionHandler &handler) override;
    void RegisterBrokenHandler(const MulticastEpBrokenHandler &handler);
    // send callback for reserved, not used now
    void RegisterSendHandler(const MulticastReqPostedHandler &handler);
    // recv callback for reserved, not used now as subscriber will not send msg, just reply
    void RegisterPubRecvHandler(const MulticastPubReqRecvHandler &handler);

    void RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb);
    void RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb);
    void RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb);

    void AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount, const std::pair<uint32_t, uint32_t> &cpuIdsRange,
                        int8_t priority);

    SerResult RegisterMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr);
    SerResult RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr);
    void DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr);
private:
    SerResult InitDriver();
    SerResult CreateResource(uint32_t threadNum);
    SerResult AddTcpOobListener(const std::string &url, int cpuId, uint16_t workerCount = UINT16_MAX);
    SerResult StartDriver();
    SerResult EpBrokenCallback(const ock::hcom::UBSHcomNetEndpointPtr &ep);
    SerResult NewSubscriptionCallback(const std::string &ipPort, const ock::hcom::UBSHcomNetEndpointPtr &ep,
        const std::string &payload);
    SerResult ServiceRequestReceived(const UBSHcomNetRequestContext &ctx);
    SerResult DelayEraseEp(const UBSHcomNetEndpointPtr &ep, uint16_t delayTime);
    void DirectEraseEp(UBSHcomNetEndpointPtr ep);
    void EraseEpCb(PublisherContext &ctx, uintptr_t epPtr);

private:
    MulticastConfigImp mCfg;
    UBSHcomNetDriver *mDriverPtr = nullptr;
    bool mStarted = false;
    std::mutex mStartMutex;
    std::string mOobIp;
    std::vector<OOBTCPServer *> mOobServers;
    NewSubscriptionHandler mNewSubScriptionHandler = nullptr;
    SubscriptionExceptionHandler mSubscriptionExceptionHandler = nullptr;

    MulticastEpBrokenHandler mPubBrokenHandler = nullptr;
    MulticastPubReqRecvHandler mPubRecvHandler = nullptr;
    MulticastReqPostedHandler mPubSendHandler = nullptr;

    UBSHcomTLSCaCallback mPubTLSCaCallback = nullptr;
    UBSHcomTLSCertificationCallback mPubTLSCertificationCallback = nullptr;
    UBSHcomTLSPrivateKeyCallback mPubTLSPrivateKeyCallback = nullptr;

    PublisherPtr mPublisher = nullptr;
    MultiCastPeriodicManagerPtr mPeriodicMgr = nullptr;
    NetMemPoolFixedPtr mCtxMemPool = nullptr;
    NetMemPoolFixedPtr mPubCtxMemPool = nullptr;
    uint32_t mCtxStoreCapacity = NN_NO2097152;
};
}
}
#endif
