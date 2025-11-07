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
#ifndef OCK_HCOM_NET_SHM_DRIVER_OOB_H
#define OCK_HCOM_NET_SHM_DRIVER_OOB_H

#include "hcom.h"

#include "net_common.h"
#include "net_delay_release_timer.h"
#include "net_oob.h"
#include "net_shm_common.h"
#include "securec.h"
#include "shm_channel_keeper.h"
#include "shm_handle.h"
#include "shm_mr_pool.h"

namespace ock {
namespace hcom {
class NetDriverShmWithOOB : public UBSHcomNetDriver {
public:
    NetDriverShmWithOOB(const std::string &name, bool startOob, UBSHcomNetDriverProtocol protocol)
        : UBSHcomNetDriver(name, startOob, protocol)
    {
        OBJ_GC_INCREASE(NetDriverShmWithOOB);
    }

    ~NetDriverShmWithOOB() override
    {
        OBJ_GC_DECREASE(NetDriverShmWithOOB);
    }

    NResult Initialize(const UBSHcomNetDriverOptions &option) override;

    void UnInitialize() override;

    NResult Start() override;
    void Stop() override;

    NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr) override;
    NResult CreateMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr) override;
    NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr, unsigned long memid) override;

    void DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr) override;

    NResult Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo,
        uint8_t clientGrpNo) override;

    NResult Connect(const std::string &oobIp, uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep,
        uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx) override;

    NResult Connect(const std::string &serverUrl, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags,
        uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0, uint64_t ctx = 0) override;

    NResult MultiRailNewConnection(OOBTCPConnection &conn);

    void DestroyEndpoint(UBSHcomNetEndpointPtr &ep) override;

    NResult SendExchangeInfo(OOBTCPConnection &conn, ShmConnExchangeInfo &exInfo);
    NResult ReceiveExchangeInfo(OOBTCPConnection &conn, ShmConnExchangeInfo &exInfo);

    void *MapAndRegVaForUB(unsigned long memid, uint64_t &va) override;

    NResult UnmapVaForUB(uint64_t &va) override;

    inline NResult ValidateMemoryRegion(uint64_t lKey, uintptr_t address, uint64_t size)
    {
        return mMrChecker.Validate(lKey, address, size);
    }

    inline UBSHcomNetDriverOptions GetOptions()
    {
        return mOptions;
    }

protected:
    NResult ValidateOptions();
    NResult CreateWorkerResource();
    NResult CreateWorkers();
    void ClearWorkers();
    void UnInitializeInner();
    void StopInner();

    NResult HandleNewOobConn(OOBTCPConnection &conn);
    NResult HandleNewRequest(ShmOpContextInfo &ctx, uint32_t immData);
    NResult HandleReqPosted(ShmOpCompInfo &ctx);
    NResult OneSideDone(ShmOpContextInfo *ctx);

    void HandleChanelKeeperMsg(const ShmChKeeperMsgHeader &header, const ShmChannelPtr &channelPtr);
    void ProcessEpError(const ShmChannelPtr &channelPtr);

    NResult ConnectSyncEp(const std::string &oobIp, uint16_t oobPort, const std::string &payload,
        UBSHcomNetEndpointPtr &outEp, uint8_t serverGrpNo, uint64_t ctx);

    inline void AddEp(const UBSHcomNetEndpointPtr &newEp)
    {
        /* added into map */
        if (NN_LIKELY(newEp != nullptr)) {
            std::lock_guard<std::mutex> guard(mEndPointsMutex);
            mEndPoints.emplace(newEp->Id(), newEp);
        }
    }

    inline bool Remove(uint64_t id)
    {
        std::lock_guard<std::mutex> guard(mEndPointsMutex);
        return (mEndPoints.erase(id) > 0);
    }

    inline const std::string &ChooseListenIp()
    {
        if (NN_UNLIKELY(mFilteredIps.empty())) {
            return CONST_EMPTY_STRING;
        }

        return mFilteredIps[0];
    }

    void ClearShmLeftFile();

    void HandleKeeperMsgGetMrFd(const ShmChKeeperMsgHeader &header, const ShmChannelPtr &channelPtr);

protected:
    std::vector<ShmWorker *> mWorkers;
    std::vector<std::string> mFilteredIps;
    NetMemPoolFixedPtr mOpCompMemPool = nullptr;
    NetMemPoolFixedPtr mOpCtxMemPool = nullptr;
    NetMemPoolFixedPtr mSglCompMemPool = nullptr;
    ShmChannelKeeperPtr mChannelKeeper = nullptr;
    DelayReleaseTimerPtr mDelayReleaseTimer = nullptr;
    MemoryRegionChecker mMrChecker;
    std::thread mClearThread;
    std::atomic_bool mClearThreadStarted { false };

private:
    friend class NetAsyncEndpointShm;
    friend class NetSyncEndpointShm;
};
}
}

#endif // OCK_HCOM_NET_SHM_DRIVER_OOB_H
