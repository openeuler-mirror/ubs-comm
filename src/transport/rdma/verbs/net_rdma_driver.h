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
#ifndef OCK_NET_DRIVER_RDMA_123423434341233_H
#define OCK_NET_DRIVER_RDMA_123423434341233_H
#ifdef RDMA_BUILD_ENABLED

#include <map>
#include <mutex>

#include "hcom.h"
#include "net_common.h"
#include "rdma_worker.h"

namespace ock {
namespace hcom {
class NetDriverRDMA : public UBSHcomNetDriver {
public:
    NetDriverRDMA(const std::string &name, bool isServer, UBSHcomNetDriverProtocol protocol)
        : UBSHcomNetDriver(name, isServer, protocol)
    {
        OBJ_GC_INCREASE(NetDriverRDMA);
    }

    ~NetDriverRDMA() override
    {
        OBJ_GC_DECREASE(NetDriverRDMA);
    }

    NResult Initialize(const UBSHcomNetDriverOptions &option) override;

    void UnInitialize() override;

    NResult Start() override;
    void Stop() override;

    NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr) override;
    NResult CreateMemoryRegion(uintptr_t address, uint64_t size, UBSHcomNetMemoryRegionPtr &mr) override;
    NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr, unsigned long memid) override;
    void DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr) override;

    inline NResult ValidateMemoryRegion(uint64_t lKey, uintptr_t address, uint64_t size)
    {
        return mMrChecker.Validate(lKey, address, size);
    }

    void DestroyEndpoint(UBSHcomNetEndpointPtr &ep) override;

    inline RDMAMemoryRegionFixedBuffer *GetDriverSendMr() const
    {
        return mDriverSendMR;
    }

protected:
    NResult ValidateOptions();
    NResult CreateContext();
    NResult CreateWorkers();
    void ClearWorkers();
    void UnInitializeInner();
    virtual NResult DoInitialize()
    {
        return NN_OK;
    }

    virtual void DoUnInitialize() {}

    virtual NResult DoStart()
    {
        return NN_OK;
    }

    virtual void DoStop() {}

protected:
    std::string mMatchIp;
    RDMAContext *mContext = nullptr;
    std::vector<RDMAWorker *> mWorkers;
    RDMAMemoryRegionFixedBuffer *mDriverSendMR = nullptr;
    MemoryRegionChecker mMrChecker;
    uint32_t mHeartBeatIdleTime = NN_NO8;
    uint32_t mHeartBeatProbeInterval = NN_NO1;

private:
    NResult CreateSendMr();
    NResult CreateOpCtxMemPool();
    NResult CreateSglCtxMemPool();
    NResult CreateWorkerResource();
    NResult MatchIpByMask(std::vector<std::string> &matchIps);
    NetMemPoolFixedPtr mOpCtxMemPool = nullptr;
    NetMemPoolFixedPtr mSglCtxMemPool = nullptr;
};
}
}

#endif
#endif // _OCK_NET_DRIVER_RDMA_123423434341233_H
