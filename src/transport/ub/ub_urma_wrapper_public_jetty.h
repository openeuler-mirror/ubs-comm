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

#ifndef HCOM_UB_URMA_WRAPPER_PUBLIC_JETTY_H
#define HCOM_UB_URMA_WRAPPER_PUBLIC_JETTY_H
#ifdef UB_BUILD_ENABLED

#include "net_oob.h"
#include "net_load_balance.h"
#include "ub_common.h"
#include "ub_fixed_mem_pool.h"
#include "ub_urma_wrapper_jetty.h"
#include "ub_thread_pool.h"

namespace ock {
namespace hcom {

enum UrmaConnectMsgType : uint8_t {
    CONNECT_REQ = 1,
    EXCHANGE_MSG = 2,
};

struct JettyConnHeader {
    UrmaConnectMsgType msgType;
    uint64_t epId = 0;
    UBJettyExchangeInfo info{};
    uint32_t controlJettyId = 0;
    struct {
        uint64_t magic : 16;
        uint64_t version : 8;
        uint64_t groupIndex : 8;
        uint64_t protocol : 8;
        uint64_t bandWidth : 8;
        uint64_t devIndex : 8;
        uint64_t majorVersion : 8;
        uint64_t minorVersion : 8;
        uint64_t tlsVersion : 16;
        uint64_t reserve : 40;
    } ConnectHeader;
    uint32_t payloadLen = 0;
    char payload[1024];

    inline void SetConnHeader(uint32_t magic, uint32_t version, uint32_t groupIndex, uint32_t protocol,
    uint32_t majorVersion, uint32_t minorVersion, uint32_t tlsVersion)
    {
        ConnectHeader.magic = magic;
        ConnectHeader.version = version;
        ConnectHeader.groupIndex = groupIndex;
        ConnectHeader.protocol = protocol;
        ConnectHeader.majorVersion = majorVersion;
        ConnectHeader.minorVersion = minorVersion;
        ConnectHeader.tlsVersion = tlsVersion;
    }
} __attribute__((packed));

struct JettyConnResp {
    UrmaConnectMsgType msgType;
    ConnectResp connResp = OK;
    uint64_t epId = 0;
    UBJettyExchangeInfo info{};
    uint32_t serverCtrlJettyId = 0;
    urma_eid_t serverCtrlEid{};
} __attribute__((packed));

#define PUBLIC_JETTY_SEG_SIZE 2560


class UBPublicJetty {
public:
    using NewConnectionHandler = std::function<NResult(UBOpContextInfo *)>;

    UBPublicJetty(const std::string &name, uint32_t id, UBContext *ctx, UBJfc *jfc, bool isServer = false,
        JettyOptions jettyOptions = {}) : mName(name), mId(id), mUBContext(ctx), mSendJfc(jfc), mRecvJfc(jfc),
        isServer(isServer), mJettyOptions(jettyOptions)
    {
        mIsStarted = false;
        if (mUBContext != nullptr) {
            mUBContext->IncreaseRef();
        }

        if (mSendJfc != nullptr) {
            mSendJfc->IncreaseRef();
        }
        mPollTimeout = GetPollTimeout();
        OBJ_GC_INCREASE(UBPublicJetty);
    }

    ~UBPublicJetty()
    {
        UnInitialize();
        OBJ_GC_DECREASE(UBPublicJetty);
    }

    /* create public(URMA_TM_RM) jetty */
    UResult CreateUrmaPublicJetty(uint32_t id);
    UResult InitializePublicJetty(uint32_t id);
    UResult CreateCtxInfoPool();
    void ProcessWorkerCompletion(UBOpContextInfo *ctx);

    UResult StartPublicJetty();
    void RunInThread();
    void ProcessPollingResult(urma_cr_t &wc);
    int NewRequest(UBOpContextInfo *ctx);
    int SendFinished(UBOpContextInfo *ctx);
    UResult ImportPublicJetty(const urma_eid_t &remoteEid, uint32_t jettyId);
    UResult SendByPublicJetty(const void *buf, uint32_t size);
    UResult PollingCompletion();
    UResult Receive(void *buf, uint32_t size);
    UResult PostReceive(uintptr_t bufAddr, uint32_t bufSize, urma_target_seg_t *localSeg, uint64_t context);
    UResult CheckRecvResult(urma_cr_t wc, uint32_t size, UResult result, uint32_t pollCount, int32_t timeoutInMs);

    UResult CreateJettyMr();
    UResult UnInitialize();
    void Stop();
    inline void SetNewConnCB(const NewConnectionHandler &handler)
    {
        mNewConnectionHandler = handler;
    }
    inline UBSHcomNetDriverProtocol GetProtocol()
    {
        return mUBContext->protocol;
    }

    inline uint32_t GetJettyId()
    {
        if (mUrmaJetty != nullptr) {
            return mUrmaJetty->jetty_id.id;
        }
        return 0;
    }

    inline urma_eid_t GetEid()
    {
        return mUBContext->mBestEid.urmaEid;
    }

    inline void SetWorkerLb(NetWorkerLB *lb)
    {
        if (lb != nullptr) {
            mWorkerLb = lb;
        }
    }

    inline const NetWorkerLBPtr &LoadBalancer() const
    {
        return mWorkerLb;
    }

    bool GetFreeBuff(uintptr_t &item);
    bool ReturnBuffer(uintptr_t value);
    bool GetFreeBufferN(uintptr_t *&items, uint32_t n);
    urma_target_seg_t *GetMemorySeg();

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

public:
    inline uint32_t QpNum() const
    {
        if (NN_UNLIKELY(mUrmaJetty == nullptr)) {
            return 0xffffffff;
        }

        return mUrmaJetty->jetty_id.id;
    }

    static uint32_t NewId()
    {
        return __sync_fetch_and_add(&G_INDEX, 1);
    }

private:
    void FillJfsCfg(urma_jfs_cfg_t *jfs_cfg);
    void FillJfrCfg(urma_jfr_cfg_t *jfr_cfg);
    static long GetPollTimeout()
    {
        static long timeout = []() {
            long res = NetFunc::NN_GetLongEnv("HCOM_UB_CONNECTION_POLL_TIMEOUT", NN_NO1, NN_NO180, NN_NO60);
            NN_LOG_INFO("Public jetty polling timeout is " << res << " s");
            return res;
        }();
        return timeout;
    }

private:
    std::string mName;
    bool isServer = false;
    std::atomic<bool> mIsStarted;
    uint32_t mId = 0;
    std::mutex mStopMutex;
    UBContext *mUBContext = nullptr;
    UBJfc *mSendJfc = nullptr;
    UBJfc *mRecvJfc = nullptr;
    urma_jfr_t *mJfr = nullptr;
    JettyOptions mJettyOptions{};
    uint32_t mUrmaJettyId = 0; // mUrmaJetty->jetty_id.id
    urma_jetty_t *mUrmaJetty = nullptr;
    urma_target_jetty_t *mTargetJetty = nullptr;
    UBMemoryRegionFixedBuffer *mJettyMr = nullptr;
    UBFixedMemPool *mCtxInfoPool = nullptr;
    // public polling thread
    std::thread mPublicJettyPollingThread;
    bool mNeedStop = true;
    NewConnectionHandler mNewConnectionHandler = nullptr;
    NetWorkerLBPtr mWorkerLb = nullptr;
    UBThreadPool *mThreadPool = nullptr;

    int32_t mOneSideMaxWr = JETTY_MAX_SEND_WR - NN_NO64;
    int32_t mOneSideRef = JETTY_MAX_SEND_WR - NN_NO64;
    int32_t mPostSendMaxWr = NN_NO64;
    int32_t mPostSendMaxSize = NN_NO1024;
    int32_t mPostSendRef = NN_NO64;
    long mPollTimeout = NN_NO60;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    static uint32_t G_INDEX;
};
}
}
#endif
#endif // HCOM_UB_URMA_WRAPPER_PUBLIC_JETTY_H