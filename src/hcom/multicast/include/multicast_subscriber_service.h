/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_SUBSCRIBER_SERVICE_H
#define HCOM_MULTICAST_SUBSCRIBER_SERVICE_H

#include "multicast_config.h"
#include "multicast_subscriber.h"

namespace ock {
namespace hcom {
struct MulticastServiceOptions;
class SubscriberService {
public:
    SubscriberService() = default;
    virtual ~SubscriberService() = default;

    /**
     * @brief 创建SubscriberService
     *
     * @param name service名称
     * @param opt service创建需要的配置项
     * @return SubscriberService* SubscriberService
     */
    static SubscriberService *Create(const std::string &name, const MulticastServiceOptions &opt = {});

    /**
     * @brief 销毁SubscriberService
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
     * @brief 创建subscriber
     * @param serverUrl 目标url，如：tcp://127.0.0.1:9981
     * @param subscriber 返回创建出的subscriber
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t CreateSubscriber(const std::string &serverUrl, NetRef<Subscriber> &subscriber) = 0;

    /**
     * @brief 销毁subscriber
     * @param subscriber 待销毁的subscriber
     */
    virtual void DestroySubscriber(const NetRef<Subscriber> &subscriber) = 0;

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
    virtual void RegisterBrokenHandler(const MulticastEpBrokenHandler &handler) = 0;

    /**
     * @brief 注册接收回调
     *
     * @param handler 接收回调函数
     */
    virtual void RegisterRecvHandler(const MulticastReqRecvHandler &handler) = 0;

    /**
     * @brief 注册TLS回调
     *
     * @param cb TLS回调函数
     */
    virtual void RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb) = 0;
    virtual void RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb) = 0;
    virtual void RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb) = 0;
};
}
}

#endif