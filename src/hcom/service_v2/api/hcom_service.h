/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef HCOM_API_HCOM_SERVICE_H_
#define HCOM_API_HCOM_SERVICE_H_

#include <cstdint>
#include <string>
#include <vector>
#include "hcom.h"
#include "hcom_service_channel.h"
#include "hcom_obj_statistics.h"
#include "hcom_def.h"
#include "hcom_ref.h"

namespace ock {
namespace hcom {

using UBSHcomChannelPtr = NetRef<UBSHcomChannel>;

using UBSHcomServiceNewChannelHandler =
    std::function<int(const std::string &ipPort, const UBSHcomChannelPtr &, const std::string &payload)>;
using UBSHcomServiceChannelBrokenHandler = std::function<void(const UBSHcomChannelPtr &)>;
using UBSHcomServiceRecvHandler = std::function<int(UBSHcomServiceContext &)>;
using UBSHcomServiceSendHandler = std::function<int(const UBSHcomServiceContext &)>;
using UBSHcomServiceOneSideDoneHandler = std::function<int(const UBSHcomServiceContext &)>;
using UBSHcomServiceIdleHandler = UBSHcomNetDriverIdleHandler;
using UBSHcomServiceProtocol = UBSHcomNetDriverProtocol;
using UBSHcomServiceLBPolicy = UBSHcomNetDriverLBPolicy;

class UBSHcomService {
public:
    /**
     * @brief service创建
     *
     * @param t service对应底层通信协议
     * @param name service名称
     * @param opt service创建需要的配置项
     * @return UBSHcomService* 返回创建好的service
     */
    static UBSHcomService* Create(UBSHcomServiceProtocol t, const std::string &name,
        const UBSHcomServiceOptions &opt = {});

    /**
     * @brief 销毁service
     *
     * @param name 要销毁的实例名称
     * @return int32_t 成功：0；失败：错误码
     */
    static int32_t Destroy(const std::string &name);

    /**
     * @brief 绑定监听url，指定监听的类型及url，客户端可以不调用Bind。
     *
     * @param listenerUrl 监听url，对于tcp来说：tcp://127.0.0.1:9981
     *                          对于uds来说：uds://file:perm（如果有:perm则使用真实文件，perm格式如：0600，没有则使用抽象文件）
     *                          对于ubc来说：ubc://eid:jettyId
     * @param handler 收到建链请求后的回调函数
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t Bind(const std::string &listenerUrl, const UBSHcomServiceNewChannelHandler &handler) = 0;

    /**
     * @brief 开启服务，如果调用过Bind，则同时开启监听，否则不进行监听
     *
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t Start() = 0;

    /**
     * @brief 建立链接
     *
     * @param serverUrl 建连服务端url，对于tcp来说：tcp://127.0.0.1:9981
     *                                对于uds来说：uds://file(文件名/抽象命名空间)
     *                                对于ubc来说：ubc://eid:jettyId
     * @param ch 出参，建链成功返回的channel
     * @param opt 建链配置项
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t Connect(const std::string &serverUrl, UBSHcomChannelPtr &ch,
        const UBSHcomConnectOptions &opt = {}) = 0;

    /**
     * @brief 断开链接
     *
     * @param ch 要断开的channel
     */
    virtual void Disconnect(const UBSHcomChannelPtr &ch) = 0;

    /**
     * @brief 注册memory region，内存会在内部进行分配
     *
     * @param size memory region的大小
     * @param mr 注册好的memoryRegion
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t RegisterMemoryRegion(uint64_t size, UBSHcomRegMemoryRegion &mr) = 0;

    /**
     * @brief 注册memory region，分配的内存需要传入进来
     *
     * @param address 需要被注册为MR的内存起始地址
     * @param size memory region的大小
     * @param mr 注册好的memoryRegion
     * @return int32_t 成功：0；失败：错误码
     */
    virtual int32_t RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomRegMemoryRegion &mr) = 0;

    /**
     * @brief memory region取消注册
     *
     * @param mr 取消的mr
     */
    virtual void DestroyMemoryRegion(UBSHcomRegMemoryRegion &mr) = 0;

    /**
     * @brief 设置RegisterMemoryRegion是否将mr信息放入pgTable管理
     *        若用户需要使用RNDV，则需要设置为true
     *
     * @param enableMrCache true表示放入pgTable，false表示不放入；默认是false。
     */
    virtual void SetEnableMrCache(bool enableMrCache) = 0;

    /**
     * @brief 注册断链回调
     *
     * @param handler 断链回调函数
     * @param policy 断链回调策略
     */
    virtual void RegisterChannelBrokenHandler(const UBSHcomServiceChannelBrokenHandler &handler,
                                              const UBSHcomChannelBrokenPolicy policy) = 0;

    /**
     * @brief 注册pollCq、epoll_wait超时等回调
     *
     * @param handler 回调函数
     */
    virtual void RegisterIdleHandler(const UBSHcomServiceIdleHandler &handler) = 0;

    /**
     * @brief 注册接收receive操作回调
     *
     * @param rcvHandler 回调函数
     */
    virtual void RegisterRecvHandler(const UBSHcomServiceRecvHandler &recvHandler) = 0;

    /**
     * @brief 注册发送send操作回调
     *
     * @param sentHandler 回调函数
     */
    virtual void RegisterSendHandler(const UBSHcomServiceSendHandler &sendHandler) = 0;

    /**
     * @brief 注册单边操作回调
     *
     * @param oneSideDoneHandler 回调函数
     */
    virtual void RegisterOneSideHandler(const UBSHcomServiceOneSideDoneHandler &oneSideDoneHandler) = 0;

    // 高级配置选项及特性配置选项

    /**
     * @brief 增加workerGroup
     *
     * @param workerGroupId workerGroup的id
     * @param threadCount 该workerGroup的线程数
     * @param cpuIdsRange 该workerGroup绑定的cpuId范围
     * @param priority 同线程nice值，范围[-20,19]，-20优先级最高，19优先级最低
     * @param multirailIdx 该workerGroup绑定的rail
     */
    virtual void AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount,
        const std::pair<uint32_t, uint32_t> &cpuIdsRange, int8_t priority = 0, uint16_t multirailIdx = 0) = 0;

    /**
     * @brief 增加监听器，支持监听多个url
     *
     * @param url 监听url，tcp协议：tcp://127.0.0.1:9981；uds协议：uds://file(文件名/抽象命名空间)
     * @param workerCount 该listener监听到链接请求后，会从对应的workerGroup中选择workerCount个线程按照lbPolicy的策略去选择线程绑定到asyncEp上
     */
    virtual void AddListener(const std::string &url,  uint16_t workerCount = UINT16_MAX) = 0;

    /**
     * @brief 设置建链负载均衡策略，主动/被动建链时需要选择一个worker线程去完成，lbPolicy则代表选择worker线程的策略
     *
     * @param lbPolicy NET_ROUND_ROBIN：轮询，NET_HASH_IP_PORT：根据ip和port做hash
     */
    virtual void SetConnectLBPolicy(UBSHcomServiceLBPolicy lbPolicy) = 0;

    /**
     * @brief TLS相关配置项，如果不配置的话默认不开启
     *
     * @param opt
     */
    virtual void SetTlsOptions(const UBSHcomTlsOptions &opt) = 0;

    virtual void SetConnSecureOpt(const UBSHcomConnSecureOptions &opt) = 0;

    /**
     * @brief 设置TCP_USER_TIMEOUT套接字选项，tcp超时时间，[0, 1024]，0表示永不超时
     *
     * @param timeOutSec
     */
    virtual void SetTcpUserTimeOutSec(uint16_t timeOutSec) = 0;

    /**
     * @brief 设置TCP发送是否要做内存拷贝（hcom内部内存）
     *
     * @param tcpSendZCopy 是否要做数据拷贝
     */
    virtual void SetTcpSendZCopy(bool tcpSendZCopy) = 0;

    /**
     * @brief 设置设备ipMask，用于rdma/ub，根据ipMask获取该网段的GID和UBEId
     *
     * @param ipMasks 用于过滤的ipMask集合
     */
    virtual void SetDeviceIpMask(const std::vector<std::string> &ipMasks) = 0;

    /**
     * @brief 设置设备的ipGroup，如果明确制定了ipGroup，则直接使用对应的设备
     *
     * @param ipGroups ipGroups集合
     */
    virtual void SetDeviceIpGroups(const std::vector<std::string> &ipGroups) = 0;

    /**
     * @brief 设置cq队列的深度
     *
     * @param depth cq队列深度
     */
    virtual void SetCompletionQueueDepth(uint16_t depth) = 0;

    /**
     * @brief 设置SQ队列的大小，默认256
     *
     * @param sqSize 队列大小
     */
    virtual void SetSendQueueSize(uint32_t sqSize) = 0;

    /**
     * @brief 设置RQ队列的大小，默认256
     *
     * @param rqSize 队列大小
     */
    virtual void SetRecvQueueSize(uint32_t rqSize) = 0;

    /**
     * @brief 设置提前下发wr的数量，不设置的话默认64
     * @param prePostSize 预先下发的wr数量
     */
    virtual void SetQueuePrePostSize(uint32_t prePostSize) = 0;

    /**
     * @brief 设置批量polling的大小，默认是4
     *
     * @param pollSize 每批大小
     */
    virtual void SetPollingBatchSize(uint16_t pollSize) = 0;

    /**
     * @brief 设置polling的超时时间，单位us，默认500
     *
     * @param pollTimeout 超时时间
     */
    virtual void SetEventPollingTimeOutUs(uint16_t pollTimeout) = 0;

    /**
     * @brief 设置周期任务处理线程数，主要用在内部异步检查超时等场景，不设置的话默认1个线程
     *
     * @param threadNum 线程数
     */
    virtual void SetTimeOutDetectionThreadNum(uint32_t threadNum) = 0;

    /**
     * @brief 设置最大连接数，不设置的话默认250
     *
     * @param maxConnCount 最大连接数
     */
    virtual void SetMaxConnectionCount(uint32_t maxConnCount) = 0;

    /**
     * @brief 设置心跳选项
     *
     * @param opt 心跳设置选项
     * @return int32_t
     */
    virtual void SetHeartBeatOptions(const UBSHcomHeartBeatOptions &opt) = 0;

    /**
     * @brief Set the Multi Rail Options object
     *
     * @param opt multi rail option
     */
    virtual void SetMultiRailOptions(const UBSHcomMultiRailOptions &opt) = 0;

    /**
     * @brief 设置 UB-C 多路径模式
     *
     * @param ubcMode UB-C 多路径模式
     */
    virtual void SetUbcMode(UBSHcomUbcMode ubcMode) = 0;

    /**
     * @brief 设置发送数据块最大数量
     *
     * @param maxSendRecvDataCount 发送数据块最大数量
     */
    virtual void SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount) = 0;

    virtual ~UBSHcomService() {}

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    virtual int32_t DoDestroy(const std::string &name) = 0;

private:
    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

}
}
#endif // HCOM_SERVICE_H