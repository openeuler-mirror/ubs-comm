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

#ifndef HCOM_UB_WORKER_H
#define HCOM_UB_WORKER_H
#ifdef UB_BUILD_ENABLED

#include <atomic>
#include <mutex>
#include <sys/resource.h>
#include <thread>
#include <set>
#include <utility>
#include <pthread.h>

#include "net_ctx_info_pool.h"
#include "net_ub_endpoint.h"
#include "ub_urma_wrapper_jetty.h"
#include "ub_jetty_ptr_map.h"

namespace ock {
namespace hcom {
using UBNewReqHandler = std::function<int(UBOpContextInfo *)>;
using UBPostedHandler = std::function<int(UBOpContextInfo *)>;
using UBOneSideDoneHandler = std::function<int(UBOpContextInfo *)>;

// when there is no request from cq, call this
using UBIdleHandler = UBSHcomNetDriverIdleHandler;

using UBOpContextInfoPool = OpContextInfoPool<UBOpContextInfo>;
using UBSglContextInfoPool = OpContextInfoPool<UBSglContextInfo>;

enum UBWorkerType : uint8_t {
    UB_SENDER = 0,
    UB_RECEIVER = 1,
    UB_SENDER_RECEIVER = 2,
};

std::string &WorkerTypeToString(UBWorkerType tp);
std::string &PollingModeToString(UBPollingMode m);

using UBWorkerOptions = struct UBWorkerOptionsStruct {
    UBWorkerType workerType = UBWorkerType::UB_RECEIVER;
    UBPollingMode workerMode = UBPollingMode::UB_BUSY_POLLING;
    int16_t cpuId = -1;
    uint16_t completionQueueDepth = NN_NO2048;
    uint16_t maxPostSendCountPerQP = NN_NO64;
    uint16_t prePostReceiveSizePerQP = NN_NO64;
    uint16_t pollingBatchSize = NN_NO4;
    uint32_t qpSendQueueSize = NN_NO256;
    uint32_t qpReceiveQueueSize = NN_NO256;
    uint32_t qpMrSegSize = NN_NO1024;
    uint32_t qpMrSegCount = NN_NO64;
    uint32_t eventPollingTimeout = NN_NO500;
    bool dontStartWorkers = false;
    /* worker thread priority [-20,20], 20 is the lowest, -20 is the highest, 0 (default) means do not set priority */
    int threadPriority = 0;
    uint8_t slave = 1;
    UBSHcomUbcMode ubcMode = UBSHcomUbcMode::LowLatency;

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "options type: " << WorkerTypeToString(workerType) << ", mode: " << PollingModeToString(workerMode) <<
            ", jfc size: " << completionQueueDepth << ", max post send: " << maxPostSendCountPerQP <<
            ", pre-post receive size: " << prePostReceiveSizePerQP << ", poll batch size " << pollingBatchSize <<
            ", cpu id: " << cpuId << ", jetty send queue: " << qpSendQueueSize << ", jetty receive queue: " <<
            qpReceiveQueueSize << ", dontStartWorkers: " << dontStartWorkers;
        return oss.str();
    }

    void SetValue(const UBSHcomNetDriverOptions &opt)
    {
        workerType = UBWorkerType::UB_SENDER_RECEIVER;
        completionQueueDepth = opt.completionQueueDepth;
        maxPostSendCountPerQP = opt.maxPostSendCountPerQP;
        prePostReceiveSizePerQP = opt.prePostReceiveSizePerQP;
        pollingBatchSize = opt.pollingBatchSize;
        if (opt.mode == NET_EVENT_POLLING) {
            workerMode = UBPollingMode::UB_EVENT_POLLING;
        } else if (opt.mode == NET_BUSY_POLLING) {
            workerMode = UBPollingMode::UB_BUSY_POLLING;
        }
        qpSendQueueSize = opt.qpSendQueueSize;
        qpReceiveQueueSize = opt.qpReceiveQueueSize;
        qpMrSegSize = opt.mrSendReceiveSegSize;
        qpMrSegCount = opt.prePostReceiveSizePerQP;
        eventPollingTimeout = opt.eventPollingTimeout;
        dontStartWorkers = opt.dontStartWorkers;
        threadPriority = opt.workerThreadPriority;
        slave = opt.slave;
        ubcMode = opt.ubcMode;
    }
};

class UBWorker {
public:
    UBWorker(const std::string &name, UBContext *ctx, const UBWorkerOptions &options, const NetMemPoolFixedPtr &memPool,
        const NetMemPoolFixedPtr &sglMemPool);

    virtual ~UBWorker()
    {
        UnInitialize();
        OBJ_GC_DECREASE(UBWorker);
    }

    UResult Initialize();
    UResult UnInitialize();
    UResult ReInitializeCQ();

    UResult Start();
    UResult Stop();

    inline void SetIndex(const UBSHcomNetWorkerIndex &value)
    {
        mIndex = value;
    }

    inline const UBSHcomNetWorkerIndex &Index() const
    {
        return mIndex;
    }

    inline bool IsWorkStarted(uint32_t timeOutSecond = NN_NO8)
    {
        uint64_t count = static_cast<uint64_t>(timeOutSecond) * NN_NO1000000 / NN_NO100;
        while (--count > 0 && !mProgressThreadStarted.load()) {
            usleep(NN_NO100);
        }

        if (count > 0) {
            return true;
        } else {
            return false;
        }
    }

    UResult CreateQP(UBJetty *&qp);

    UResult PostReceive(UBJetty *qp, uintptr_t bufAddress, uint32_t bufSize, urma_target_seg_t *localSeg);
    UResult PostSend(UBJetty *qp, const UBSendReadWriteRequest &req, urma_target_seg_t *localSeg, uint32_t immData = 0);
    UResult PostSendSglInline(UBJetty *qp, const UBSendSglInlineHeader &header, const UBSendReadWriteRequest &req,
        uint32_t immData = 0);
    UResult PostSendSgl(UBJetty *qp, const UBSHcomNetTransSglRequest &req, const UBSHcomNetTransRequest &tlsReq,
        uint32_t immData, bool isEncrypted);
    UResult PostRead(UBJetty *qp, const UBSendReadWriteRequest &req);
    UResult PostWrite(UBJetty *qp, const UBSendReadWriteRequest &req,
        UBOpContextInfo::OpType type = UBOpContextInfo::WRITE);
    UResult RePostReceive(UBOpContextInfo *ctx);
    UResult CreateOneSideCtx(const UBSgeCtxInfo &sgeInfo, const UBSHcomNetTransSgeIov *iov, uint32_t iovCount,
        uint64_t (&ctxArr)[NET_SGE_MAX_IOV], bool isRead);
    UResult PostOneSideSgl(UBJetty *qp, const UBSendSglRWRequest &req, bool isRead = true);

    inline UBOpContextInfo *GetOpContextInfo()
    {
        return mOpCtxInfoPool.Get();
    }

    inline void ReturnOpContextInfo(UBOpContextInfo *&ctx)
    {
        if (NN_LIKELY(ctx != nullptr)) {
            if (NN_LIKELY(ctx->ubJetty != nullptr)) {
                ctx->ubJetty->DecreaseRef();
            }
            mOpCtxInfoPool.Return(ctx);
            ctx = nullptr;
        }
    }

    inline void ReturnSglContextInfo(UBSglContextInfo *&ctx)
    {
        if (NN_LIKELY(ctx != nullptr)) {
            mSglCtxInfoPool.Return(ctx);
            ctx = nullptr;
        }
    }

    inline void RegisterNewRequestHandler(const UBNewReqHandler &handler)
    {
        mNewRequestHandler = handler;
    }

    inline void RegisterPostedHandler(const UBPostedHandler &handler)
    {
        mSendPostedHandler = handler;
    }

    inline void RegisterOneSideDoneHandler(const UBOneSideDoneHandler &handler)
    {
        mOneSideDoneHandler = handler;
    }

    inline void RegisterIdleHandler(const UBIdleHandler &handler)
    {
        mIdleHandler = handler;
    }

    inline const std::string &Name() const
    {
        return mName;
    }

    std::string DetailName() const
    {
        std::ostringstream oss;
        oss << "[name: " << mName << ", index: " << mIndex.ToString() << "]";
        return oss.str();
    }

    inline uint8_t PortNum() const
    {
        return mUBContext->mPortNumber;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
public:
    static UResult Create(const std::string &name, UBContext *ctx, const UBWorkerOptions &options,
        NetMemPoolFixedPtr memPool, NetMemPoolFixedPtr sglMemPool, UBWorker *&outWorker);

protected:
    void RunInThread();
    void DoWithBusyPolling();
    void DoWithCQEventPolling();

protected:
    std::string mName;
    UBSHcomNetWorkerIndex mIndex{};
    UBContext *mUBContext = nullptr;
    UBJfc *mUBJfc = nullptr;
    NetMemPoolFixedPtr mOpCtxMemPool = nullptr;
    NetMemPoolFixedPtr mSglCtxMemPool = nullptr;
    bool mInited = false;

    UBWorkerOptions mOptions{};

    // variable for thread
    std::thread mProgressThread;
    std::atomic_bool mProgressThreadStarted;
    int16_t mProgressCpuId = -1;
    bool mNeedStop = false;

    UBOpContextInfoPool mOpCtxInfoPool;
    UBSglContextInfoPool mSglCtxInfoPool;

    // request process related
    UBNewReqHandler mNewRequestHandler = nullptr;

    // send request posted process related
    UBPostedHandler mSendPostedHandler = nullptr;

    // one side done  related
    UBOneSideDoneHandler mOneSideDoneHandler = nullptr;

    // no request will this
    UBIdleHandler mIdleHandler = nullptr;

    uint32_t mProgressBatchSize = NN_NO4;
    
    JettyPtrMap mJettyPtrMap; ///< ID -> UBJetty* 映射表，仅出错后开始记录

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class UBJetty;

private:
    inline __attribute__((always_inline)) bool BusyPolling(urma_cr_t *wc, uint32_t &pollCount)
    {
        if (NN_UNLIKELY(mUBJfc->ProgressV(wc, pollCount) != UB_OK)) {
            return true;
        }
        return false;
    }

    inline __attribute__((always_inline)) bool CqEventPolling(urma_cr_t *wc, uint32_t &pollCount, uint32_t pollTimeOut)
    {
        if (NN_UNLIKELY(mUBJfc->EventProgressV(wc, pollCount, pollTimeOut) != UB_OK)) {
            if (mIdleHandler != nullptr) {
                mIdleHandler(mIndex);
            }
            return true;
        }
        return false;
    }

    inline __attribute__((always_inline)) void ProcessPollingResult(urma_cr_t *wc, uint32_t pollCount,
        UBJetty *&lastBrokenQp, urma_cr_status_t &lastErrorWcStatus)
    {
        for (uint32_t i = 0; i < pollCount; i++) {
            const uint32_t jettyId = wc[i].local_id;
            
            // SQE 被硬件处理时同时 modify jetty error 了
            if (wc[i].status == URMA_CR_WR_FLUSH_ERR) {
                NN_LOG_DEBUG("SQE flushed, jetty id: " << wc[i].local_id);
                continue;
            }

            // 按照先 modify jfr error, 再 modify jetty error 的顺序可以保证 FLUSH_ERR_DONE 必定为最后第一个错误，后续
            // 不会出现正常的CQE，之前所有的正常 Post 的资源统一在 FLUSH_ERR_DONE 时回收。
            // \see UBJetty::Stop()
            if (wc[i].status == URMA_CR_WR_FLUSH_ERR_DONE || wc[i].status == URMA_CR_WR_SUSPEND_DONE) {
                UBJetty *jetty = mJettyPtrMap.Lookup(jettyId);
                if (jetty == nullptr) {
                    NN_LOG_WARN("The jetty id " << jettyId << " has no associated UBJetty");
                    continue;
                }
                jetty->Cleanup();
                mJettyPtrMap.Clear(jettyId);

                // 如果在创建 EP 过程中失败，则 UBJetty 无对应 EP， 依赖 ClearJettyResource做清理。
                // \see ClearJettyResource
                auto ep = reinterpret_cast<NetUBAsyncEndpoint *>(jetty->GetUpContext());
                if (ep != nullptr) {
                    // EP 存在时，driver必定存在。
                    auto *driver = ep->GetDriver();
                    driver->ProcessEpError(jetty->GetUpContext());
                    // 从全局 EP 表中删除 EP.
                    UBSHcomNetEndpointPtr nep(ep);
                    driver->DestroyEndpoint(nep);
                }
                continue;
            }

            UBOpContextInfo *contextInfo = reinterpret_cast<UBOpContextInfo *>(wc[i].user_ctx);
            contextInfo->opResultType = UBOpContextInfo::OpResult(wc[i]);
            switch (contextInfo->ubJetty->State()) {
                case UBJettyState::READY:
                    break;
                
                // 已经处于 error 状态，需要等到 FLUSH_ERR_DONE 进行资源回收
                case UBJettyState::ERROR:
                    continue;

                case UBJettyState::RESET:
                    NN_LOG_ERROR("Unreachable: A jetty with reset state is unable to recv/send. Something went wrong.");
                    break;
            }

            CheckPollingResult(*contextInfo, wc[i], lastBrokenQp, lastErrorWcStatus);
            if (!contextInfo->HasInternalError()) {
                // detach the context
                contextInfo->ubJetty->RemoveOpCtxInfo(contextInfo);
            }

            auto ep = reinterpret_cast<NetUBAsyncEndpoint *>(contextInfo->ubJetty->GetUpContext());
            if (ep == nullptr) {
                NN_LOG_ERROR("Unreachable: A jetty received message with no EP bound");
                continue;
            }
            if (wc[i].status == URMA_CR_SUCCESS) {
                ep->UpdateTargetHbTime();
            }
            DispatchByContexetInfoType(*contextInfo, wc[i]);
        }
        /* if there is no coming request, call up idle function */
        if (mIdleHandler != nullptr && (pollCount) == 0) {
            mIdleHandler(mIndex);
        }
    }

    inline __attribute__((always_inline)) void CheckPollingResult(UBOpContextInfo &contextInfo, urma_cr_t &wc,
        UBJetty *&lastBrokenQp, urma_cr_status_t &lastErrorWcStatus)
    {
        if (NN_UNLIKELY(wc.status == URMA_CR_SUCCESS)) {
            return;
        }
        if (contextInfo.opType == UBOpContextInfo::HB_WRITE) {
            lastBrokenQp = contextInfo.ubJetty;
            NN_LOG_INFO("HB poll cq receive wcStatus " << wc.status << ", maybe remote ep " <<
                contextInfo.ubJetty->GetUpId() << " closed");
        } else if (lastBrokenQp != contextInfo.ubJetty) {
            lastBrokenQp = contextInfo.ubJetty;
            NN_LOG_ERROR("Poll cq failed in UBWorker " << DetailName() << ", wcStatus " << wc.status << ", opType " <<
                (uint32_t)(contextInfo.opType) << ", ep id = " << contextInfo.ubJetty->GetUpId() << ", context = " <<
                (uint64_t)(&contextInfo) << ", mrMemAddr = " << contextInfo.mrMemAddr);
        } else if (lastErrorWcStatus != wc.status) {
            lastErrorWcStatus = wc.status;
            NN_LOG_ERROR("Poll cq failed in UBWorker " << DetailName() << ", wc Status " << wc.status << ", opType " <<
                (uint32_t)contextInfo.opType << ", ep id = " << contextInfo.ubJetty->GetUpId() << ", context = " <<
                (uint64_t)(&contextInfo) << ", mrMemAddr = " << contextInfo.mrMemAddr);
        }
    }

    inline __attribute__((always_inline)) void DispatchByContexetInfoType(UBOpContextInfo &contextInfo, urma_cr_t &wc)
    {
        switch (contextInfo.opType) {
            case (UBOpContextInfo::OpType::SEND):
            case (UBOpContextInfo::OpType::SEND_RAW):
            case (UBOpContextInfo::OpType::SEND_RAW_SGL):
            case (UBOpContextInfo::OpType::SEND_SGL_INLINE):
                mSendPostedHandler(&contextInfo);
                break;
            case (UBOpContextInfo::OpType::RECEIVE): /* NOTE, up context is store imm data */
                (contextInfo).dataSize = wc.completion_len;
                *((int32_t *)(void *)&((contextInfo).upCtx)) = wc.imm_data;
                mNewRequestHandler(&contextInfo);
                break;
            case (UBOpContextInfo::OpType::WRITE):
            case (UBOpContextInfo::OpType::SGL_WRITE):
            case (UBOpContextInfo::OpType::HB_WRITE):
            case (UBOpContextInfo::OpType::READ):
            case (UBOpContextInfo::OpType::SGL_READ):
                mOneSideDoneHandler(&contextInfo);
                break;
            default:
                NN_LOG_ERROR("Poll cq invalid OpType " << contextInfo.opType);
        }
    }
};
} // namespace hcom
} // namespace ock

#endif
#endif // HCOM_UB_WORKER_H
