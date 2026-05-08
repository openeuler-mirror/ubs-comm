/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_PUBLISHER_SERVICE_H
#define HCOM_MULTICAST_PUBLISHER_SERVICE_H

#include "multicast_config.h"
#include "multicast_publisher.h"

namespace ock {
namespace hcom {
class SubscriptionInfo;
using SubscriptionInfoPtr = NetRef<SubscriptionInfo>;

class MultiCastPeriodicManager;
using MultiCastPeriodicManagerPtr = NetRef<MultiCastPeriodicManager>;

using NewSubscriptionHandler = std::function<int(SubscriptionInfoPtr &info)>;
using SubscriptionExceptionHandler = std::function<void(SubscriptionInfo &info)>;

class PublisherService {
public:
    virtual ~PublisherService() = default;

    /**
     * @brief 创建PublisherService
     *
     * @param name service名称
     * @param opt service创建需要的配置项
     * @return PublisherService* PublisherService
     */
    static PublisherService *Create(const std::string &name, const MulticastServiceOptions &opt = {});

    /* *
     * @brief 销毁PublisherService
     *
     * @param name 要销毁的实例名称
     * @return int32_t 成功：0；失败：错误码
     */
    static int32_t Destroy(const std::string &name);

    /**
     * @brief 启动服务
     *
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t Start() = 0;

    /**
     * @brief 停止服务
     */
    virtual void Stop() = 0;

    /**
     * @brief 创建publisher
     * @param publisher 返回创建出的publisher
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t CreatePublisher(NetRef<Publisher> &publisher) = 0;

     /**
     * @brief 销毁publisher
     * @param publisher 待销毁的publisher
     */
    virtual void DestroyPublisher(NetRef<Publisher> &publisher) = 0;

    /**
     * @brief 绑定监听url，制定监听的类型及url
     *
     * @param listenerUrl 监听url，如：tcp://127.0.0.1:9981
     * @param handler  收到建链请求后的回调函数
     * @param cpuId  监听线程绑定的cpuId
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t Bind(const std::string &listenerUrl, const NewSubscriptionHandler &handler, const int cpuId) = 0;

    /**
     * @brief 获取MulticastConfig，用于做高级配置
     *
     * @return MulticastConfig& 返回MulticastConfig对象
     */
    virtual MulticastConfig &GetConfig() = 0;

    /**
     * @brief 注册断链回调
     *
     * @param handler 断链回调函数
     */
    virtual void RegisterSubscriptionExceptionHandler(const SubscriptionExceptionHandler &handler) = 0;
    virtual void RegisterBrokenHandler(const MulticastEpBrokenHandler &handler) = 0;
    virtual void RegisterSendHandler(const MulticastReqPostedHandler &handler) = 0;
    virtual void RegisterPubRecvHandler(const MulticastPubReqRecvHandler &handler) = 0;

    /**
     * @brief 注册TLS回调
     *
     * @param cb TLS回调函数
     */
    virtual void RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb) = 0;
    virtual void RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb) = 0;
    virtual void RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb) = 0;

    virtual void AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount,
        const std::pair<uint32_t, uint32_t> &cpuIdsRange, int8_t priority) = 0;

    virtual SerResult RegisterMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr) = 0;
    virtual SerResult RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr) = 0;
    virtual void DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr) = 0;
};
}
}

#endif