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
#ifndef OCK_HCOM_NET_SOCK_DRIVER_OOB_H_234234
#define OCK_HCOM_NET_SOCK_DRIVER_OOB_H_234234

#include "net_sock_common.h"

namespace ock {
namespace hcom {
class NetDriverSockWithOOB : public UBSHcomNetDriver {
public:
    NetDriverSockWithOOB(const std::string &name, bool startOobSvr, UBSHcomNetDriverProtocol protocol, SockType t)
        : UBSHcomNetDriver(name, startOobSvr, protocol), mSockType(t)
    {
        OBJ_GC_INCREASE(NetDriverSockWithOOB);
    }

    ~NetDriverSockWithOOB() override
    {
        OBJ_GC_DECREASE(NetDriverSockWithOOB);
    }

    NResult Initialize(const UBSHcomNetDriverOptions &option) override;

    void UnInitialize() override;

    NResult Start() override;
    void Stop() override;

    NResult CreateMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr) override;
    NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr) override;
    NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr, unsigned long memid) override;
    void DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr) override;

    inline NResult ValidateMemoryRegion(uint64_t lKey, uintptr_t address, uint64_t size)
    {
        return mMrChecker.Validate(lKey, address, size);
    }

    NResult Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo,
        uint8_t clientGrpNo) override;

    NResult Connect(const std::string &oobIp, uint16_t oobPort, const std::string &payload, UBSHcomNetEndpointPtr &ep,
        uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx) override;

    NResult Connect(const std::string &serverUrl, const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags,
        uint8_t serverGrpNo = 0, uint8_t clientGrpNo = 0, uint64_t ctx = 0) override;

    NResult MultiRailNewConnection(OOBTCPConnection &conn);
    void DestroyEndpoint(UBSHcomNetEndpointPtr &ep) override;
    void DestroyEndpointById(uint64_t id);
    inline NetMemPoolFixedPtr GetOpCtxMemPool()
    {
        return mOpCtxMemPool;
    }
    inline NetMemPoolFixedPtr GetSglCtxMemPool()
    {
        return mSglCtxMemPool;
    }

protected:
    NResult ValidateOptions();
    NResult CreateWorkers();
    void ClearWorkers();
    void UnInitializeInner();
    NResult HandleSockError(Sock *sock);

    NResult HandleNewOobConn(OOBTCPConnection &conn);
    NResult HandleNewRequest(SockOpContextInfo &ctx);
    NResult HandleReqPosted(SockOpContextInfo *ctx);
    NResult OneSideDone(SockOpContextInfo *ctx);
    NResult HandleEpClose(Sock *sock);

    NResult Connect(const OOBTCPClientPtr &client, const std::string &payload, UBSHcomNetEndpointPtr &outEp,
       uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx);
    NResult ConnectSyncEp(const OOBTCPClientPtr &client, const std::string &payload, UBSHcomNetEndpointPtr &outEp,
        uint8_t serverGrpNo, uint64_t ctx);

    inline bool Remove(uint64_t id)
    {
        std::lock_guard<std::mutex> guard(mEndPointsMutex);
        return (mEndPoints.erase(id) > 0);
    }

    inline void AddEp(const UBSHcomNetEndpointPtr &newEp)
    {
        /* added into map */
        if (NN_LIKELY(newEp != nullptr)) {
            std::lock_guard<std::mutex> guard(mEndPointsMutex);
            mEndPoints.emplace(newEp->Id(), newEp);
        }
    }

    static inline ConnectResp GetConnResp(SockType t)
    {
        switch (t) {
            case SOCK_TCP:
                return OK_PROTOCOL_TCP;
            case SOCK_UDS:
                return OK_PROTOCOL_UDS;
            default:
                return OK;
        }
    }

protected:
    SockType mSockType = SockType::SOCK_TCP;
    std::vector<SockWorker *> mWorkers;
    std::vector<std::string> mFilteredIps;
    MemoryRegionChecker mMrChecker;
    NormalMemoryRegionFixedBuffer *mSockDriverSendMR = nullptr;

    NResult CreateWorkerResource();
    NResult CreateOpCtxMemPool();
    NResult CreateSglCtxMemPool();
    NResult CreateHeaderReqMemPool();
    NResult CreateSendMr();

    NResult HandleSockRealConnect(SockOpContextInfo &ctx);

    NetMemPoolFixedPtr mOpCtxMemPool = nullptr;
    NetMemPoolFixedPtr mSglCtxMemPool = nullptr;
    NetMemPoolFixedPtr mHeaderReqMemPool = nullptr;

    friend class NetAsyncEndpointSock;
    friend class NetSyncEndpointSock;
};
}
}

#endif // OCK_HCOM_NET_SOCK_DRIVER_OOB_H_234234
