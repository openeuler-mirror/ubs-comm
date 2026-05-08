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
#ifndef OCK_HCOM_SOCK_WORKER_H_234214
#define OCK_HCOM_SOCK_WORKER_H_234214

#include <sys/resource.h>

#include "hcom.h"
#include "net_ctx_info_pool.h"
#include "net_mem_pool_fixed.h"
#include "sock_common.h"
#include "sock_wrapper.h"

namespace ock {
namespace hcom {
using SockNewReqHandler = std::function<int(SockOpContextInfo &)>;
using SockPostedHandler = std::function<int(SockOpContextInfo *)>;
using SockOneSideHandler = std::function<int(SockOpContextInfo *)>;
using SockEpCloseHandler = std::function<int(Sock *)>;

using SockOpContextInfoPool = OpContextInfoPool<SockOpContextInfo>;
using SockSglContextInfoPool = OpContextInfoPool<SockSglContextInfo>;

// when there is no request from cq, call this
using SockIdleHandler = UBSHcomNetDriverIdleHandler;

class SockWorker {
public:
    SockWorker(SockType t, const std::string &name, const UBSHcomNetWorkerIndex &index,
        const NetMemPoolFixedPtr &opCtxMemPool, const NetMemPoolFixedPtr &sglCtxMemPool,
        const NetMemPoolFixedPtr &headerReqMemPool, const SockWorkerOptions &options)
        : mType(t),
          mName(name),
          mIndex(index),
          mOpCtxMemPool(opCtxMemPool),
          mSglCtxMemPool(sglCtxMemPool),
          mHeaderReqMemPool(headerReqMemPool),
          mOptions(options)
    {
        OBJ_GC_INCREASE(SockWorker);
    }

    ~SockWorker()
    {
        UnInitialize();

        OBJ_GC_DECREASE(SockWorker);
    }

    SResult Initialize();
    void UnInitialize();

    SResult Start();
    void Stop();

    inline void ReturnResources(Sock *sock, SockOpContextInfo *ctx)
    {
        sock->ReturnQueueSpace(NN_NO1);
        mSglCtxInfoPool.Return(ctx->sendCtx);
        ctx->sendCtx = nullptr;
        mOpCtxInfoPool.Return(ctx);
        ctx = nullptr;
    }

    void ReturnResources(Sock *sock, SockOpContextInfo *ctx, SockSglContextInfo *sglCtx);

    inline const UBSHcomNetWorkerIndex &Index() const
    {
        return mIndex;
    }

    inline void SetIndex(const UBSHcomNetWorkerIndex &value)
    {
        mIndex = value;
    }

    std::string DetailName() const
    {
        std::ostringstream oss;
        oss << "[name: " << mName << ", index: " << mIndex.ToString() << "]";
        return oss.str();
    }

    inline void RegisterNewReqHandler(const SockNewReqHandler &h)
    {
        mNewRequestHandler = h;
    }

    inline void RegisterReqPostedHandler(const SockPostedHandler &h)
    {
        mSendPostedHandler = h;
    }

    inline void RegisterOneSideHandler(const SockOneSideHandler &h)
    {
        mOneSideDoneHandler = h;
    }

    inline void RegisterEpCloseHandler(const SockEpCloseHandler &h)
    {
        mEpCloseHandler = h;
    }

    inline void RegisterIdleHandler(const SockIdleHandler &h)
    {
        mIdleHandler = h;
    }

    inline const SockWorkerOptions &Options() const
    {
        return mOptions;
    }

    inline void ReturnOpContextInfo(SockOpContextInfo *&ctx)
    {
        if (NN_LIKELY(ctx != nullptr)) {
            if (NN_LIKELY(ctx->sock != nullptr)) {
                ctx->sock->DecreaseRef();
            }
            mOpCtxInfoPool.Return(ctx);
            ctx = nullptr;
        }
    }

    inline void ReturnSglContextInfo(SockSglContextInfo *&ctx)
    {
        if (NN_LIKELY(ctx != nullptr)) {
            mSglCtxInfoPool.Return(ctx);
            ctx = nullptr;
        }
    }

    inline SockOpContextInfoPool GetSockOpContextInfoPool() const
    {
        return mOpCtxInfoPool;
    }

    inline SockSglContextInfoPool GetSockSglContextInfoPool() const
    {
        return mSglCtxInfoPool;
    }

    inline SockHeaderReqInfoPool GetSockHeaderReqInfoPool() const
    {
        return mHeaderReqInfoPool;
    }

    inline SockPostedHandler GetSockPostedHandler() const
    {
        return mSendPostedHandler;
    }

    inline SockOneSideHandler GetSockOneSideHandler() const
    {
        return mOneSideDoneHandler;
    }

    SResult PostSend(Sock *sock, SockTransHeader &header, const UBSHcomNetTransRequest &req);
    SResult PostSendRawSgl(Sock *sock, SockTransHeader &header, const UBSHcomNetTransSglRequest &req);
    SResult PostRead(Sock *sock, SockTransHeader &header, const UBSHcomNetTransRequest &request);
    SResult PostRead(Sock *sock, SockTransHeader &header, const UBSHcomNetTransSglRequest &request);
    SResult PostWrite(Sock *sock, SockTransHeader &header, const UBSHcomNetTransRequest &request);
    SResult PostWrite(Sock *sock, SockTransHeader &header, const UBSHcomNetTransSglRequest &request);
    SResult PostSendNoCpy(Sock *sock, SockTransHeader &header, const UBSHcomNetTransRequest &req);

#define SET_EPOLL_EVENT(selfSock, events, evNewFd) \
    do {                                           \
        (evNewFd).data.ptr = selfSock;             \
        (evNewFd).events = events;                 \
    } while (0)

    inline SResult AddToEpoll(Sock *sock, uint32_t events)
    {
        NN_ASSERT_LOG_RETURN(sock != nullptr, SS_PARAM_INVALID)

        if (sock->FD() == INVALID_FD) {
            return SS_PARAM_INVALID;
        }

        struct epoll_event evNewFd {};
        SET_EPOLL_EVENT(sock, events, evNewFd);
        NN_LOG_TRACE_INFO("Adding sock " << sock->Id() << " address " << sock << " fd " << sock->FD() <<
            " into sock worker " << mName);

        if (NN_UNLIKELY(epoll_ctl(mEpollHandle, EPOLL_CTL_ADD, sock->FD(), &evNewFd) != 0)) {
            NN_LOG_ERROR("Failed to add fd " << sock->FD() << " into epoll for sock worker " << mName <<
                ", errno " << errno);
            return SS_SOCK_EPOLL_OP_FAILED;
        }

        sock->IncreaseRef();
        return SS_OK;
    }

    inline SResult ModifyInEpoll(Sock *sock, uint32_t events)
    {
        NN_ASSERT_LOG_RETURN(sock != nullptr, SS_PARAM_INVALID)

        if (sock->FD() == INVALID_FD) {
            return SS_PARAM_INVALID;
        }

        NN_LOG_TRACE_INFO("Modifying sock " << sock->Id() << " fd " << sock->FD() << " in sock worker " << mName);

        struct epoll_event evNewFd {};
        SET_EPOLL_EVENT(sock, events, evNewFd);

        if (NN_UNLIKELY(epoll_ctl(mEpollHandle, EPOLL_CTL_MOD, sock->FD(), &evNewFd) != 0)) {
            if (errno == ENOENT) {
                NN_LOG_ERROR("fd in epoll for worker " << mName << " is not found or has been removed from epoll");
                return SS_SOCK_EPOLL_OP_FAILED;
            }
            char errBuf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to modify fd in epoll for sock worker " << mName << ", errno:" << errno << " error:" <<
                NetFunc::NN_GetStrError(errno, errBuf, NET_STR_ERROR_BUF_SIZE));
            return SS_SOCK_EPOLL_OP_FAILED;
        }

        return SS_OK;
    }

    inline SResult RemoveFromEpoll(Sock *sock)
    {
        NN_ASSERT_LOG_RETURN(sock != nullptr, SS_PARAM_INVALID)

        if (sock->FD() == INVALID_FD) {
            return SS_PARAM_INVALID;
        }

        NN_LOG_TRACE_INFO("Deleting sock " << sock->Id() << " fd " << sock->FD() << " from sock worker " << mName);

        if (NN_UNLIKELY(epoll_ctl(mEpollHandle, EPOLL_CTL_DEL, sock->FD(), nullptr) != 0)) {
            if (errno == ENOENT) {
                NN_LOG_ERROR("Sock " << sock->Id() << " fd " << sock->FD() <<
                    " has been removed from epoll in worker " << mName);
                return SS_OK;
            }
            NN_LOG_ERROR("Failed to remove from epoll for sock worker " << mName << ", errno " << errno);
            return SS_SOCK_EPOLL_OP_FAILED;
        }

        sock->DecreaseRef();
        return SS_OK;
    }

    void EpCloseByUser(Sock *sock);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    SResult Validate();
    void RunInThread(int16_t cpuId);
    void UnInitializeInner();
    SResult HandleReceiveCtx(SockOpContextInfo &opCtx);
    SResult PostReadAck(SockOpContextInfo &opCtx);
    SResult PostReadAckHandle(SockOpContextInfo &opCtx);
    SResult PostWriteAck(SockOpContextInfo &opCtx);
    SResult PostWriteAckHandle(SockOpContextInfo &opCtx);
    SResult PostWriteSglAck(SockOpContextInfo &opCtx);
    SResult PostWriteSglAckHandle(SockOpContextInfo &opCtx);
    SResult PostReadSglAck(SockOpContextInfo &opCtx);
    SResult PostReadSglAckHandle(SockOpContextInfo &opCtx);
    SResult GenerateReadSglAckOpCtxInfo(SockOpContextInfo *&opCtxInfo, SockOpContextInfo &opCtx,
        UBSHcomNetTransSgeIov *&rawIov, uint16_t iovCount, uint32_t dataSize);
    SResult GenerateWriteSglAckOpCtxInfo(SockOpContextInfo *&opCtxInfo, SockOpContextInfo &opCtx);
    inline SResult CheckIovLen(SockOpContextInfo &opCtx, uint16_t &iovCount);
    SResult GenerateWriteAckOpCtxInfo(SockOpContextInfo *&opCtxInfo, SockOpContextInfo &opCtx);
    SResult InitContextInfoPool();
    __always_inline void BindCpuSetPthreadName(int16_t cpuId)
    {
        // set cpu id
        cpu_set_t cpuSet;
        if (cpuId != -1) {
            CPU_ZERO(&cpuSet);
            CPU_SET(cpuId, &cpuSet);
            if (pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet) != 0) {
                NN_LOG_WARN("Unable to bind sock worker " << mName << mIndex.ToString() << " to cpu " << cpuId);
            }
        }
        // set thread name
        std::string workerName = mType == SOCK_TCP ? "SockWkr" : "UDSWkr";
        workerName += mIndex.ToString();
        pthread_setname_np(pthread_self(), workerName.c_str());
        NN_LOG_INFO("SockWorker [name: " << mName << ", index: " << mIndex.ToString() << ", cpuId: " << cpuId <<
            ", more " << mOptions.ToShortString() << "] working thread started");
    }

private:
    SockType mType = SOCK_TCP;
    std::string mName;
    std::mutex mMutex;
    UBSHcomNetWorkerIndex mIndex {};
    bool mInited = false;
    NetMemPoolFixedPtr mOpCtxMemPool = nullptr;
    NetMemPoolFixedPtr mSglCtxMemPool = nullptr;
    NetMemPoolFixedPtr mHeaderReqMemPool = nullptr;

    SockWorkerOptions mOptions {};

    /* variable for thread */
    std::thread mProgressThr;                       /* thread object of progress */
    bool mStarted = false;                          /* thread already started or not */
    std::atomic_bool mProgressThrStarted { false }; /* started flag */
    volatile bool mNeedToStop = false;              /* flag to be stopped */

    SockNewReqHandler mNewRequestHandler = nullptr;   /* request process related */
    SockPostedHandler mSendPostedHandler = nullptr;   /* send request posted process related */
    SockOneSideHandler mOneSideDoneHandler = nullptr; /* one side done will call this */
    SockEpCloseHandler mEpCloseHandler = nullptr;     /* ep closing will call this */
    SockIdleHandler mIdleHandler = nullptr;           /* no request will call this */

    int mEpollHandle = -1; /* event polling handle */
    SockOpContextInfoPool mOpCtxInfoPool;
    SockSglContextInfoPool mSglCtxInfoPool;
    SockHeaderReqInfoPool mHeaderReqInfoPool;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif // OCK_HCOM_SOCK_WORKER_H_234214
