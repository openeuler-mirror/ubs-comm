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
#ifndef OCK_HCOM_NET_SHM_ENDPOINT_H
#define OCK_HCOM_NET_SHM_ENDPOINT_H

#include "hcom.h"
#include "transport/net_endpoint_impl.h"
#include "hcom_utils.h"
#include "net_common.h"
#include "net_monotonic.h"
#include "net_security_alg.h"
#include "net_shm_common.h"
#include "net_shm_driver_oob.h"
#include "shm_composed_endpoint.h"
#include "shm_handle_fds.h"

namespace ock {
namespace hcom {
class NetSyncEndpointShm : public NetEndpointImpl {
public:
    NetSyncEndpointShm(uint64_t id, ShmChannel *ch, NetDriverShmWithOOB *driver,
        const UBSHcomNetWorkerIndex &workerIndex, ShmSyncEndpoint *shmEp, ShmMRHandleMap &handleMap);
    ~NetSyncEndpointShm() override;

    NResult SetEpOption(UBSHcomEpOptions &epOptions) override
    {
        NN_LOG_WARN("[SHM SyncEp] Empty function for now");
        return NN_OK;
    }

    uint32_t GetSendQueueCount() override
    {
        NN_LOG_WARN("[SHM SyncEp] Empty function for now");
        return 0;
    }

    const std::string &PeerIpAndPort() override
    {
        if (NN_LIKELY(mShmCh != nullptr)) {
            return mShmCh->PeerIpPort();
        }

        return CONST_EMPTY_STRING;
    }

    const std::string &UdsName() override
    {
        if (NN_LIKELY(mShmCh != nullptr)) {
            return mShmCh->UdsName();
        }

        return CONST_EMPTY_STRING;
    }

    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo) override;
    NResult PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNO) override;
    NResult PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo) override;
    NResult PostRead(const UBSHcomNetTransRequest &request) override;
    NResult PostRead(const UBSHcomNetTransSglRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransRequest &request) override;
    NResult PostWrite(const UBSHcomNetTransSglRequest &request) override;
    NResult WaitCompletion(int32_t timeout) override;
    NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) override;
    NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) override;

    NResult GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &idInfo) override
    {
        if (!mState.Compare(NEP_ESTABLISHED)) {
            NN_LOG_ERROR("[SHM SyncEp] EP is not established");
            return NN_EP_NOT_ESTABLISHED;
        }

        if (!mDriver->mStartOobSvr) {
            NN_LOG_ERROR("[SHM SyncEp] oob server is not start");
            return NN_UDS_ID_INFO_NOT_SUPPORT;
        }

        idInfo = mRemoteUdsIdInfo;
        return NN_OK;
    }

    bool GetPeerIpPort(std::string &ip, uint16_t &port) override
    {
        NN_LOG_WARN("Invalid operation for shm, shm does not have ip and port");
        return false;
    }

    NResult SendFds(int fds[], uint32_t len) override
    {
        if (NN_UNLIKELY(len < NN_NO1 || len > NN_NO4)) {
            NN_LOG_ERROR("Failed to send fds in shm async ep as length should more than 0 and less than 4.");
            return NN_PARAM_INVALID;
        }

        if (NN_UNLIKELY(!mState.Compare(NEP_ESTABLISHED))) {
            NN_LOG_ERROR("Failed to send fds in shm async ep as endpoint " << mId << " is not established, state is " <<
                UBSHcomNEPStateToString(mState.Get()));
            return NN_EP_NOT_ESTABLISHED;
        }

        int innerFds[NN_NO4] = {0};
        for (uint32_t i = 0; i < len; i++) {
            innerFds[i] = fds[i];
            if (fds[i] <= 0) {
                NN_LOG_ERROR("Failed to send fds in shm async ep, as invalid fds index:" << i);
                return NN_INVALID_PARAM;
            }
        }

        std::lock_guard<std::mutex> guard(mShmCh->mFdMutex);
        ShmChKeeperMsgHeader header {};
        header.msgType = ShmChKeeperMsgType::EXCHANGE_USER_FD;
        header.dataSize = len;
        if (NN_UNLIKELY(::send(mShmCh->UdsFD(), &header, sizeof(ShmChKeeperMsgHeader), MSG_NOSIGNAL) <= 0)) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to send header info of exchange external fd to peer, error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return NN_ERROR;
        }

        return ShmHandleFds::SendMsgFds(mShmCh->UdsFD(), innerFds, NN_NO4);
    }

    NResult ReceiveFds(int fds[], uint32_t len, int32_t timeoutSec) override
    {
        if (NN_UNLIKELY(len < NN_NO1 || len > NN_NO4)) {
            NN_LOG_ERROR("Failed to receive fds in shm async ep as length should more than 0 and less than 4.");
            return NN_PARAM_INVALID;
        }

        if (NN_UNLIKELY(!mState.Compare(NEP_ESTABLISHED))) {
            NN_LOG_ERROR("Failed to receive fds in shm async ep as endpoint " << mId <<
                " is not established, state is " << UBSHcomNEPStateToString(mState.Get()));
            return NN_EP_NOT_ESTABLISHED;
        }

        return mShmCh->RemoveUserFds(fds, len, timeoutSec);
    }

    void Close() override
    {
        if (NN_UNLIKELY(mShmCh != nullptr)) {
            mShmCh->Close();
        }
    }

private:
    static bool inline NeedRetry(HResult res)
    {
        if (res == SH_OP_CTX_FULL || res == SH_RETRY_FULL) {
            return true;
        }

        return false;
    }

    uint64_t inline GetFinishTime()
    {
        if (mDefaultTimeout > 0) {
            return NetMonotonic::TimeNs() + static_cast<uint64_t>(mDefaultTimeout) * 1000000000UL;
        } else if (mDefaultTimeout < 0) {
            return UINT64_MAX;
        }

        return 0;
    }

    ShmChannel *mShmCh = nullptr;
    NetDriverShmWithOOB *mDriver = nullptr;
    ShmSyncEndpoint *mShmEp = nullptr;
    uint32_t mAllowedSize = 0;
    uint32_t mLastSendSeqNo = 0;
    ShmOpContextInfo::ShmOpType mDemandPollingOpType = ShmOpContextInfo::SH_SEND;
    UBSHcomNetMessage mRespMessage;
    UBSHcomNetResponseContext mRespCtx;
    ShmMRHandleMap &mrHandleMap;

    bool mExistDelayEvent = false;
    ShmEvent mDelayHandleReceiveEvent;
};
}
}

#endif // OCK_HCOM_NET_SHM_ENDPOINT_H
