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

#ifndef HCOM_NET_UB_DRIVER_H
#define HCOM_NET_UB_DRIVER_H
#ifdef UB_BUILD_ENABLED

#include <map>
#include <mutex>

#include "hcom.h"
#include "net_common.h"
#include "ub_device_helper.h"
#include "net_mem_pool_fixed.h"

namespace ock {
namespace hcom {
class UBWorker;
class NetDriverUB : public UBSHcomNetDriver {
public:
    NetDriverUB(const std::string &name, bool isServer, UBSHcomNetDriverProtocol protocol)
        : UBSHcomNetDriver(name, isServer, protocol)
    {
        OBJ_GC_INCREASE(NetDriverUB);
    }

    ~NetDriverUB() override
    {
        OBJ_GC_DECREASE(NetDriverUB);
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
        return NN_OK;
    }

    inline NResult GetTseg(uint64_t lKey, urma_target_seg_t *&tseg)
    {
        std::lock_guard<std::mutex> locker(mLockTseg);
        auto it = mMapTseg.find(lKey);
        if (it == mMapTseg.end()) {
            NN_LOG_ERROR("Failed to get tseg by lkey: " << lKey);
            return UB_PARAM_INVALID;
        }

        tseg = it->second;
        return NN_OK;
    }

    void DestroyEndpoint(UBSHcomNetEndpointPtr &ep) override;

protected:
    NResult ValidateOptions();
    NResult ValidaQpQueueSizeOptions();
    NResult CreateContext();
    NResult CreateWorkers();
    NResult GetDeviceByIp(UBEId &tmpEid);
    NResult GetDeviceByEid(UBEId &tmpEid);
    NResult GetDeviceByName(UBEId &tmpEid);
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
    UBContext *mContext = nullptr;
    std::vector<UBWorker *> mWorkers;
    UBMemoryRegionFixedBuffer *mDriverSendMR = nullptr;
    MemoryRegionChecker mMrChecker;
    uint32_t mHeartBeatIdleTime = NN_NO8;
    uint32_t mHeartBeatProbeInterval = NN_NO1;
private:
    NResult CreateSendMr(uint8_t slave);
    NResult ImportRemotePA(unsigned long memid);
    NResult CreateOpCtxMemPool();
    NResult CreateSglCtxMemPool();
    NResult CreateWorkerResource();
    NetMemPoolFixedPtr mOpCtxMemPool = nullptr;
    NetMemPoolFixedPtr mSglCtxMemPool = nullptr;
    std::map<uint64_t, urma_target_seg_t *> mMapTseg;
    std::mutex mLockTseg;
};
}
}

#endif
#endif // HCOM_NET_UB_DRIVER_H
