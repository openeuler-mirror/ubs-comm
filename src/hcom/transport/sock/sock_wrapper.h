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
#ifndef OCK_HCOM_SOCK_WRAPPER_H_234234
#define OCK_HCOM_SOCK_WRAPPER_H_234234

#include <sys/ioctl.h>
#include <linux/sockios.h>

#include "net_common.h"
#include "net_ctx_info_pool.h"
#include "net_memory_region.h"
#include "net_oob.h"
#include "net_oob_ssl.h"
#include "net_security_rand.h"
#include "openssl_api_wrapper.h"
#include "securec.h"
#include "sock_common.h"
#include "net_oob_openssl.h"
#include "sock_buff.h"

namespace ock {
namespace hcom {
struct SockReceiveState {
    uint16_t headerLen = sizeof(SockTransHeader);
    uint16_t headerToBeReceived = 0;
    ssize_t bodyToBeReceived = -1;

    inline bool ShouldReceiveHeader() const
    {
        return bodyToBeReceived == -1;
    }

    inline uint16_t ReceivedHeaderLen() const
    {
        return headerLen - headerToBeReceived;
    }

    inline void ResetHeader()
    {
        headerToBeReceived = sizeof(SockTransHeader);
        bodyToBeReceived = -1;
    }

    inline bool BodySatisfied(ssize_t newReceivedSize)
    {
        bodyToBeReceived -= newReceivedSize;
        return bodyToBeReceived == 0;
    }

    inline bool HeaderSatisfied(uint16_t newReceivedHeader)
    {
        headerToBeReceived -= newReceivedHeader;
        return headerToBeReceived == 0;
    }
} __attribute__((packed));

struct SendingQueueRequest {
    uint64_t remainSize = 0;
    struct iovec iov[NN_NO7] {};
    uint16_t iovCount = 0;
    bool isTwoSideMode = false;
};

/* ***************************************************************************************************** */
/*
 * @brief sock buffer list for send pending
 */
class SockBuffList {
public:
private:
};

/* ***************************************************************************************************** */
/*
 * @brief sock wrapper
 */
using SockOpContextInfoPool = OpContextInfoPool<SockOpContextInfo>;
using SockSglContextInfoPool = OpContextInfoPool<SockSglContextInfo>;
using SockHeaderReqInfoPool = OpContextInfoPool<SockHeaderReqInfo>;
using SockPostedHandler = std::function<int(SockOpContextInfo *)>;
using SockOneSideHandler = std::function<int(SockOpContextInfo *)>;
class Sock {
public:
    inline void BufferStatus() const
    {
        int buffer = 0;
        socklen_t buffLen = sizeof buffer;
        getsockopt(mFd, SOL_SOCKET, SO_SNDBUF, &buffer, &buffLen);
        NN_LOG_INFO("send buffer size in total:" << buffer);
        ioctl(mFd, SIOCOUTQ, &buffer);
        NN_LOG_INFO("send buffer size in using:" << buffer);
        getsockopt(mFd, SOL_SOCKET, SO_RCVBUF, &buffer, &buffLen);
        NN_LOG_INFO("receive buffer size in total:" << buffer);
        ioctl(mFd, SIOCINQ, &buffer);
        NN_LOG_INFO("receive buffer size in using:" << buffer);
    }

    static inline SResult SendRealConnHeader(int fd, void *buf, uint32_t size)
    {
        if (NN_UNLIKELY(fd == -1 || buf == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (NN_UNLIKELY(::send(fd, buf, size, 0) <= 0)) {
            NN_LOG_ERROR("Failed to send real connection header, with errno is " << errno);
            return SS_SOCK_SEND_FAILED;
        }

        return SS_OK;
    }

    inline SResult Send(const void *buf, uint32_t size)
    {
        if (NN_UNLIKELY(mFd == -1 || buf == nullptr)) {
            return SS_PARAM_INVALID;
        }

        ssize_t result = 0;

        if (mEnableTls) {
            uint32_t writeLen = 0;
            return SSLSend(buf, size, writeLen);
        } else {
            result = ::send(mFd, buf, size, 0);
            if (result <= 0) {
                NN_LOG_ERROR("Failed to send data, ret: " << result << ", errno: " << errno);
                return errno;
            }
        }

        if (NN_UNLIKELY(result != size)) {
            NN_LOG_ERROR("Failed to send data, expected size: " << size << ", actual size: " << result);
            return SS_SOCK_DATA_SIZE_UN_MATCHED;
        }

        return SS_OK;
    }

    inline SResult Receive(void *&buf, uint32_t size)
    {
        if (NN_UNLIKELY(mFd == -1 || buf == nullptr)) {
            return SS_PARAM_INVALID;
        }

        ssize_t result = 0;

        if (mEnableTls) {
            return SSLRead(buf, size, reinterpret_cast<uint32_t &>(result));
        } else {
            result = ::recv(mFd, buf, size, 0);
            if (result <= 0) {
                NN_LOG_ERROR("Failed to recv data, ret: " << result << ", errno: " << errno);
                return errno;
            }
        }

        if (NN_UNLIKELY(result != size)) {
            NN_LOG_ERROR("Failed to recv data, expected size: " << size << ", actual size: " << result);
            return SS_SOCK_DATA_SIZE_UN_MATCHED;
        }

        return SS_OK;
    }

public:
    Sock(SockType type, const std::string &name, uint64_t id, int fd, SockOptions &options)
        : mFd(fd),
          mQueueVacantSize(options.sendQueueSize),
          mQueueSize(options.sendQueueSize),
          mName(name),
          mId(id),
          mType(type),
          mSendQueue(options.sendQueueSize)
    {
        mEnableTls = false;
        OBJ_GC_INCREASE(Sock);
    }

    Sock(SockType type, const std::string &name, uint64_t id, int fd, SockOptions &options, OOBTCPConnection *conn)
        : mFd(fd),
          mQueueVacantSize(options.sendQueueSize),
          mQueueSize(options.sendQueueSize),
          mName(name),
          mId(id),
          mType(type),
          mSendQueue(options.sendQueueSize)
    {
        mEnableTls = true;
        mSsl = reinterpret_cast<OOBOpenSSLConnection *>(conn)->TransferSsl();

        OBJ_GC_INCREASE(Sock);
    }

    ~Sock()
    {
        UnInitialize();

        OBJ_GC_DECREASE(Sock);
    }

    SResult Initialize(const SockWorkerOptions &workerOptions);
    void UnInitialize();
    void Close();
    SResult SetBlockingSendTimeout(int32_t sendTimeout);
    SResult SetBlockingIo(UBSHcomEpOptions &epOptions);
    uint32_t GetSendQueueCount();

    /*
     * @brief Get name
     */
    inline const std::string &Name() const
    {
        return mName;
    }

    /*
     * @brief Get ip and port of peer
     */
    inline const std::string &PeerIpPort() const
    {
        return mPeerIpPort;
    }

    /*
     * @brief Get sock id
     */
    inline uint64_t Id() const
    {
        return mId;
    }

    /*
     * @brief Set a context by caller
     */
    inline void UpContext(uint64_t value)
    {
        mUpCtx = value;
    }

    /*
     * @brief Get a context by caller
     */
    inline uint64_t UpContext() const
    {
        return mUpCtx;
    }

    /*
     * @brief Set a context by caller
     */
    inline void UpContext1(uint64_t value)
    {
        mUpCtx1 = value;
    }

    /*
     * @brief Get a context by caller
     */
    inline uint64_t UpContext1() const
    {
        return mUpCtx1;
    }

    /*
     * @brief Get a secret by caller
     */
    inline NetSecrets &Secret()
    {
        return mSecret;
    }

    /*
     * @brief Set a secret by caller
     */
    inline void Secret(NetSecrets &secret)
    {
        mSecret = secret;
    }

    /*
     * @brief Get receive data
     */
    inline SockBuff &ReceiveData()
    {
        return mReceiveBuff;
    }

    /*
     * @brief Get the file description of socket
     */
    inline int FD() const
    {
        return mFd;
    }

    inline bool CbByWorkerInBlocking() const
    {
        return mCbByWorkerInBlocking;
    }

    inline void SetCbByWorkerInBlocking(bool cbByWorkerInBlocking)
    {
        mCbByWorkerInBlocking = cbByWorkerInBlocking;
    }

    inline uint32_t OneSideNextSeq()
    {
        return __sync_fetch_and_add(&mSeqIndex, 1);
    }

    inline void AddOpCtx(uint32_t id, SockOpContextInfo *opCtx)
    {
        std::lock_guard<std::mutex> guard(mCtxMutex);

        mCtxMap.emplace(id, opCtx);
    }

    inline SockOpContextInfo *RemoveOpCtx(uint32_t id)
    {
        std::lock_guard<std::mutex> guard(mCtxMutex);

        auto iter = mCtxMap.find(id);
        if (NN_UNLIKELY(iter == mCtxMap.end())) {
            return nullptr;
        }
        SockOpContextInfo *ctxInfo = iter->second;
        mCtxMap.erase(iter);
        return ctxInfo;
    }
    /*
     * @brief Get header address
     */
    inline SockTransHeader *GetHeaderAddress()
    {
        return &mHeader;
    }

#define COMPOSE_REQUEST(mSendingQueueRequest, reqInQueue)                                                             \
    if ((mSendingQueueRequest).remainSize == 0) {                                                                     \
        if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SEND ||                                         \
            (reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW) {                                     \
            uint32_t reqSize = 0;                                                                                     \
            if (mOptions.sendZCopy) {                                                                                 \
                (mSendingQueueRequest).iov[NN_NO0].iov_base = &(reqInQueue)->headerRequest->sendHeader;               \
                (mSendingQueueRequest).iov[NN_NO1].iov_base = (reqInQueue)->headerRequest->request;                   \
                reqSize = (reqInQueue)->headerRequest->sendHeader.dataLength;                                         \
            } else {                                                                                                  \
                (mSendingQueueRequest).iov[NN_NO0].iov_base = (reqInQueue)->sendBuff;                                 \
                (mSendingQueueRequest).iov[NN_NO1].iov_base = reinterpret_cast<void *>(                               \
                    reinterpret_cast<char *>((reqInQueue)->sendBuff) + sizeof(SockTransHeader));                      \
                reqSize = reinterpret_cast<SockTransHeader *>((reqInQueue)->sendBuff)->dataLength;                    \
            }                                                                                                         \
            (mSendingQueueRequest).iov[NN_NO0].iov_len = sizeof(SockTransHeader);                                     \
            (mSendingQueueRequest).iov[NN_NO1].iov_len = reqSize;                                                     \
            (mSendingQueueRequest).iovCount = NN_NO2;                                                                 \
            (mSendingQueueRequest).remainSize = reqSize + sizeof(SockTransHeader);                                    \
        } else if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW_SGL ||                          \
            (reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_READ_ACK) {                                     \
            auto sendCtx = (reqInQueue)->sendCtx;                                                                     \
            (mSendingQueueRequest).iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);             \
            (mSendingQueueRequest).iov[NN_NO0].iov_len = sizeof(SockTransHeader);                                     \
            for (uint16_t i = 0; i < sendCtx->iovCount; i++) {                                                        \
                (mSendingQueueRequest).iov[i + NN_NO1].iov_base = reinterpret_cast<void *>(sendCtx->iov[i].lAddress); \
                (mSendingQueueRequest).iov[i + NN_NO1].iov_len = sendCtx->iov[i].size;                                \
            }                                                                                                         \
            (mSendingQueueRequest).iovCount = sendCtx->iovCount + NN_NO1;                                             \
            (mSendingQueueRequest).remainSize = sendCtx->sendHeader.dataLength + sizeof(SockTransHeader);             \
        } else if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_WRITE) {                                 \
            auto sendCtx = (reqInQueue)->sendCtx;                                                                     \
            (mSendingQueueRequest).iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);             \
            (mSendingQueueRequest).iov[NN_NO0].iov_len = sizeof(SockTransHeader);                                     \
            (mSendingQueueRequest).iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iov[0]);                 \
            (mSendingQueueRequest).iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSgeIov);                               \
            (mSendingQueueRequest).iov[NN_NO2].iov_base = reinterpret_cast<void *>(sendCtx->iov[0].lAddress);         \
            (mSendingQueueRequest).iov[NN_NO2].iov_len = sendCtx->iov[0].size;                                        \
            (mSendingQueueRequest).iovCount = NN_NO3;                                                                 \
            (mSendingQueueRequest).remainSize =                                                                       \
                sizeof(SockTransHeader) + sizeof(UBSHcomNetTransSgeIov) + sendCtx->iov[0].size;                       \
        } else if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_READ) {                                  \
            auto sendCtx = (reqInQueue)->sendCtx;                                                                     \
            (mSendingQueueRequest).iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);             \
            (mSendingQueueRequest).iov[NN_NO0].iov_len = sizeof(SockTransHeader);                                     \
            (mSendingQueueRequest).iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iov[0]);                 \
            (mSendingQueueRequest).iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSgeIov);                               \
            (mSendingQueueRequest).iovCount = NN_NO2;                                                                 \
            (mSendingQueueRequest).remainSize = sizeof(SockTransHeader) + sizeof(UBSHcomNetTransSgeIov);              \
        } else if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_WRITE_ACK ||                             \
            (reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SGL_WRITE_ACK) {                                \
            (mSendingQueueRequest).iov[NN_NO0].iov_base =                                                             \
                reinterpret_cast<void *>(&(reqInQueue)->sendCtx->sendHeader);                                         \
            (mSendingQueueRequest).iov[NN_NO0].iov_len = sizeof(SockTransHeader);                                     \
            (mSendingQueueRequest).iovCount = NN_NO1;                                                                 \
            (mSendingQueueRequest).remainSize = sizeof(SockTransHeader);                                              \
        } else if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SGL_READ) {                              \
            auto sendCtx = (reqInQueue)->sendCtx;                                                                     \
            (mSendingQueueRequest).iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);             \
            (mSendingQueueRequest).iov[NN_NO0].iov_len = sizeof(SockTransHeader);                                     \
            (mSendingQueueRequest).iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iovCount);               \
            (mSendingQueueRequest).iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSglRequest::iovCount);                 \
            (mSendingQueueRequest).iov[NN_NO2].iov_base = reinterpret_cast<void *>(sendCtx->iov);                     \
            (mSendingQueueRequest).iov[NN_NO2].iov_len = sizeof(UBSHcomNetTransSgeIov) * sendCtx->iovCount;           \
            (mSendingQueueRequest).iovCount = NN_NO3;                                                                 \
            (mSendingQueueRequest).remainSize = sendCtx->sendHeader.dataLength + sizeof(SockTransHeader);             \
        } else if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SGL_WRITE ||                             \
            (reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SGL_READ_ACK) {                                 \
            auto sendCtx = (reqInQueue)->sendCtx;                                                                     \
            (mSendingQueueRequest).iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);             \
            (mSendingQueueRequest).iov[NN_NO0].iov_len = sizeof(SockTransHeader);                                     \
            (mSendingQueueRequest).iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iovCount);               \
            (mSendingQueueRequest).iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSglRequest::iovCount);                 \
            (mSendingQueueRequest).iov[NN_NO2].iov_base = reinterpret_cast<void *>(sendCtx->iov);                     \
            (mSendingQueueRequest).iov[NN_NO2].iov_len = sizeof(UBSHcomNetTransSgeIov) * sendCtx->iovCount;           \
            for (uint16_t i = 0; i < sendCtx->iovCount; i++) {                                                        \
                if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SGL_WRITE) {                            \
                    (mSendingQueueRequest).iov[i + NN_NO3].iov_base =                                                 \
                        reinterpret_cast<void *>(sendCtx->iov[i].lAddress);                                           \
                } else {                                                                                              \
                    (mSendingQueueRequest).iov[i + NN_NO3].iov_base =                                                 \
                        reinterpret_cast<void *>(sendCtx->iov[i].rAddress);                                           \
                }                                                                                                     \
                (mSendingQueueRequest).iov[i + NN_NO3].iov_len = sendCtx->iov[i].size;                                \
            }                                                                                                         \
            (mSendingQueueRequest).iovCount = NN_NO3 + sendCtx->iovCount;                                             \
            (mSendingQueueRequest).remainSize = sendCtx->sendHeader.dataLength + sizeof(SockTransHeader);             \
        }                                                                                                             \
                                                                                                                      \
        if ((reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SEND ||                                         \
            (reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW ||                                     \
            (reqInQueue)->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW_SGL) {                                 \
            (mSendingQueueRequest).isTwoSideMode = true;                                                              \
        } else {                                                                                                      \
            (mSendingQueueRequest).isTwoSideMode = false;                                                             \
        }                                                                                                             \
    }

#define PROCESS_REQUEST(mSendingQueueRequest, reqInQueue)                                                             \
    do {                                                                                                              \
        std::lock_guard<std::mutex> guard(mIoMutex);                                                                  \
        ssize_t result = 0;                                                                                           \
        if ((mSendingQueueRequest).isTwoSideMode && mEnableTls) {                                                     \
            auto iov = (mSendingQueueRequest).iov;                                                                    \
            for (uint16_t i = 0; i < (mSendingQueueRequest).iovCount; i++) {                                          \
                if (iov[i].iov_len == 0) {                                                                            \
                    continue;                                                                                         \
                }                                                                                                     \
                int ret = 0;                                                                                          \
                SResult writeRet = SS_OK;                                                                             \
                if (i == NN_NO0) {                                                                                    \
                    ret = writev(mFd, &iov[i], NN_NO1);                                                               \
                } else {                                                                                              \
                    writeRet = SSLSend(iov[i].iov_base, iov[i].iov_len, reinterpret_cast<uint32_t &>(ret));           \
                }                                                                                                     \
                if (ret <= 0 || writeRet != SS_OK) {                                                                  \
                    if (errno != ECONNRESET && errno != EAGAIN) {                                                     \
                        NN_LOG_ERROR("Failed to send msg with tls to peer in sock " << mId << " name " << mName <<    \
                            " result:" << result << " iov_len:" << iov[i].iov_len);                                   \
                        return SS_SOCK_SEND_FAILED;                                                                   \
                    }                                                                                                 \
                    break;                                                                                            \
                }                                                                                                     \
                result += ret;                                                                                        \
                if (static_cast<size_t>(ret) != iov[i].iov_len) {                                                     \
                    break;                                                                                            \
                }                                                                                                     \
            }                                                                                                         \
        } else {                                                                                                      \
            result = writev(mFd, reinterpret_cast<const struct iovec *>(&(mSendingQueueRequest).iov),                 \
                (mSendingQueueRequest).iovCount);                                                                     \
        }                                                                                                             \
        if (result <= 0) {                                                                                            \
            if (errno == ECONNRESET) {                                                                                \
                NN_LOG_ERROR("Failed to send msg to peer in sock " << mId << " name " << mName << ", reset by peer, " \
                                                                   << " result:" << result);                          \
                return SS_RESET_BY_PEER;                                                                              \
            }                                                                                                         \
            if (errno == EAGAIN) {                                                                                    \
                /* send buff is full not send */                                                                      \
                return SS_SOCK_SEND_EAGAIN;                                                                           \
            }                                                                                                         \
            NN_LOG_ERROR("Failed to send msg to peer in sock " << mId << " name " << mName << ", errno " << errno <<  \
                " error code:" << errno << " result:" << result);                                                     \
            return SS_SOCK_SEND_FAILED;                                                                               \
        }                                                                                                             \
                                                                                                                      \
        NN_LOG_TRACE_INFO("Receive sock " << Id() << " event EPOLLOUT,"                                               \
                                          << "queue size:" << mSendQueue.Size() << " deque and write result: " <<     \
            result << " req size:" << (mSendingQueueRequest).remainSize << " max send size:" << maxSendSize);         \
        if (static_cast<uint64_t>(result) < (mSendingQueueRequest).remainSize) {                                      \
            for (uint32_t i = 0; i < (mSendingQueueRequest).iovCount; i++) {                                          \
                auto iovLen = static_cast<ssize_t>((mSendingQueueRequest).iov[i].iov_len);                            \
                if (result < iovLen) {                                                                                \
                    (mSendingQueueRequest).iov[i].iov_base =                                                          \
                        reinterpret_cast<char *>((mSendingQueueRequest).iov[i].iov_base) + result;                    \
                    (mSendingQueueRequest).iov[i].iov_len -= static_cast<size_t>(result);                             \
                    (mSendingQueueRequest).remainSize -= static_cast<uint64_t>(result);                               \
                    break;                                                                                            \
                } else if (result > iovLen) {                                                                         \
                    result -= iovLen;                                                                                 \
                    (mSendingQueueRequest).remainSize -= static_cast<uint64_t>(iovLen);                               \
                    (mSendingQueueRequest).iov[i].iov_len = 0;                                                        \
                } else {                                                                                              \
                    (mSendingQueueRequest).remainSize -= static_cast<uint64_t>(iovLen);                               \
                    (mSendingQueueRequest).iov[i].iov_len = 0;                                                        \
                    break;                                                                                            \
                }                                                                                                     \
            }                                                                                                         \
            return SS_SOCK_SEND_EAGAIN;                                                                               \
        } else {                                                                                                      \
            (mSendingQueueRequest).remainSize = 0;                                                                    \
        }                                                                                                             \
    } while (0)

#define POST_PROCESS(popReq)                                                       \
    do {                                                                           \
        ReturnQueueSpace(NN_NO1);                                                  \
        if ((popReq)->opType == SockOpContextInfo::SockOpType::SS_SEND ||          \
            (popReq)->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW ||      \
            (popReq)->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW_SGL) {  \
            mSendPostedHandler((popReq));                                          \
        }                                                                          \
                                                                                   \
        if ((popReq)->opType == SockOpContextInfo::SockOpType::SS_WRITE_ACK ||     \
            (popReq)->opType == SockOpContextInfo::SockOpType::SS_READ_ACK ||      \
            (popReq)->opType == SockOpContextInfo::SockOpType::SS_SGL_WRITE_ACK || \
            (popReq)->opType == SockOpContextInfo::SockOpType::SS_SGL_READ_ACK) {  \
            mSglCtxInfoPool.Return((popReq)->sendCtx);                             \
            (popReq)->sendCtx = nullptr;                                           \
            mOpCtxInfoPool.Return((popReq));                                       \
            (popReq) = nullptr;                                                    \
        }                                                                          \
    } while (0)

    inline void DealCbWithFailure()
    {
        SockOpContextInfo *popReq = {};
        while (mSendQueue.GetFront(popReq)) {
            if (!mSendQueue.PopFront(popReq)) {
                break;
            }
            popReq->errType = SockOpContextInfo::SS_OPERATE_FAILURE;
            POST_PROCESS(popReq);
        }

        for (auto &it : mCtxMap) {
            it.second->errType = SockOpContextInfo::SS_OPERATE_FAILURE;
            mOneSideDoneHandler(it.second);
        }
        mCtxMap.clear();
    }

    inline SResult ProcessQueueReq()
    {
        int64_t maxSendSize = static_cast<int64_t>(mOptions.sendBufSizeKB) * NN_NO1024;
        bool isGetSuccess = true;

        while (maxSendSize > 0) {
            SockOpContextInfo *reqInQueue = nullptr;
            isGetSuccess = mSendQueue.GetFront(reqInQueue);
            if (!isGetSuccess) {
                return SS_OK;
            }

            if (!reqInQueue->isSent) {
                COMPOSE_REQUEST(mSendingQueueRequest, reqInQueue);

                auto sentSize = mSendingQueueRequest.remainSize;

                PROCESS_REQUEST(mSendingQueueRequest, reqInQueue);

                maxSendSize -= static_cast<int64_t>(sentSize);
            }
            SockOpContextInfo *popReq = {};
            isGetSuccess = mSendQueue.PopFront(popReq);
            if (!isGetSuccess) {
                return SS_OK;
            }
            POST_PROCESS(popReq);
        }

        return SS_SOCK_SEND_EAGAIN;
    }

#define POST_SEND(iov, requestSize, seqNo)                                                                         \
    do {                                                                                                           \
        ssize_t ret = 0;                                                                                           \
        if (!mEnableTls) {                                                                                         \
            if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&(iov)), NN_NO2)) <                      \
                static_cast<ssize_t>((requestSize) + sizeof(SockTransHeader))) {                                   \
                if (ret == 0) {                                                                                    \
                    return SS_TCP_RETRY;                                                                           \
                }                                                                                                  \
                if (errno == 0) {                                                                                  \
                    NN_LOG_ERROR("Failed to PostSend to peer in sock " << mId << " name " << mName << " with " <<  \
                        mSendTimeoutSecond << " second timeout, " << ret << " is sent");                           \
                    return SS_TIMEOUT;                                                                             \
                }                                                                                                  \
                NN_LOG_ERROR("Failed to PostSend to peer in sock " << mId << " name " << mName << ", errno " <<    \
                    errno << ", seqNo " << (seqNo));                                                               \
                return SS_SOCK_SEND_FAILED;                                                                        \
            }                                                                                                      \
        } else {                                                                                                   \
            if ((ret = writev(mFd, &(iov)[NN_NO0], NN_NO1)) < static_cast<ssize_t>(sizeof(SockTransHeader))) {     \
                if (ret == 0) {                                                                                    \
                    return SS_TCP_RETRY;                                                                           \
                }                                                                                                  \
                if (errno == 0) {                                                                                  \
                    NN_LOG_ERROR("(TLS)Failed to PostSend header to peer in sock " << mId << " name " << mName <<  \
                        " with " << mSendTimeoutSecond << " second timeout, " << ret << " is sent");               \
                    return SS_TIMEOUT;                                                                             \
                }                                                                                                  \
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};                                                          \
                NN_LOG_ERROR("(TLS)Failed to PostSend header to peer in sock " << mId << " name " << mName <<      \
                    ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) << ", seqNo " <<     \
                    (seqNo));                                                                                      \
                return SS_SOCK_SEND_FAILED;                                                                        \
            }                                                                                                      \
            uint32_t writeLen = 0;                                                                                 \
            ret = SSLSend((iov)[NN_NO1].iov_base, (iov)[NN_NO1].iov_len, writeLen);                                \
            if (ret != SS_OK || writeLen != (iov)[NN_NO1].iov_len) {                                               \
                if (ret == SS_TIMEOUT) {                                                                           \
                    NN_LOG_ERROR("(TLS)Failed to PostSendSgl body to peer in sock " << mId << " name " << mName << \
                        ", error is timeout with " << mSendTimeoutSecond << " second, seqNo " << (seqNo) <<        \
                        ", the failed iov data len " << (iov)[NN_NO1].iov_len);                                    \
                    return SS_TIMEOUT;                                                                             \
                }                                                                                                  \
                NN_LOG_ERROR("(TLS)Failed to PostSend body to peer in sock " << mId << " name " << mName <<        \
                    ", seqNo " << (seqNo) << ", the failed iov data len " << (iov)[NN_NO1].iov_len);               \
                return SS_SOCK_SEND_FAILED;                                                                        \
            }                                                                                                      \
        }                                                                                                          \
    } while (0)

    inline SResult PostSend(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            struct iovec iov[NN_NO2];
            uint32_t requestSize = 0;
            if (mOptions.sendZCopy) {
                iov[NN_NO0].iov_base = &ctx->headerRequest->sendHeader;
                iov[NN_NO0].iov_len = sizeof(SockTransHeader);
                iov[NN_NO1].iov_base = ctx->headerRequest->request;
                requestSize = reinterpret_cast<SockTransHeader *>(&ctx->headerRequest->sendHeader)->dataLength;
                iov[NN_NO1].iov_len = requestSize;
            } else {
                iov[NN_NO0].iov_base = ctx->sendBuff;
                iov[NN_NO0].iov_len = sizeof(SockTransHeader);
                iov[NN_NO1].iov_base =
                    reinterpret_cast<void *>(reinterpret_cast<char *>(ctx->sendBuff) + sizeof(SockTransHeader));
                requestSize = reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->dataLength;
                iov[NN_NO1].iov_len = requestSize;
            }
            std::lock_guard<std::mutex> guard(mIoMutex);
            POST_SEND(iov, requestSize, reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->seqNo);

            if (mCbByWorkerInBlocking) {
                ctx->isSent = true;
                mSendQueue.PushBack(ctx);
                return SS_SOCK_SEND_EAGAIN;
            }
            NN_LOG_TRACE_INFO("Post send request successfully : sock " << mId << ", head imm data " <<
                reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->immData << ", flags " <<
                reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->flags << ", seqNo " <<
                reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->seqNo << ", data len " <<
                reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->dataLength);
            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

    inline SResult PostSend(SockTransHeader &header, const UBSHcomNetTransRequest &req)
    {
        struct iovec iov[NN_NO2];
        iov[NN_NO0].iov_base = reinterpret_cast<void *>(&header);
        iov[NN_NO0].iov_len = sizeof(SockTransHeader);
        iov[NN_NO1].iov_base = reinterpret_cast<void *>(req.lAddress);
        iov[NN_NO1].iov_len = req.size;

        std::lock_guard<std::mutex> guard(mIoMutex);
        POST_SEND(iov, req.size, header.seqNo);

        NN_LOG_TRACE_INFO("PostSend request successfully : sock " << mId << ", head imm data " << header.immData <<
            ", flags " << header.flags << ", seqNo " << header.seqNo << ", data len " << header.dataLength);
        return SS_OK;
    }

    inline SResult PostSendSglSsl(SockOpContextInfo *ctx, struct iovec *iov, uint32_t iovLen = NN_NO5)
    {
        auto sendCtx = ctx->sendCtx;
        ssize_t ret = 0;
        if ((ret = writev(mFd, &iov[NN_NO0], NN_NO1)) < static_cast<ssize_t>(sizeof(SockTransHeader))) {
            if (ret == 0) {
                return SS_TCP_RETRY;
            }
            if (errno == 0) {
                NN_LOG_ERROR("(TLS)Failed to PostSendSgl header to peer in sock " << mId << " name " << mName <<
                    " with " << mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                return SS_TIMEOUT;
            }

            NN_LOG_ERROR("(TLS)Failed to PostSendSgl header to peer in sock " << mId << " name " << mName <<
                ", errno " << errno << ", seqNo " << reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->seqNo);
            return SS_SOCK_SEND_FAILED;
        }

        for (uint32_t i = 1; i < NN_NO1 + sendCtx->iovCount; i++) {
            uint32_t writeLen = 0;
            ret = SSLSend(iov[i].iov_base, iov[i].iov_len, writeLen);
            if (ret == SS_TIMEOUT) {
                NN_LOG_ERROR("(TLS)Failed to PostSendSgl body to peer in sock " << mId << " name " <<
                    mName << ", error is timeout with " << mSendTimeoutSecond << " second, seqNo " <<
                    reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->seqNo <<
                    ", the failed iov data len " << iov[NN_NO1].iov_len);
                return SS_TIMEOUT;
            }
            if (ret != SS_OK || writeLen != static_cast<uint32_t>(iov[i].iov_len)) {
                NN_LOG_ERROR("(TLS)Failed to PostSendSgl body to peer in sock " << mId << " name " << mName <<
                    ", seqNo " << reinterpret_cast<SockTransHeader *>(ctx->sendBuff)->seqNo <<
                    ", the failed iov data len " << iov[i].iov_len);
                return SS_SOCK_SEND_FAILED;
            }
        }
        return SS_OK;
    }

    inline SResult PostSendSgl(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            auto sendCtx = ctx->sendCtx;
            struct iovec iov[NN_NO5];
            iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);
            iov[NN_NO0].iov_len = sizeof(SockTransHeader);

            NN_LOG_TRACE_INFO("PostSendSgl in sock iov count " << sendCtx->iovCount << ", head size " <<
                iov[NN_NO0].iov_len);
            size_t requestSize = 0;
            for (uint16_t i = 0; i < sendCtx->iovCount; i++) {
                iov[i + NN_NO1].iov_base = reinterpret_cast<void *>(sendCtx->iov[i].lAddress);
                iov[i + NN_NO1].iov_len = sendCtx->iov[i].size;
                NN_LOG_TRACE_INFO("iov index " << i + NN_NO1 << ", length " << iov[i + NN_NO1].iov_len);
                requestSize += iov[i + NN_NO1].iov_len;
            }

            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            if (mEnableTls && ctx->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW_SGL) {
                if ((ret = PostSendSglSsl(ctx, iov, NN_NO5) != SS_OK)) {
                    NN_LOG_ERROR("PostSendSglSsl failed, ret: " << ret);
                    return ret;
                }
            } else {
                if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&iov), NN_NO1 + sendCtx->iovCount)) <
                    static_cast<ssize_t>(requestSize + sizeof(SockTransHeader))) {
                    if (ret == 0) {
                        return SS_TCP_RETRY;
                    }
                    if (errno == 0) {
                        NN_LOG_ERROR("Failed to PostSendSgl to peer in sock: " << mId << " name: " << mName <<
                            " with " << mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                        return SS_TIMEOUT;
                    }
                    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("Failed to PostSendSgl to peer in sock " << mId << " name " << mName << ", error " <<
                        NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                    return SS_SOCK_SEND_FAILED;
                }
            }

            if (mCbByWorkerInBlocking && ctx->opType == SockOpContextInfo::SockOpType::SS_SEND_RAW_SGL) {
                ctx->isSent = true;
                mSendQueue.PushBack(ctx);
                return SS_SOCK_SEND_EAGAIN;
            }
            NN_LOG_TRACE_INFO("Post send request successfully : sock " << mId << ", head imm data " <<
                sendCtx->sendHeader.immData << ", flags " << sendCtx->sendHeader.flags << ", seqNo " <<
                sendCtx->sendHeader.seqNo << ", data len " << sendCtx->sendHeader.dataLength);

            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

    inline SResult PostSendSgl(SockTransHeader &header, const UBSHcomNetTransSglRequest &req)
    {
        struct iovec iov[NN_NO5];
        iov[NN_NO0].iov_base = reinterpret_cast<void *>(&header);
        iov[NN_NO0].iov_len = sizeof(SockTransHeader);

        NN_LOG_TRACE_INFO("Send raw sgl in sock iov count " << req.iovCount << ", head size " << iov[NN_NO0].iov_len);
        size_t requestSize = 0;
        for (uint16_t i = 0; i < req.iovCount; i++) {
            iov[i + NN_NO1].iov_base = reinterpret_cast<void *>(req.iov[i].lAddress);
            iov[i + NN_NO1].iov_len = req.iov[i].size;
            NN_LOG_TRACE_INFO("iov index " << i + NN_NO1 << ", length " << req.iov[i].size);
            requestSize += iov[i + NN_NO1].iov_len;
        }

        std::lock_guard<std::mutex> guard(mIoMutex);
        ssize_t ret = 0;
        if (!mEnableTls) {
            if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&iov), NN_NO1 + req.iovCount)) <
                static_cast<ssize_t>(requestSize + sizeof(SockTransHeader))) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }
                if (errno == 0) {
                    NN_LOG_ERROR("Failed to PostSendSgl to peer in sock " << mId << " name " << mName << " with " <<
                        mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                    return SS_TIMEOUT;
                }
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("Failed to PostSendSgl to peer in sock " << mId << " name " << mName << ", error " <<
                    NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                return SS_SOCK_SEND_FAILED;
            }
        } else {
            if ((ret = writev(mFd, &iov[NN_NO0], NN_NO1)) < static_cast<ssize_t>(sizeof(SockTransHeader))) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }
                if (errno == 0) {
                    NN_LOG_ERROR("(TLS)Failed to PostSendSgl header to peer in sock " << mId << " name " << mName <<
                        " with " << mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                    return SS_TIMEOUT;
                }
                char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                NN_LOG_ERROR("(TLS)Failed to PostSendSgl header to peer in sock " << mId << " name " << mName <<
                    ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) << ", seqNo " <<
                    header.seqNo);
                return SS_SOCK_SEND_FAILED;
            }
            for (uint16_t i = 1; i < NN_NO1 + req.iovCount; i++) {
                uint32_t writeLen = 0;
                ret = SSLSend(iov[i].iov_base, iov[i].iov_len, writeLen);
                if (ret == SS_TIMEOUT) {
                    NN_LOG_ERROR("(TLS)Failed to PostSendSgl body to peer in sock " << mId << " name " << mName <<
                        ", error is timeout with " << mSendTimeoutSecond << " second, seqNo " << header.seqNo <<
                        ", the failed iov data len " << iov[i].iov_len);
                    return SS_TIMEOUT;
                }
                if (ret != SS_OK || writeLen != iov[i].iov_len) {
                    NN_LOG_ERROR("(TLS)Failed to PostSendSgl body to peer in sock " << mId << " name " << mName <<
                        ", seqNo " << header.seqNo << ", the failed iov data len " << iov[i].iov_len);
                    return SS_SOCK_SEND_FAILED;
                }
            }
        }
        NN_LOG_TRACE_INFO("PostSendSgl request successfully: sock " << mId << ", head imm data " << header.immData <<
            ", flags " << header.flags << ", seqNo " << header.seqNo << ", data len " << header.dataLength);
        return SS_OK;
    }

    inline SResult PostSendHead(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            if ((ret = ::send(mFd, reinterpret_cast<void *>(&ctx->sendCtx->sendHeader), sizeof(SockTransHeader), 0)) <
                static_cast<ssize_t>(sizeof(SockTransHeader))) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }
                if (errno == 0) {
                    NN_LOG_ERROR("Failed to PostSendHead to peer in sock " << mId << " name " << mName << " with " <<
                        mSendTimeoutSecond << " second, " << ret << " is sent");
                    return SS_TIMEOUT;
                }
                NN_LOG_ERROR("Failed to PostSendHead to peer in sock " << mId << " name " << mName << ", errno " <<
                    errno);
                return SS_SOCK_SEND_FAILED;
            }
            NN_LOG_TRACE_INFO("Post send head successfully: sock " << mId << ", head imm data " <<
                ctx->sendCtx->sendHeader.immData << ", flags " << ctx->sendCtx->sendHeader.flags << ", seqNo " <<
                ctx->sendCtx->sendHeader.seqNo);
            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

    inline SResult PostRead(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            auto sendCtx = ctx->sendCtx;
            struct iovec iov[NN_NO2];
            iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);
            iov[NN_NO0].iov_len = sizeof(SockTransHeader);
            iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iov[0]);
            iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSgeIov);

            auto length = iov[NN_NO0].iov_len + iov[NN_NO1].iov_len;
            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&iov), NN_NO2)) <
                static_cast<ssize_t>(length)) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }

                if (errno == 0) {
                    NN_LOG_ERROR("Failed to PostRead to peer in sock " << mId << " name " << mName << " with " <<
                        mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                    return SS_TIMEOUT;
                }

                NN_LOG_ERROR("Failed to PostRead to peer in sock " << mId << " name " << mName << ", errno " << errno <<
                    ", seqNo " << sendCtx->sendHeader.seqNo);
                return SS_SOCK_SEND_FAILED;
            }
            NN_LOG_TRACE_INFO("PostRead successfully: sock " << mId << ", head imm data " <<
                sendCtx->sendHeader.immData << ", flags " << sendCtx->sendHeader.flags << ", seqNo " <<
                sendCtx->sendHeader.seqNo << ", data len " << sendCtx->sendHeader.dataLength);
            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

    inline SResult PostWrite(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            auto sendCtx = ctx->sendCtx;
            struct iovec iov[NN_NO3];
            iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);
            iov[NN_NO0].iov_len = sizeof(SockTransHeader);
            iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iov[0]);
            iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSgeIov);
            iov[NN_NO2].iov_base = reinterpret_cast<void *>(sendCtx->iov[0].lAddress);
            iov[NN_NO2].iov_len = sendCtx->iov[0].size;

            auto length = iov[NN_NO0].iov_len + iov[NN_NO1].iov_len + iov[NN_NO2].iov_len;
            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&iov), NN_NO3)) <
                static_cast<ssize_t>(length)) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }
                if (errno == 0) {
                    NN_LOG_ERROR("Failed to PostSendSgl to peer in sock " << mId << " name " << mName << " with " <<
                        mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                    return SS_TIMEOUT;
                }
                NN_LOG_ERROR("Failed to PostSendSgl to peer in sock " << mId << " name " << mName << ", errno " <<
                    errno);
                return SS_SOCK_SEND_FAILED;
            }

            NN_LOG_TRACE_INFO("PostWrite request successfully : sock " << mId << ", head imm data " <<
                sendCtx->sendHeader.immData << ", flags " << sendCtx->sendHeader.flags << ", seqNo " <<
                sendCtx->sendHeader.seqNo << ", data len " << sendCtx->sendHeader.dataLength);

            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

    inline SResult PostReceiveHeader(SockTransHeader &header, int32_t timeoutSecond = 0)
    {
        if (NN_UNLIKELY(mRevTimeoutSecond != timeoutSecond)) {
            mRevTimeoutSecond = timeoutSecond;
            timeoutSecond = timeoutSecond > 0 ? timeoutSecond : timeoutSecond == 0 ? -1 : 0;
            struct timeval timeout = { timeoutSecond, 0 };
            if (NN_UNLIKELY(
                setsockopt(mFd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof(timeval)) < 0)) {
                return SS_TCP_SET_OPTION_FAILED;
            }
        }
        {
            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            uint32_t result = 0;
            auto buff = reinterpret_cast<void *>(&header);
            size_t remainingSize = sizeof(SockTransHeader);
            while (result < sizeof(SockTransHeader)) {
                ret = ::recv(mFd, buff, remainingSize, 0);
                if (errno == EAGAIN) {
                    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("Failed to PostReceiveHeader from peer in sock " << mId << " name " << mName <<
                        ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) <<
                        " due to timeout with " << mRevTimeoutSecond << " second, " << ret << " is received");
                    return SS_TIMEOUT;
                }
                if (ret <= 0) {
                    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("Failed to PostReceiveHeader from peer in sock " << mId << " name " << mName <<
                        ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                    return SS_SOCK_SEND_FAILED;
                }

                buff = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buff) + ret);
                result += static_cast<uint32_t>(ret);
                remainingSize -= static_cast<size_t>(ret);
            }
        }

        auto result = NetFunc::ValidateHeader(header);
        if (NN_UNLIKELY(result != NN_OK)) {
            NN_LOG_ERROR("Failed to validate received header, ep " << Id());
            return result;
        }
        NN_LOG_TRACE_INFO("PostReceiveHeader from peer successfully: sock " << mId << ", head imm data " <<
            header.immData << ", flags " << header.flags << ", seqNo " << header.seqNo);
        return SS_OK;
    }

    inline SResult PostReceiveBody(void *buff, uint32_t dataLength, bool isOneSide)
    {
        if (NN_UNLIKELY(buff == nullptr || dataLength == 0)) {
            return SS_PARAM_INVALID;
        }

        std::lock_guard<std::mutex> guard(mIoMutex);
        ssize_t ret = 0;
        uint32_t result = 0;
        size_t remainingSize = static_cast<size_t>(dataLength);
        while (result < dataLength) {
            if (!mEnableTls || isOneSide) {
                ret = ::recv(mFd, buff, remainingSize, 0);
                if (errno == EAGAIN) {
                    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("Failed to PostReceiveBody from peer in sock " << mId << " name " << mName <<
                        ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) <<
                        " due to timeout with " << mRevTimeoutSecond << " second, " << ret << " is received");
                    return SS_TIMEOUT;
                }
                if (ret <= 0) {
                    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("Failed to PostReceiveBody from peer in sock " << mId << " name " << mName <<
                        ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                    return SS_SOCK_SEND_FAILED;
                }
            } else {
                auto readResult = SSLRead(buff, remainingSize, reinterpret_cast<uint32_t &>(ret));
                if (readResult == SS_TIMEOUT) {
                    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
                    NN_LOG_ERROR("(TLS)Failed to PostReceiveBody from peer in sock " << mId << " name " << mName <<
                        ", error " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) <<
                        " due to timeout with " << mRevTimeoutSecond << " second, " << ret << " is received");
                    return SS_TIMEOUT;
                }

                if (readResult != SS_OK) {
                    NN_LOG_ERROR("(TLS)Failed to PostReceiveBody from peer in sock " << mId << " name " << mName);
                    return SS_SSL_READ_FAILED;
                }
            }

            buff = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(buff) + ret);
            result += static_cast<uint32_t>(ret);
            remainingSize -= static_cast<size_t>(ret);
        }

        return SS_OK;
    }

    inline SResult PostReadSgl(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            auto sendCtx = ctx->sendCtx;
            struct iovec iov[NN_NO3];
            iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);
            iov[NN_NO0].iov_len = sizeof(SockTransHeader);
            iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iovCount);
            iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSglRequest::iovCount);
            iov[NN_NO2].iov_base = reinterpret_cast<void *>(sendCtx->iov);
            iov[NN_NO2].iov_len = sizeof(UBSHcomNetTransSgeIov) * sendCtx->iovCount;

            auto length = iov[NN_NO0].iov_len + iov[NN_NO1].iov_len + iov[NN_NO2].iov_len;
            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&iov), NN_NO3)) <
                static_cast<ssize_t>(length)) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }

                if (errno == 0) {
                    NN_LOG_ERROR("Failed to PostReadSgl to peer in sock " << mId << " name " << mName << " with " <<
                        mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                    return SS_TIMEOUT;
                }

                NN_LOG_ERROR("Failed to PostReadSgl to peer in sock " << mId << " name " << mName << ", errno " <<
                    errno << ", seqNo " << sendCtx->sendHeader.seqNo);
                return SS_SOCK_SEND_FAILED;
            }
            NN_LOG_TRACE_INFO("PostReadSgl successfully: sock " << mId << ", head imm data " <<
                sendCtx->sendHeader.immData << ", flags " << sendCtx->sendHeader.flags << ", seqNo " <<
                sendCtx->sendHeader.seqNo << ", data len " << sendCtx->sendHeader.dataLength);
            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

    inline SResult PostReadSglAck(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            auto sendCtx = ctx->sendCtx;
            struct iovec iov[NN_NO7];
            iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);
            iov[NN_NO0].iov_len = sizeof(SockTransHeader);
            iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iovCount);
            iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSglRequest::iovCount);
            iov[NN_NO2].iov_base = reinterpret_cast<void *>(sendCtx->iov);
            iov[NN_NO2].iov_len = sizeof(UBSHcomNetTransSgeIov) * sendCtx->iovCount;

            auto length = iov[NN_NO0].iov_len + iov[NN_NO1].iov_len + iov[NN_NO2].iov_len;
            for (uint16_t i = 0; i < sendCtx->iovCount; i++) {
                iov[i + NN_NO3].iov_base = reinterpret_cast<void *>(sendCtx->iov[i].rAddress);
                iov[i + NN_NO3].iov_len = sendCtx->iov[i].size;
                length += iov[i + NN_NO3].iov_len;
            }

            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&iov), NN_NO3 + sendCtx->iovCount)) <
                static_cast<ssize_t>(length)) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }

                if (errno == 0) {
                    NN_LOG_ERROR("Failed to PostReadSglAck to peer in sock " << mId << " name " << mName << " with " <<
                        mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                    return SS_TIMEOUT;
                }

                NN_LOG_ERROR("Failed to PostReadSglAck to peer in sock " << mId << " name " << mName << ", errno " <<
                    errno << ", seqNo " << sendCtx->sendHeader.seqNo);
                return SS_SOCK_SEND_FAILED;
            }

            NN_LOG_TRACE_INFO("PostReadSglAck successfully: sock " << mId << ", head imm data " <<
                sendCtx->sendHeader.immData << ", flags " << sendCtx->sendHeader.flags << ", seqNo " <<
                sendCtx->sendHeader.seqNo << ", data len " << sendCtx->sendHeader.dataLength);
            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

    inline SResult PostWriteSgl(SockOpContextInfo *ctx)
    {
        if (NN_UNLIKELY(ctx == nullptr)) {
            return SS_PARAM_INVALID;
        }

        if (mTcpBlockingMode) {
            auto sendCtx = ctx->sendCtx;
            struct iovec iov[NN_NO7];
            iov[NN_NO0].iov_base = reinterpret_cast<void *>(&sendCtx->sendHeader);
            iov[NN_NO0].iov_len = sizeof(SockTransHeader);
            iov[NN_NO1].iov_base = reinterpret_cast<void *>(&sendCtx->iovCount);
            iov[NN_NO1].iov_len = sizeof(UBSHcomNetTransSglRequest::iovCount);
            iov[NN_NO2].iov_base = reinterpret_cast<void *>(sendCtx->iov);
            iov[NN_NO2].iov_len = sizeof(UBSHcomNetTransSgeIov) * sendCtx->iovCount;

            for (uint16_t i = 0; i < sendCtx->iovCount; i++) {
                iov[i + NN_NO3].iov_base = reinterpret_cast<void *>(sendCtx->iov[i].lAddress);
                iov[i + NN_NO3].iov_len = sendCtx->iov[i].size;
            }

            std::lock_guard<std::mutex> guard(mIoMutex);
            ssize_t ret = 0;
            if ((ret = writev(mFd, reinterpret_cast<const struct iovec *>(&iov), NN_NO3 + sendCtx->iovCount)) <
                static_cast<ssize_t>(sendCtx->sendHeader.dataLength + sizeof(SockTransHeader))) {
                if (ret == 0) {
                    return SS_TCP_RETRY;
                }

                if (errno == 0) {
                    NN_LOG_ERROR("Failed to PostWriteSgl to peer in sock " << mId << " name " << mName << " with " <<
                        mSendTimeoutSecond << " second timeout, " << ret << " is sent");
                    return SS_TIMEOUT;
                }

                NN_LOG_ERROR("Failed to PostWriteSgl to peer in sock " << mId << " name " << mName << ", errno " <<
                    errno << ", seqNo " << sendCtx->sendHeader.seqNo);
                return SS_SOCK_SEND_FAILED;
            }

            NN_LOG_TRACE_INFO("PostWriteSgl successfully: sock " << mId << ", head imm data " <<
                sendCtx->sendHeader.immData << ", flags " << sendCtx->sendHeader.flags << ", seqNo " <<
                sendCtx->sendHeader.seqNo << ", data len " << sendCtx->sendHeader.dataLength);
            return SS_OK;
        } else {
            mSendQueue.PushBack(ctx);
            return SS_SOCK_SEND_EAGAIN;
        }
    }

#define NO_BODY_FLAG(flag) ((flag) == NTH_WRITE_ACK || (flag) == NTH_WRITE_SGL_ACK)

#define RECEIVE_HEADER(result, fullReceived)                                                                      \
    do {                                                                                                          \
        result = ::recv(mFd, reinterpret_cast<void *>(headDataPtr + mReceiveState.ReceivedHeaderLen()),           \
            mReceiveState.headerToBeReceived, 0);                                                                 \
        if (NN_LIKELY((result) > 0)) {                                                                            \
            /* header is full */                                                                                  \
            if (mReceiveState.HeaderSatisfied(result)) {                                                          \
                if (NN_UNLIKELY(NetFunc::ValidateHeader(mHeader) != NN_OK)) {                                     \
                    NN_LOG_ERROR("Failed to validate received header param, sock " << mId);                       \
                    return SockOpContextInfo::SS_OPERATE_FAILURE;                                                 \
                }                                                                                                 \
                /* set body len to be received to the value in header */                                          \
                mReceiveState.bodyToBeReceived = static_cast<ssize_t>(mHeader.dataLength);                        \
                /* expand memory size */                                                                          \
                if (NN_UNLIKELY(!mReceiveBuff.ExpandIfNeed(mHeader.dataLength))) {                                \
                    NN_LOG_ERROR("Failed to expand receive buffer to " << mHeader.dataLength <<                   \
                        ", probably out of memory");                                                              \
                    return SockOpContextInfo::SS_OUT_OF_MEM;                                                      \
                }                                                                                                 \
                                                                                                                  \
                /* set actually body data to 0 */                                                                 \
                mReceiveBuff.ActualDataSize(0);                                                                   \
                /* if head only message do upper callback directly */                                             \
                fullReceived = (mHeader.dataLength == 0);                                                         \
                NN_LOG_TRACE_INFO("Receive sock " << mId << " head imm data " << mHeader.immData << ", flags " << \
                    mHeader.flags << ", seqNo " << mHeader.seqNo << ", data len " << mHeader.dataLength);         \
                if ((fullReceived) == true || NO_BODY_FLAG(mHeader.flags)) {                                      \
                    mReceiveState.ResetHeader();                                                                  \
                    fullReceived = true;                                                                          \
                    return SockOpContextInfo::SS_NO_ERROR;                                                        \
                }                                                                                                 \
            } else {                                                                                              \
                return SockOpContextInfo::SS_NO_ERROR; /* header is not fully received, continue to receive */    \
            }                                                                                                     \
        } else {                                                                                                  \
            /* ECONNRESET is broken during io, SUCCESS is broken during idle time. */                             \
            if (errno == ECONNRESET || errno == 0) {                                                              \
                NN_LOG_WARN("Sock " << mId << " does not receive data header, connection "                        \
                                    << " reset by peer, errno " << errno);                                        \
                return SockOpContextInfo::SS_RESET_BY_PEER; /* socket is closed by peer, socket is error */       \
            }                                                                                                     \
            /* if errno is eagain is normal, need to continue to receive */                                       \
            /* else meaning failed to read from socket, socket is error */                                        \
            if (errno != EAGAIN) {                                                                                \
                NN_LOG_ERROR("sock " << mId << " receive header failed, errno " << errno);                        \
            }                                                                                                     \
            return (errno == EAGAIN ? SockOpContextInfo::SS_NO_ERROR : SockOpContextInfo::SS_OPERATE_FAILURE);    \
        }                                                                                                         \
    } while (0)

#define RECEIVE_BODY(result, fullReceived)                                                                          \
    do {                                                                                                            \
        /* receive body */                                                                                          \
        auto dataPtr =                                                                                              \
            mReceiveBuff.DataIntPtr() + (mHeader.dataLength - static_cast<size_t>(mReceiveState.bodyToBeReceived)); \
        if (mEnableTls && ((mHeader.flags & 0xff) == NTH_TWO_SIDE || (mHeader.flags & 0xff) == NTH_TWO_SIDE_SGL)) { \
            auto readRet = SSLRead(reinterpret_cast<void *>(dataPtr), mReceiveState.bodyToBeReceived,               \
                reinterpret_cast<uint32_t &>(result));                                                              \
            if (readRet != SS_OK) {                                                                                 \
                result = -1;                                                                                        \
            }                                                                                                       \
        } else {                                                                                                    \
            result = ::recv(mFd, reinterpret_cast<void *>(dataPtr), mReceiveState.bodyToBeReceived, 0);             \
        }                                                                                                           \
        if (NN_LIKELY((result) > 0)) {                                                                              \
            /* body is full */                                                                                      \
            if (mReceiveState.BodySatisfied(result)) {                                                              \
                mReceiveState.ResetHeader();                                                                        \
                mReceiveBuff.ActualDataSize(mHeader.dataLength);                                                    \
                fullReceived = true;                                                                                \
                NN_LOG_TRACE_INFO("Receive sock " << mId << " full body size " << mHeader.dataLength);              \
                return SockOpContextInfo::SS_NO_ERROR;                                                              \
            }                                                                                                       \
                                                                                                                    \
            NN_LOG_TRACE_INFO("Receive sock " << mId << " not full body size " << mReceiveState.bodyToBeReceived);  \
            /* body is not fully received, continue to receive */                                                   \
            return SockOpContextInfo::SS_NO_ERROR;                                                                  \
        } else {                                                                                                    \
            /* ECONNRESET is broken during io, SUCCESS is broken during idle time. */                               \
            if (errno == ECONNRESET || errno == 0) {                                                                \
                NN_LOG_WARN("Sock " << mId << " does not receive data body, connection "                            \
                                    << " reset by peer, errno " << errno);                                          \
                return SockOpContextInfo::SS_RESET_BY_PEER; /* socket is closed by peer, socket is error */         \
            }                                                                                                       \
            /* if errno is eagain is normal, need to continue to receive */                                         \
            /* else meaning failed to read from socket, socket is error */                                          \
            if (errno != EAGAIN) {                                                                                  \
                NN_LOG_ERROR("sock " << mId << " receive body failed, errno " << errno);                            \
            }                                                                                                       \
            return (errno == EAGAIN ? SockOpContextInfo::SS_NO_ERROR : SockOpContextInfo::SS_OPERATE_FAILURE);      \
        }                                                                                                           \
    } while (0)

    /*
     * @brief Receive data when data is received
     *
     * @param fullReceived [out] if header and body are both received, can do upper call
     *
     * @param return true if socket is ok, otherwise the socket is broken then need to do connection broken process
     *
     */
    inline SockOpContextInfo::SockErrorType HandleIn(bool &fullReceived)
    {
        const auto headDataPtr = reinterpret_cast<uintptr_t>(&mHeader);

        fullReceived = false;

        /* receive header */
        ssize_t result = 0;
        if (mReceiveState.ShouldReceiveHeader()) {
            RECEIVE_HEADER(result, fullReceived);
        }

        RECEIVE_BODY(result, fullReceived);
    }

    inline bool HandleOut()
    {
        return true;
    }

    inline void SetSockOpContextInfoPool(SockOpContextInfoPool opCtxInfoPool)
    {
        mOpCtxInfoPool = opCtxInfoPool;
    }

    inline void SetSockSglContextInfoPool(SockSglContextInfoPool sglCtxInfoPool)
    {
        mSglCtxInfoPool = sglCtxInfoPool;
    }

    inline void SetSockHeaderReqInfoPool(SockHeaderReqInfoPool headerReqInfoPool)
    {
        mHeaderReqInfoPool = headerReqInfoPool;
    }

    inline void SetSockDriverSendMR(NormalMemoryRegionFixedBuffer *sockDriverSendMR)
    {
        mSockDriverSendMR = sockDriverSendMR;
    }

    inline void SetSockPostedHandler(SockPostedHandler sockPostedHandler)
    {
        mSendPostedHandler = sockPostedHandler;
    }

    inline void SetSockOneSideHandler(SockOneSideHandler oneSideDoneHandler)
    {
        mOneSideDoneHandler = oneSideDoneHandler;
    }

    inline void SetMrChecker(MemoryRegionChecker *checker)
    {
        mMrChecker = checker;
    }

    inline bool GetQueueSpace(uint32_t times = NN_NO8, uint32_t sleepUs = NN_NO64)
    {
        while (times-- > 0) {
            if (NN_LIKELY(__sync_sub_and_fetch(&mQueueVacantSize, NN_NO1) >= 0)) {
                return true;
            }
            __sync_add_and_fetch(&mQueueVacantSize, NN_NO1);
            usleep(sleepUs);
        }
        return false;
    }

    inline void ReturnQueueSpace(uint16_t size)
    {
        int32_t ref = __sync_add_and_fetch(&mQueueVacantSize, size);
        if (ref > mQueueSize) {
            NN_LOG_WARN("Queue size " << ref << " over capacity " << mQueueSize);
        }
    }

    std::string ToString()
    {
        std::ostringstream oss;
        oss << "info [type " << SockTypeToString(mType) << ", name " << mName << ", id " << mId << ", peer-ip-port " <<
            mPeerIpPort << ", up-ctx: " << mUpCtx << ", up-ctx1: " << mUpCtx1 << ", rev-buff-size: " <<
            mReceiveBuff.Size();
        return oss.str();
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

protected:
    SResult SetSockOption(const SockWorkerOptions &workerOptions);
    SResult ValidateOptions();
    SResult SetBlockingIo();
    SResult SetNonBlockingIo();

    /*
     * @brief Set ip and port of peer
     */
    inline void PeerIpPort(const std::string &value)
    {
        mPeerIpPort = value;
    }

    /*
     * @brief store connect info
     */
    inline void StoreConnInfo(uint32_t localIp, uint16_t listenPort, uint8_t version)
    {
        mLocalIp = localIp;
        mListenPort = listenPort;
        mVersion = version;
    }

    int SSLSend(const void *buf, uint32_t size, uint32_t &writeLen)
    {
        int ret = HcomSsl::SslWrite(mSsl, buf, size);
        if (ret <= 0) {
            int sslErrCode = HcomSsl::SslGetError(mSsl, ret);
            if (sslErrCode == HcomSsl::SSL_ERROR_WANT_WRITE) {
                return SS_TIMEOUT;
            }
            NN_LOG_ERROR("Failed to write data to TLS channel, ret: " << ret << ", errno: " << sslErrCode <<
                " write Len: " << size);
            return SS_OOB_SSL_WRITE_ERROR;
        }
        writeLen = static_cast<uint32_t>(ret);
        return SS_OK;
    }

    SResult SSLRead(void *buff, size_t dataLength, uint32_t &readLen)
    {
        auto ret = HcomSsl::SslRead(mSsl, buff, dataLength);
        if (ret <= 0) {
            int sslErrCode = HcomSsl::SslGetError(mSsl, ret);
            if (sslErrCode == HcomSsl::SSL_ERROR_WANT_READ) {
                return SS_TIMEOUT;
            }
            NN_LOG_ERROR("SSL read failed sock id " << mId << " name " << mName << ", error " << sslErrCode);
            return SS_SSL_READ_FAILED;
        }
        readLen = static_cast<uint32_t>(ret);
        return SS_OK;
    }

protected:
    int mFd = -1;                       /* socket fd */
    uint64_t mUpCtx = 0;                /* up context */
    uint64_t mUpCtx1 = 0;               /* up context 1 */
    SockBuff mReceiveBuff;              /* one extendable receive buffer */
    SockTransHeader mHeader;            /* sock command header */
    SockReceiveState mReceiveState {};  /* receive data status */
    bool mCbByWorkerInBlocking = false; /* worker call send post cb for blocking io */
    bool mTcpBlockingMode = true;       /* tcp mode: nonblocking in default */
    int64_t mQueueVacantSize = 0;
    int64_t mQueueSize = 0;
    std::mutex mInitMutex;
    SockOptions mOptions; /* sock options */
    SSL *mSsl = nullptr;
    NetSecrets mSecret;
    bool mEnableTls = true;
    std::string mName;       /* name of sock */
    std::string mPeerIpPort; /* peer ip and port */
    uint32_t mLocalIp = INVALID_IP;
    uint16_t mListenPort = 0;
    uint8_t mVersion = 0;
    uint64_t mId = 0;          /* uid */
    SockType mType = SOCK_TCP; /* sock type */
    bool mInited = false;      /* inited or not */

    std::mutex mIoMutex;

    uint32_t mSeqIndex = 1;
    std::mutex mCtxMutex;                                      /* op context mutex */
    std::unordered_map<uint32_t, SockOpContextInfo *> mCtxMap; /* op context map */

    NetRingBuffer<SockOpContextInfo *> mSendQueue;
    SendingQueueRequest mSendingQueueRequest;

    SockOpContextInfoPool mOpCtxInfoPool;
    SockSglContextInfoPool mSglCtxInfoPool;
    SockHeaderReqInfoPool mHeaderReqInfoPool;
    SockPostedHandler mSendPostedHandler = nullptr;
    SockOneSideHandler mOneSideDoneHandler = nullptr;
    NormalMemoryRegionFixedBuffer *mSockDriverSendMR = nullptr;
    MemoryRegionChecker *mMrChecker = nullptr;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class SockWorker;
    friend class NetDriverSockWithOOB;

private:
    int32_t mSendTimeoutSecond = -1;
    int32_t mRevTimeoutSecond = -1;
};
}
}

#endif // OCK_HCOM_SOCK_WRAPPER_H_234234
