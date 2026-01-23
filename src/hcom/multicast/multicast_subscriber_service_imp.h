/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_SUBSCRIBER_SERVICE_IMP_H
#define HCOM_MULTICAST_SUBSCRIBER_SERVICE_IMP_H

#include "include/multicast_subscriber_service.h"
#include "include/multicast_subscriber.h"
#include "multicast_config_imp.h"

namespace ock {
namespace hcom {
class SubscriberServiceImp : public SubscriberService {
public:
    ~SubscriberServiceImp() override;
    SerResult Start() override;
    void Stop() override;

    SerResult CreateSubscriber(const std::string &serverUrl, NetRef<Subscriber> &subscriber) override;
    void DestroySubscriber(const NetRef<Subscriber> &subscriber) override;

    MulticastConfig &GetConfig() override;
    /* *
     * @brief 注册接收receive操作回调
     *
     * @param rcvHandler 回调函数
     */
    void RegisterRecvHandler(const MulticastReqRecvHandler &recvHandler);

    /* *
     * @brief 注册接收receive操作回调
     *
     * @param rcvHandler 回调函数
     */
    void RegisterBrokenHandler(const MulticastEpBrokenHandler &handler);

private:
    SerResult InitDriver();
    SerResult StartDriver();

private:
    int ServiceRequestReceived(const UBSHcomNetRequestContext &ctx);
    void ServiceEndPointBroken(const UBSHcomNetEndpointPtr &ep);

    MulticastConfigImp mCfg;

    UBSHcomNetDriver *mDriverPtr = nullptr;
    bool mStarted = false;
    std::mutex mStartMutex;
    std::string mOobIp;
    std::vector<OOBTCPServer *> mOobServers;

    MulticastReqRecvHandler mSubscribeRecvHandler = nullptr;
    MulticastEpBrokenHandler mEpBrokenHandler = nullptr;
};
}
}

#endif