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
#ifndef OCK_NET_HEARTBEAT_H
#define OCK_NET_HEARTBEAT_H

#include "hcom.h"
#include "net_monotonic.h"

#include <thread>

namespace ock {
namespace hcom {
class NetUBAsyncEndpoint;
class NetDriverUBWithOob;

class NetHeartbeat {
public:
    NetHeartbeat(UBSHcomNetDriver *driver, uint16_t heartBeatIdleTime, uint16_t heartBeatProbeInterval);
    ~NetHeartbeat();
    NResult Start();
    void Stop();

    template <typename T>
    void GetRemoteHbInfo(T &info)
    {
        uint64_t nextOffset = __sync_fetch_and_add(&mRemoteNextOffset, NN_NO4) % mHBRemoteOpMr->Size();
        info.hbAddress = mHBRemoteOpMr->GetAddress() + nextOffset;
        info.hbKey = mHBRemoteOpMr->GetLKey();
        info.hbMrSize = NN_NO4;
    }

    uint16_t GetHbIdleTime()
    {
        return mHeartBeatIdleTime;
    }

private:
    void RunInHbThread();
    void DetectHbState();

    template <typename T, typename T1, typename T2>
    void DetectSingleEpHbState(T *ep, T1 *driver, UBSHcomNetTransRequest &request, T2 opType);

    /// UBC 专用
    template <typename T>
    void DetectSingleEpHbState(NetUBAsyncEndpoint *ep, NetDriverUBWithOob *driver,
        UBSHcomNetTransRequest &request, T opType);

    /// 使用双边心跳，目前hshmem专用
    template <typename T, typename T1>
    void DetectSingleEpHbState(T *ep, T1 *driver);

    void DetectSingleEpHbState(UBSHcomNetTransRequest &request, UBSHcomNetEndpoint *endPoint);

    template <typename T, typename T1, typename T2>
    NResult SendHeartBeat(T *ep, T1 *driver, UBSHcomNetTransRequest &request, T2 opType);
    NResult SendTwoSideHeartBeat(UBSHcomNetEndpoint *endPoint);

    inline uintptr_t GetNextLocalOpHBAddress()
    {
        uint64_t nextOffset = __sync_fetch_and_add(&mLocalNextOffset, NN_NO4) % mHBLocalOpMr->Size();
        return mHBLocalOpMr->GetAddress() + nextOffset;
    }

    inline uint64_t GetLocalOpHBKey() const
    {
        return mHBLocalOpMr->GetLKey();
    }

    static inline uint64_t GetLocalOpHBMrSize()
    {
        return NN_NO4;
    }

    UBSHcomNetDriver *mDriver = nullptr;

    bool mNeedStopHb = false;
    std::thread mHbThread;
    std::atomic<bool> mHBStarted { false };
    UBSHcomNetMemoryRegionPtr mHBLocalOpMr;
    uint64_t mLocalNextOffset = 0;
    UBSHcomNetMemoryRegionPtr mHBRemoteOpMr;
    uint64_t mRemoteNextOffset = 0;
    uint16_t mHeartBeatIdleTime = NN_NO60;
    uint32_t mHeartBeatProbeInterval = NN_NO2000000; // 2s
    uint64_t mTarSec = 0;
    uint64_t mCurrentSec = 0;
};
}
}

#endif // OCK_NET_HEARTBEAT_H
