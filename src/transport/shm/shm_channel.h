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
#ifndef OCK_HCOM_SHM_CHANNEL_H
#define OCK_HCOM_SHM_CHANNEL_H

#include <queue>
#include <sys/socket.h>
#include <sys/types.h>

#include "net_common.h"
#include "shm_channel_keeper.h"
#include "shm_common.h"
#include "shm_data_channel.h"
#include "shm_queue.h"

namespace ock {
namespace hcom {
constexpr uint32_t QUEUE_TIMEOUT_US = NN_NO5 * NN_NO1000000; // 5s

class ShmChannel {
public:
    static uint32_t gQueueSizeCap;

public:
    static HResult CreateAndInit(const std::string &name, uint64_t id, uint32_t dcBuckSize, uint16_t dcBuckCount,
        ShmChannelPtr &out)
    {
        ShmChannelPtr tmp = new (std::nothrow) ShmChannel(name, id, dcBuckSize, dcBuckCount);
        if (NN_UNLIKELY(tmp.Get() == nullptr)) {
            NN_LOG_ERROR("Failed to new ShmChannel " << name << ", probably out of memory");
            return SH_NEW_OBJECT_FAILED;
        }

        auto result = tmp->Initialize();
        if (NN_UNLIKELY(result != SH_OK)) {
            return result;
        }

        out.Set(tmp.Get());
        return result;
    }

public:
    ShmChannel(const std::string &name, uint64_t id, uint32_t dcBuckSize, uint16_t dcBuckCount)
        : mId(id), mName(name), mSendDCBuckSize(dcBuckSize), mSendDCBuckCount(dcBuckCount)
    {
        OBJ_GC_INCREASE(ShmChannel);
    }

    ~ShmChannel()
    {
        UnInitialize();
        OBJ_GC_DECREASE(ShmChannel);
    }

    HResult Initialize();
    void UnInitialize();

    inline const std::string &PeerIpPort() const
    {
        return mPeerIpPort;
    }

    inline void PeerIpAndPort(const std::string &value)
    {
        mPeerIpPort = value;
    }

    /*
     * @brief Set a context by caller
     */
    inline uint64_t UpContext() const
    {
        return mUpCtx;
    }

    /*
     * @brief Set a context by caller
     */
    inline void UpContext(uint64_t value)
    {
        mUpCtx = value;
    }

    /*
     * @brief Set a context by caller
     */
    inline uint64_t UpContext1() const
    {
        return mUpCtx1;
    }

    /*
     * @brief Set a context by caller
     */
    inline void UpContext1(uint64_t value)
    {
        mUpCtx1 = value;
    }

    /*
     * @brief Get the file description of uds
     */
    inline int UdsFD() const
    {
        return mFd;
    }

    inline void UdsFD(int fd)
    {
        mFd = fd;
    }

    inline const std::string &UdsName() const
    {
        return mUdsName;
    }

    inline void UdsName(std::string udsName)
    {
        mUdsName = udsName;
    }

    inline uint64_t Id() const
    {
        return mId;
    }

    inline void Close()
    {
        NetFunc::NN_SafeCloseFd(mFd);
    }

    bool FillExchangeInfo(ShmConnExchangeInfo &info) const;

    HResult ChangeToReady(const ShmConnExchangeInfo &info);

    inline HResult DCGetFreeBuck(uintptr_t &address, uint64_t &offsetToBase, uint16_t waitPeriodUs = NN_NO100,
        int32_t timeoutSecond = -1)
    {
        NN_ASSERT_LOG_RETURN(mDataChannel != nullptr, SH_NOT_INITIALIZED)
        return mDataChannel->TryOccupyWithWait(address, offsetToBase, waitPeriodUs, timeoutSecond);
    }

    inline void DCMarkBuckFree(uintptr_t address)
    {
        if (NN_UNLIKELY(mDataChannel == nullptr)) {
            NN_LOG_WARN("data channel is null in DCMarkBuckFree");
            return;
        }
        mDataChannel->MarkFree(address);
    }

    inline void DCMarkPeerBuckFree(uintptr_t address)
    {
        if (NN_UNLIKELY(mPeerDataChannel == nullptr)) {
            NN_LOG_WARN("data channel is null in DCMarkBuckFree");
            return;
        }
        mPeerDataChannel->MarkFree(address);
    }

    HResult EQEventEnqueue(ShmEvent &event);

    HResult GetRemoteMrFds(uint32_t remoteKey, int &rfd);
    HResult GetRemoteMrHandle(uint32_t remoteKey, uint32_t bufSize, ShmMRHandleMap &mrHandleMap);
    void AddOpCtxInfo(ShmOpContextInfo *shmCtxInfo);
    void AddOpCompInfo(ShmOpCompInfo *compInfo);

    HResult RemoveOpCtxInfo(ShmOpContextInfo *ctxInfo);
    HResult RemoveOpCompInfo(ShmOpCompInfo *compInfo);

    // need to call this when qp broken, to get these contexts to return mrs
    void GetCtxPosted(ShmOpContextInfo *&remaining);
    void GetCompPosted(ShmOpCompInfo *&remaining);

    inline uint64_t PeerChannelId() const
    {
        return mPeerChId;
    }

    inline uintptr_t PeerChannelAddress() const
    {
        return mPeerChAddress;
    }

    inline UBSHcomNetAtomicState<ShmChannelState> &State()
    {
        return mState;
    }

    HResult GetPeerDataAddressByOffset(uint64_t offset, uintptr_t &address);

    HResult AddMrFd(int fd)
    {
        std::unique_lock<std::mutex> guard(mMrFdQueueMutex);
        if (mMrFdQueue.size() >= gQueueSizeCap) {
            NN_LOG_ERROR("Failed to add fd in the queue, the queue size is exceeded in channel " << mName << " " <<
                mId);
            return SH_FDS_QUEUE_FULL;
        }

        mMrFdQueue.push(fd);
        return SH_OK;
    }

    HResult RemoveMrFd(int &fd)
    {
        bool flag = true;
        auto start = NetMonotonic::TimeUs();
        do {
            {
                std::lock_guard<std::mutex> guard(mMrFdQueueMutex);
                if (!mMrFdQueue.empty()) {
                    fd = mMrFdQueue.front();
                    mMrFdQueue.pop();
                    return SH_OK;
                }
            }

            auto end = NetMonotonic::TimeUs();
            auto pollTime = end - start;
            if (QUEUE_TIMEOUT_US < pollTime) {
                NN_LOG_ERROR("Within a limited time, failed to get remote mr fds as queue empty in channel " << mName <<
                    " " << mId);
                flag = false;
                break;
            }

            usleep(NN_NO128);
        } while (flag);

        return SH_TIME_OUT;
    }

    inline static uint32_t GetQueueCap() noexcept
    {
        /* set fd queue size */
        char *envSize = ::getenv("HCOM_SHM_EXCHANGE_FD_QUEUE_SIZE");

        if (envSize != nullptr) {
            long value = 0;
            if (NetFunc::NN_Stol(envSize, value) && value >= NN_NO10 && value <= NN_NO256) {
                NN_LOG_INFO("Successfully to set the fd exchange queue capacity to " << value);
                return value;
            }
            NN_LOG_ERROR("Invalid setting 'HCOM_SHM_EXCHANGE_FD_QUEUE_SIZE' which should be 10~256, restored fd "
                "exchange queue capacity to default value 10");
        }

        return NN_NO10;
    }

    HResult AddUserFds(int fds[], uint32_t len)
    {
        std::unique_lock<std::mutex> guard(mUserFdQueueMutex);
        if (mUserFdQueue.size() + len > gQueueSizeCap) {
            NN_LOG_ERROR("Failed to add fd in the queue, the queue size is exceeded in channel " << mName << " " <<
                mId);
            return SH_FDS_QUEUE_FULL;
        }

        for (uint32_t i = 0; i < len; i++) {
            mUserFdQueue.push(fds[i]);
        }

        return SH_OK;
    }

    HResult RemoveUserFds(int fds[], uint32_t len, int32_t timeoutSec)
    {
        uint32_t timeoutUs = QUEUE_TIMEOUT_US;
        if (timeoutSec > 0) {
            timeoutUs = static_cast<uint32_t>(timeoutSec) * NN_NO1000000;
        }
        bool flag = true;
        uint32_t index = 0;
        auto start = NetMonotonic::TimeUs();
        do {
            {
                std::lock_guard<std::mutex> guard(mUserFdQueueMutex);
                while (!mUserFdQueue.empty() && index < len) {
                    fds[index] = mUserFdQueue.front();
                    mUserFdQueue.pop();
                    index++;
                }
                if (index == len) {
                    return SH_OK;
                }
            }

            auto end = NetMonotonic::TimeUs();
            auto pollTime = end - start;
            if (timeoutUs < pollTime) {
                NN_LOG_ERROR("Failed to remove user fds in queue of channel " << mName << " " << mId <<
                    " as timeout " << timeoutUs << " us is exceeded");
                flag = false;
                break;
            }

            usleep(NN_NO128);
        } while (flag);

        return SH_TIME_OUT;
    }

    inline uint32_t GetSendDCBuckSize() const
    {
        return mSendDCBuckSize;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

public:
    // 1.ensure the order of send header firstly and send fds secondly in multi thread
    // 2.add the same lock in GET_MR_FD\SEND_MR_FD\EXCHANGE_USER_FD process to ensure order of header +
    // fds in diff thread, and then keeper thread receive order is true
    std::mutex mFdMutex;

private:
    HResult ValidateExchangeInfo(const ShmConnExchangeInfo &info);

private:
    ShmEventQueue *mPeerEventQueue = nullptr;   /* event queue of peer worker */
    ShmDataChannel *mDataChannel = nullptr;     /* channel for data transfer */
    ShmDataChannel *mPeerDataChannel = nullptr; /* peer data channel for reading data */
    uint64_t mPeerChId = 0;                     /* peer channel id */
    uint64_t mPeerChAddress = 0;                /* peer channel address */
    uint64_t mId = 0;                           /* id of this channel */
    uint64_t mUpCtx = 0;                        /* up context */
    uint64_t mUpCtx1 = 0;                       /* up context 1 */
    bool mPeerEventPooling = true;              /* peer is event pooling or not */
    NetSpinLock mLock;                          /* spin lock of post ctx */
    ShmOpContextInfo mCtxPosted {};             /* one side done ctx double linked list */
    ShmOpCompInfo mCompPosted {};               /* two side complete post ctx double linked list */
    uint32_t mCtxPostedCount { 0 };             /* one side done ctx count */
    uint32_t mCompPostedCount { 0 };            /* two side complete post ctx count */

    int mFd = -1; /* uds fd to transfer file descriptor of shm files, between client and server */
    std::string mUdsName;

    std::string mName;                   /* name of channel */
    std::string mPeerIpPort;             /* peer ip port */
    uint32_t mSendDCBuckSize = NN_NO256; /* buck size of data channel for send */
    uint16_t mSendDCBuckCount = NN_NO16; /* buck count of data channel for send */
    std::mutex mMrFdQueueMutex;          /* lock for add/remove in exchange mr fd queue */
    std::queue<int> mMrFdQueue;          /* exchange one side mr fd queue */
    std::mutex mUserFdQueueMutex;        /* lock for add/remove in exchange user fd queue */
    std::queue<int> mUserFdQueue;        /* exchange user fd queue */
    UBSHcomNetAtomicState<ShmChannelState> mState { CH_NEW };

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

inline void ShmChannel::AddOpCompInfo(ShmOpCompInfo *compInfo)
{
    if (NN_LIKELY(compInfo != nullptr)) {
        // bi-direction linked list, 4 step to insert to head
        compInfo->prev = &mCompPosted;
        mLock.Lock();
        // head -><- first -><- second -><- third -> nullptr
        // insert into the head place
        compInfo->next = mCompPosted.next;
        if (mCompPosted.next != nullptr) {
            mCompPosted.next->prev = compInfo;
        }
        mCompPosted.next = compInfo;
        ++mCompPostedCount;
        mLock.Unlock();
    }
}

inline HResult ShmChannel::RemoveOpCompInfo(ShmOpCompInfo *compInfo)
{
    mLock.Lock();
    if (mCompPostedCount == 0) {
        mLock.Unlock();
        return SH_OP_CTX_REMOVED;
    }

    if (NN_LIKELY(compInfo != nullptr)) {
        // bi-direction linked list, 4 step to remove one
        // repeat remove
        if (compInfo->prev == nullptr) {
            mLock.Unlock();
            return SH_OP_CTX_REMOVED;
        }

        // head-><- first -><- second -><- third -> nullptr
        compInfo->prev->next = compInfo->next;
        if (compInfo->next != nullptr) {
            compInfo->next->prev = compInfo->prev;
        }
        --mCompPostedCount;
        compInfo->prev = nullptr;
        compInfo->next = nullptr;
    }
    mLock.Unlock();
    return SH_OK;
}

inline void ShmChannel::AddOpCtxInfo(ShmOpContextInfo *shmCtxInfo)
{
    if (NN_LIKELY(shmCtxInfo != nullptr)) {
        // bi-direction linked list, 4 step to insert to head
        shmCtxInfo->prev = &mCtxPosted;
        mLock.Lock();
        // head -><- first -><- second -><- third -> nullptr
        // insert into the head place
        shmCtxInfo->next = mCtxPosted.next;
        if (mCtxPosted.next != nullptr) {
            mCtxPosted.next->prev = shmCtxInfo;
        }
        mCtxPosted.next = shmCtxInfo;
        ++mCtxPostedCount;
        mLock.Unlock();
    }
}

inline HResult ShmChannel::RemoveOpCtxInfo(ShmOpContextInfo *ctxInfo)
{
    mLock.Lock();
    if (mCtxPostedCount == 0) {
        mLock.Unlock();
        return SH_OP_CTX_REMOVED;
    }

    if (NN_LIKELY(ctxInfo != nullptr)) {
        // bi-direction linked list, 4 step to remove one
        // repeat remove
        if (ctxInfo->prev == nullptr) {
            mLock.Unlock();
            return SH_OP_CTX_REMOVED;
        }

        // head-><- first -><- second -><- third -> nullptr
        ctxInfo->prev->next = ctxInfo->next;
        if (ctxInfo->next != nullptr) {
            ctxInfo->next->prev = ctxInfo->prev;
        }
        --mCtxPostedCount;

        ctxInfo->prev = nullptr;
        ctxInfo->next = nullptr;
    }
    mLock.Unlock();
    return SH_OK;
}

inline void ShmChannel::GetCtxPosted(ShmOpContextInfo *&remaining)
{
    mLock.Lock();
    // head -> first -><- second -><- third -> nullptr
    remaining = mCtxPosted.next;
    mCtxPosted.next = nullptr;
    mCtxPostedCount = 0;
    mLock.Unlock();
}

inline void ShmChannel::GetCompPosted(ShmOpCompInfo *&remaining)
{
    mLock.Lock();
    // head -> first -><- second -><- third -> nullptr
    remaining = mCompPosted.next;
    mCompPosted.next = nullptr;
    mCompPostedCount = 0;
    mLock.Unlock();
}

inline HResult ShmChannel::EQEventEnqueue(ShmEvent &event)
{
    NN_ASSERT_LOG_RETURN(mPeerEventQueue != nullptr, SH_NOT_INITIALIZED)

    if (mPeerEventPooling) {
        return mPeerEventQueue->EnqueueAndNotify(event);
    }

    return mPeerEventQueue->Enqueue(event);
}

inline HResult ShmChannel::GetRemoteMrFds(uint32_t remoteKey, int &rfd)
{
    ShmChKeeperMsgHeader header {};
    header.msgType = ShmChKeeperMsgType::GET_MR_FD;
    header.dataSize = sizeof(remoteKey);

    std::lock_guard<std::mutex> guard(mFdMutex);
    if (NN_UNLIKELY(::send(UdsFD(), &header, sizeof(ShmChKeeperMsgHeader), MSG_NOSIGNAL) <= 0)) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to notify exchange mr fd info, as"
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return SH_ERROR;
    }

    if (NN_UNLIKELY(::send(UdsFD(), &remoteKey, sizeof(remoteKey), MSG_NOSIGNAL) <= 0)) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to get remote mr fds for key:" << remoteKey << " as"
                << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return SH_ERROR;
    }

    return RemoveMrFd(rfd);
}

inline HResult ShmChannel::GetRemoteMrHandle(uint32_t remoteKey, uint32_t bufSize, ShmMRHandleMap &mrHandleMap)
{
    int rfd = 0;
    auto result = GetRemoteMrFds(remoteKey, rfd);
    if (NN_UNLIKELY(result != SH_OK)) {
        NN_LOG_INFO("Get remote mr fd failed, result is:" << result);
        return result;
    }

    std::string tmpName = "tmp_mr";
    if (mrHandleMap.GetFromRemoteMap(rfd) == nullptr) {
        auto remoteHandle = new (std::nothrow) ShmHandle(mName, tmpName, rfd, bufSize, rfd, false);
        if (remoteHandle == nullptr) {
            NN_LOG_ERROR("Failed to new remote shm handle for shm data channel " << mName <<
                ", probably out of memory");
            return SH_NEW_OBJECT_FAILED;
        }

        result = remoteHandle->Initialize();
        if (NN_UNLIKELY(result != NN_OK)) {
            delete remoteHandle;
            return result;
        }
        mrHandleMap.AddToRemoteMap(remoteKey, remoteHandle);
    }

    return NN_OK;
}

inline bool ShmChannel::FillExchangeInfo(ShmConnExchangeInfo &info) const
{
    if (NN_LIKELY(mDataChannel != nullptr)) {
        info.channelId = mId;
        info.dcBuckSize = mDataChannel->BuckSize();
        info.dcBuckCount = mDataChannel->BuckCount();
        info.channelAddress = reinterpret_cast<uintptr_t>(this);
        info.channelFd = mDataChannel->GetShmHandle()->Fd();
        return info.SetDCName(mDataChannel->Filepath());
    }

    return false;
}

inline HResult ShmChannel::GetPeerDataAddressByOffset(uint64_t offset, uintptr_t &address)
{
    NN_ASSERT_LOG_RETURN(mPeerDataChannel != nullptr, SH_NOT_INITIALIZED)
    return mPeerDataChannel->GetAddressByOffset(offset, address);
}
}
}

#endif // OCK_HCOM_SHM_CHANNEL_H
