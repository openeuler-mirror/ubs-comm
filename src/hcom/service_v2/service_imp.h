/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef HCOM_SERVICE_V2_HCOM_SERVICE_IMP_H_
#define HCOM_SERVICE_V2_HCOM_SERVICE_IMP_H_

#include <cstdint>
#include <mutex>
#include <string>

#include "hcom_def.h"
#include "hcom_log.h"
#include "api/hcom_service_def.h"
#include "api/hcom_service.h"
#include "api/hcom_service_channel.h"
#include "hcom_obj_statistics.h"
#include "service_periodic_manager.h"
#include "net_common.h"
#include "net_load_balance.h"
#include "net_oob.h"
#include "net_oob_ssl.h"
#include "net_pgtable.h"

namespace ock {
namespace hcom {

using NetDriverPtr = NetRef<UBSHcomNetDriver>;
using HcomPeriodicManagerPtr = NetRef<HcomPeriodicManager>;
using HcomServiceCtxStorePtr = NetRef<HcomServiceCtxStore>;
using ConnectingEpInfoPtr = NetRef<HcomConnectingEpInfo>;
using netPgTablePtr = NetRef<NetPgTable>;

struct HcomServiceImpOptions {
    UBSHcomTlsOptions tlsOption;
    UBSHcomConnSecureOptions connSecOption;
    UBSHcomHeartBeatOptions heartBeatOption;
    UBSHcomServiceIdleHandler idleHandler = nullptr;
    UBSHcomServiceRecvHandler recvHandler = nullptr;
    UBSHcomServiceSendHandler sendHandler = nullptr;
    UBSHcomServiceOneSideDoneHandler oneSideDoneHandler = nullptr;
    UBSHcomServiceNewChannelHandler chNewHandler = nullptr;
    UBSHcomServiceChannelBrokenHandler chBrokenHandler = nullptr;
    uint32_t qpSendQueueSize = 256;
    uint32_t qpRecvQueueSize = 256;
    uint32_t qpPrePostSize = 64;
    uint32_t maxSendRecvDataSize = 1024;
    uint32_t maxSendRecvDataCount = 8192;
    uint32_t maxConnCount = 250;
    uint32_t multiRailThresh = 8192;
    uint32_t timeOutDetectThreadNum = 1;
    uint32_t ctxStoreCapacity = 2097152;
    uint32_t ubPriority = UINT32_MAX;
    uint16_t pollingBatchSize = 4;
    uint16_t eventPollingTimeOutUs = 500;
    uint16_t completionQueueDepth = 2048;
    uint16_t tcpTimeOutSec = -1;
    uint16_t jettyId = 0;
    UBSHcomServiceProtocol protocol;
    std::string name;
    std::string eid;
    bool enableRndv = false;
    bool tcpSendZCopy = false;
    bool startOobSvr = false;
    bool activateBackup = false; // 主备开关
    bool enableMultiRail = false; // 关闭时才会启用主备切换功能
    NetDriverOobType oobType = NET_OOB_TCP;
    UBSHcomServiceLBPolicy lbPolicy = NET_ROUND_ROBIN;
    UBSHcomWorkerMode workerGroupMode = NET_BUSY_POLLING;
    UBSHcomChannelBrokenPolicy chBrokenPolicy = UBSHcomChannelBrokenPolicy::BROKEN_ALL;
    UBSHcomUbcMode ubcMode = UBSHcomUbcMode::LowLatency;
    std::vector<std::vector<UBSHcomWorkerGroupInfo>> workerGroupInfos;
    std::vector<std::string> ipMasks;
    std::vector<std::string> ipGroups;
    std::unordered_map<std::string, UBSHcomNetOobListenerOptions> oobOption;
    std::unordered_map<std::string, UBSHcomNetOobUDSListenerOptions> udsOobOption;
};

class HcomServiceImp : public UBSHcomService {
public:
    HcomServiceImp(UBSHcomServiceProtocol t, const std::string &name, const UBSHcomServiceOptions &opt)
    {
        mOptions.protocol = t;
        mOptions.name = name;
        mOptions.maxSendRecvDataSize = opt.maxSendRecvDataSize;
        mOptions.workerGroupMode = opt.workerGroupMode;
        if (NN_LIKELY(opt.workerGroupThreadCount != 0)) {
            UBSHcomWorkerGroupInfo groupInfo;
            groupInfo.threadPriority = opt.workerThreadPriority;
            groupInfo.threadCount = opt.workerGroupThreadCount;
            groupInfo.groupId = opt.workerGroupId;
            groupInfo.cpuIdsRange = opt.workerGroupCpuIdsRange;
            std::vector<UBSHcomWorkerGroupInfo> workerInfoVec {};
            workerInfoVec.emplace_back(groupInfo);
            mOptions.workerGroupInfos.emplace_back(workerInfoVec);      // UBSHcomService::Instance lock
        }
        OBJ_GC_INCREASE(HcomServiceImp);
    }

    ~HcomServiceImp() override
    {
        OBJ_GC_DECREASE(HcomServiceImp);
    }

    /**
     * @brief 绑定监听url，指定监听的类型及url，客户端可以不调用Bind。
     *
     * @param listenerUrl 监听url，对于tcp来说：tcp://127.0.0.1:9981
     *                          对于uds来说：uds://file:perm（如果有:perm则使用真实文件，perm格式如：0600，没有则使用抽象文件）
     *                          对于ubc来说：ubc://eid:jettyId
     * @param handler 收到建链请求后的回调函数
     * @return int32_t 成功：0；失败：错误码
     */
    int32_t Bind(const std::string &listenerUrl, const UBSHcomServiceNewChannelHandler &handler) override;

    /**
     * @brief 开启服务，如果调用过Bind，则同时开启监听，否则不进行监听
     *
     * @return int32_t 成功：0；失败：错误码
     */
    int32_t Start() override;

    /**
     * @brief 建立链接
     *
     * @param serverUrl 建连服务端url，对于tcp来说：tcp://127.0.0.1:9981
     *                                对于uds来说：uds://file(文件名/抽象命名空间)
     *                                对于ubc来说：ubc://eid:jettyId
     * @param ch 出参，建链成功返回的channel
     * @return int32_t 成功：0；失败：错误码
     */
    int32_t Connect(const std::string &serverUrl, UBSHcomChannelPtr &ch, const UBSHcomConnectOptions &opt) override;

    /**
     * @brief 断开链接
     *
     * @param ch 要断开的channel
     */
    void Disconnect(const UBSHcomChannelPtr &ch) override;

    /**
     * @brief 注册memory region，内存会在内部进行分配
     *
     * @param size memory region的大小
     * @param mr 注册好的memoryRegion
     * @return int32_t 成功：0；失败：错误码
     */
    int32_t RegisterMemoryRegion(uint64_t size, UBSHcomRegMemoryRegion &mr) override;

    /**
     * @brief 注册memory region，分配的内存需要传入进来
     *
     * @param address 需要被注册为MR的内存起始地址
     * @param size memory region的大小
     * @param mr 注册好的memoryRegion
     * @return int32_t 成功：0；失败：错误码
     */
    int32_t RegisterMemoryRegion(uintptr_t address, uint64_t size, UBSHcomRegMemoryRegion &mr) override;
    int32_t ImportUrmaSeg(uintptr_t address, uint64_t size, UBSHcomMemoryKey &key) override;

    /**
     * @brief memory region取消注册
     *
     * @param mr 取消的mr
     */
    void DestroyMemoryRegion(UBSHcomRegMemoryRegion &mr) override;

    /**
     * @brief 设置RegisterMemoryRegion是否将mr放入pgTable管理
     *        若用户需要使用RNDV，则需要设置为true
     *
     * @param enableMrCache true表示放入pgTable，false表示不放入；默认是false。
     */
    void SetEnableMrCache(bool enableMrCache) override;

    /**
     * @brief 注册断链回调
     *
     * @param handler 断链回调函数
     * @param policy 断链回调策略
     */
    void RegisterChannelBrokenHandler(const UBSHcomServiceChannelBrokenHandler &handler,
                                      const UBSHcomChannelBrokenPolicy policy) override;

    /**
     * @brief 注册pollCq、epoll_wait超时等回调
     *
     * @param handler 回调函数
     */
    void RegisterIdleHandler(const UBSHcomServiceIdleHandler &handler) override;

    /**
     * @brief 注册接收receive操作回调
     *
     * @param rcvHandler 回调函数
     */
    void RegisterRecvHandler(const UBSHcomServiceRecvHandler &recvHandler) override;

    /**
     * @brief 注册发送send操作回调
     *
     * @param sentHandler 回调函数
     */
    void RegisterSendHandler(const UBSHcomServiceSendHandler &sendHandler) override;

    /**
     * @brief 注册单边操作回调
     *
     * @param oneSideDoneHandler 回调函数
     */
    void RegisterOneSideHandler(const UBSHcomServiceOneSideDoneHandler &oneSideDoneHandler) override;

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
    void AddWorkerGroup(uint16_t workerGroupId, uint32_t threadCount,
        const std::pair<uint32_t, uint32_t> &cpuIdsRange, int8_t priority = 0, uint16_t multirailIdx = 0) override;

    /**
     * @brief 增加监听器，支持监听多个url
     *
     * @param url 监听url，tcp协议：tcp://127.0.0.1:9981；uds协议：uds://file(文件名/抽象命名空间)
     * @param workerCount 监听到链接请求后，会从对应的workerGroup中选择workerCount个线程按照lbPolicy的策略去选择线程绑定到ep
     * @return int32_t
     */
    void AddListener(const std::string &url, uint16_t workerCount = UINT16_MAX) override;

    /**
     * @brief 设置建链负载均衡策略，主动/被动建链时需要选择一个worker线程去完成，lbPolicy则代表选择worker线程的策略
     *
     * @param lbPolicy NET_ROUND_ROBIN：轮询，NET_HASH_IP_PORT：根据ip和port做hash
     */
    void SetConnectLBPolicy(UBSHcomServiceLBPolicy lbPolicy) override;

    /**
     * @brief TLS相关配置项，如果不配置的话默认不开启
     *
     * @param opt
     */
    void SetTlsOptions(const UBSHcomTlsOptions &opt) override;

    void SetConnSecureOpt(const UBSHcomConnSecureOptions &opt) override;

    /**
     * @brief 设置TCP_USER_TIMEOUT套接字选项，tcp超时时间，[0, 1024]，0表示永不超时
     *
     * @param timeOutSec
     */
    void SetTcpUserTimeOutSec(uint16_t timeOutSec) override;

    /**
     * @brief 设置TCP发送是否要做内存拷贝（hcom内部内存）
     *
     * @param tcpSendZCopy 是否要做数据拷贝
     */
    void SetTcpSendZCopy(bool tcpSendZCopy) override;

    /**
     * @brief 设置设备ipMask，用于rdma/ub，根据ipMask获取该网段的GID和UBEId
     *
     * @param ipMasks 用于过滤的ipMask集合
     */
    void SetDeviceIpMask(const std::vector<std::string> &ipMasks) override;

    /**
     * @brief 设置设备的ipGroup，如果明确制定了ipGroup，则直接使用对应的设备
     *
     * @param ipGroups ipGroups集合
     */
    void SetDeviceIpGroups(const std::vector<std::string> &ipGroups) override;

    /**
     * @brief 设置cq队列的深度
     *
     * @param depth cq队列深度
     */
    void SetCompletionQueueDepth(uint16_t depth) override;

    /**
     * @brief 设置SQ队列的大小，默认256
     *
     * @param sqSize 队列大小
     */
    void SetSendQueueSize(uint32_t sqSize) override;

    /**
     * @brief 设置RQ队列的大小，默认256
     *
     * @param rqSize 队列大小
     */
    void SetRecvQueueSize(uint32_t rqSize) override;

    /**
     * @brief 设置提前下发wr的数量，不设置的话默认64
     *
     * @param prePostSize 预先下发的wr数量
     */
    void SetQueuePrePostSize(uint32_t prePostSize) override;

    /**
     * @brief 设置批量polling的大小，默认是4
     *
     * @param pollSize 每批大小
     */
    void SetPollingBatchSize(uint16_t pollSize) override;

    /**
     * @brief 设置polling的超时时间，单位us，默认500
     *
     * @param pollTimeout 超时时间
     */
    void SetEventPollingTimeOutUs(uint16_t pollTimeout) override;

    /**
     * @brief 设置周期任务处理线程数，主要用在内部异步检查超时等场景，不设置的话默认1个线程
     *
     * @param threadNum 线程数
     */
    void SetTimeOutDetectionThreadNum(uint32_t threadNum) override;

    /**
     * @brief 设置最大连接数，不设置的话默认250
     *
     * @param maxConnCount 最大连接数
     */
    void SetMaxConnectionCount(uint32_t maxConnCount) override;

    /**
     * @brief 设置心跳选项
     *
     * @param opt 心跳设置选项
     * @return int32_t
     */
    void SetHeartBeatOptions(const UBSHcomHeartBeatOptions &opt) override;

    /**
     * @brief 设置主备切换功能
     *
     * @param enable 主备启用设置选项
     */
    void SetActiveBackup(bool enable) override;

    /**
     * @brief Set the Multi Rail Options object
     *
     * @param opt multi rail option
     */
    void SetMultiRailOptions(const UBSHcomMultiRailOptions &opt) override;

    /**
     * @brief 设置 UB-C 多路径模式
     *
     * @param ubcMode UB-C 多路径模式
     */
    void SetUbcMode(UBSHcomUbcMode ubcMode) override;

    /**
     * @brief 设置发送数据块最大数量
     *
     * @param maxSendRecvDataCount 发送数据块最大数量
     */
    void SetMaxSendRecvDataCount(uint32_t maxSendRecvDataCount) override;

    /**
     * @brief 设置ctx store容量
     *
     * @param ctxStoreCapacity ctx store容量, 取值范围[128, 16777216], 默认2097152
     */
    void SetCtxStoreCapacity(uint32_t ctxStoreCapacity) override;

    /**
     * @brief 设置SL优先级
     *
     * @param ubPriority SL优先级, 取值范围[0， 15]
     */
    void SetUbPriority(uint32_t ubPriority) override;

private:
    SerResult ValidateServiceOption();
    SerResult CreateResource();
    SerResult InitDriver();
    SerResult DoInitDriver();
    SerResult CreateMultiRailDriver();
    SerResult CreateOobUdsListeners(const UBSHcomNetDriverOptions &driverOpt);
    SerResult CreateOobListeners(const UBSHcomNetDriverOptions &driverOpt);

    SerResult StartDriver();
    SerResult CreatePeriodicMgr();
    SerResult CreateCtxMemPool();
    SerResult DoConnect(const std::string &serverUrl, SerConnInfo &opt, const std::string &payLoad,
        UBSHcomChannelPtr &tmpChannel);
    SerResult DoConnectInner(const std::string &serverUrl, SerConnInfo &opt, const std::string &payLoad,
                             std::vector<UBSHcomNetEndpointPtr> &epVector, uint32_t &totalBandWidth);
    SerResult ChooseDriver(OOBTCPConnection &conn, UBSHcomNetDriver *&driver);
    void DoChooseDriver(uint8_t devInex, uint8_t bandWidth,
        int8_t &selectDevIndex, uint8_t &selectBandWidth, UBSHcomNetDriver *&driver);

    void ConvertHcomSerImpOptsToHcomDriOpts(const HcomServiceImpOptions &serviceOpt,
        UBSHcomNetDriverOptions &driverOpt);
    void RegisterDriverCb();
    bool RunRequestCallback(UBSHcomChannel *channel, const UBSHcomRequestContext &ctx, UBSHcomServiceContext &context);

    SerResult DoDestroy(const std::string &name) override;
    void ForceStop();
    SerResult AddTcpOobListener(const std::string &url, uint16_t workerCount = UINT16_MAX);
    SerResult AddUdsOobListener(const std::string &url, uint16_t workerCount = UINT16_MAX);

    SerResult DelayEraseChannel(UBSHcomChannelPtr &ch, uint16_t delayTime);
    void EraseChannel(uintptr_t chPtr);

    SerResult GenerateUuid(const std::string &ipInfo, uint64_t channelId, std::string &uuid);
    SerResult GenerateUuid(uint32_t ip, uint64_t channelId, std::string &uuid);
    int32_t ServiceNewChannel(const std::string &ipPort, SerConnInfo &connInfo, const std::string &userPayLoad,
        std::vector<UBSHcomNetEndpointPtr> &ep);
    int32_t ServiceHandleNewEndPoint(const std::string &ipPort, const UBSHcomNetEndpointPtr &newEp,
        const std::string &payload);
    SerResult EmplaceNewEndpoint(const UBSHcomNetEndpointPtr &newEp, ConnectingEpInfoPtr &epInfo,
        SerConnInfo &connInfo, std::string &uuid);
    void ServiceEndPointBroken(const UBSHcomNetEndpointPtr &netEp);
    int32_t ServiceRequestReceived(const UBSHcomRequestContext &ctx);
    int32_t ServiceRequestPosted(const UBSHcomRequestContext &ctx);
    int32_t ServiceOneSideDone(const UBSHcomRequestContext &ctx);
    int32_t ServiceSecInfoProvider(uint64_t ctx, int64_t &flag, UBSHcomNetDriverSecType &type, char *&output,
        uint32_t &outLen, bool &needAutoFree);
    int32_t ServiceSecInfoValidator(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen);
    SerResult ExchangeTimestamp(UBSHcomChannel *channel);
    int ServiceExchangeTimeStampHandle(UBSHcomServiceContext &ctx);
    std::string GetFilteredDeviceIP(const std::string& ipMask);
    /**
     * @brief MultiRail模式下注册的Connection事件处理函数
     *
     * @param conn
     * @return int32_t
     */
    SerResult NewConnectionCB(OOBTCPConnection &conn);

    inline UBSHcomServiceProtocol Protocol() const
    {
        return mOptions.protocol;
    }

    inline SerResult GetIpAddressByIpPort(const std::string &oobIpPort, uint32_t &ipAddress) const
    {
        if (Protocol() == SHM) {
            ipAddress = 0xffffffff;
        } else {
            if (NN_UNLIKELY(!NetFunc::NN_CovertIpWithoutPort(oobIpPort, ipAddress))) {
                NN_LOG_ERROR("Default imp Failed to covert ip by " << oobIpPort);
                return SER_INVALID_PARAM;
            }
        }
        return SER_OK;
    }

    inline SerResult EmplaceChannelUuid(UBSHcomChannelPtr &channel)
    {
        std::lock_guard<std::mutex> lockerChannel(mChannelMutex);
        auto ret = mChannelMap.emplace(channel.Get()->GetUuid(), channel);
        if (NN_UNLIKELY(!ret.second)) {
            NN_LOG_ERROR("Failed to emplace channel " << channel.Get()->GetId() << ", already exist");
            return SER_ERROR;
        }
        return SER_OK;
    }

    int32_t ServicePrivateOpHandle(UBSHcomServiceContext &ctx);

    static PgtDir *pgdAlloc(const PgTable &pgtable)
    {
        return new PgtDir;
    }

    static void pgdFree(const PgTable &pgtable, PgtDir *pgdir)
    {
        delete pgdir;
    }

    void DestroyNetMrs(std::vector<UBSHcomMemoryRegionPtr> &netMrs, uint32_t start, uint32_t end);

    SerResult InsertPgTable(UBSHcomNetMemoryRegionPtr &mr);

private:
    HcomServiceImpOptions mOptions;
    std::string mOobIp;
    std::vector<NetDriverPtr> mDriverPtrs;
    HcomPeriodicManagerPtr mPeriodicMgr = nullptr;
    NetMemPoolFixedPtr mContextMemPool = nullptr;
    bool mStarted = false;

    std::mutex mStartMutex;
    std::mutex mOptionsMutex;
    std::mutex mNewEpMutex;
    std::mutex mChannelMutex;

    std::map<std::string, ConnectingEpInfoPtr> mNewEpMap;   // temporary storage eps until create channel
    std::map<uint64_t, ConnectingSecInfo> mSecInfoMap;      // temporary storage secInfo
    std::map<std::string, UBSHcomChannelPtr> mChannelMap;
    std::vector<NetOOBServer *> mOobServers;    //  oob server need to be configed when enable multirail
    std::map<uint8_t, uint8_t> mDriverPair;    /* local driver Index and remote driver Index map have been connected
                                                    to each other */
    std::vector<uint8_t> mUseId;               /* local driver Index has been used */
    uint32_t mDriverIndex = 0;
    netPgTablePtr mPgtable = nullptr;
    bool mEnableMrCache = false;        //  mr into pgTable for management
};

}
}
#endif // HCOM_SERVICE_V2_HCOM_SERVICE_IMP_H_