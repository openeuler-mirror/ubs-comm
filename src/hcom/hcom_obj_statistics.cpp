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
#include "hcom.h"
#include "hcom_obj_statistics.h"

namespace ock {
namespace hcom {
DEFINE_OBJ_GC(NetService);
DEFINE_OBJ_GC(UBSHcomService);
DEFINE_OBJ_GC(HcomServiceImp);
DEFINE_OBJ_GC(NetServiceDefaultImp);
DEFINE_OBJ_GC(NetChannel);
DEFINE_OBJ_GC(UBSHcomChannel);
DEFINE_OBJ_GC(HcomChannelImp);
DEFINE_OBJ_GC(NetPeriodicManager);
DEFINE_OBJ_GC(HcomPeriodicManager);
DEFINE_OBJ_GC(Publisher);
DEFINE_OBJ_GC(MultiCastPeriodicManager);
DEFINE_OBJ_GC(MultiCastServiceTimer);
DEFINE_OBJ_GC(NetMemPoolFixed);
DEFINE_OBJ_GC(NetServiceCtxStore);
DEFINE_OBJ_GC(HcomServiceCtxStore);
DEFINE_OBJ_GC(NetServiceTimer);
DEFINE_OBJ_GC(HcomServiceTimer);

DEFINE_OBJ_GC(UBSHcomNetDriver);
DEFINE_OBJ_GC(UBSHcomNetEndpoint);
DEFINE_OBJ_GC(UBSHcomNetMessage);
DEFINE_OBJ_GC(UBSHcomNetRequestContext);
DEFINE_OBJ_GC(UBSHcomNetResponseContext);

DEFINE_OBJ_GC(RDMAWorker);
DEFINE_OBJ_GC(RDMAEndpoint);
DEFINE_OBJ_GC(RDMACq);
DEFINE_OBJ_GC(RDMAContext);
DEFINE_OBJ_GC(RDMAQp);
DEFINE_OBJ_GC(RDMAAsyncEndPoint);
DEFINE_OBJ_GC(RDMASyncEndpoint);
DEFINE_OBJ_GC(RDMAMemoryRegion);
DEFINE_OBJ_GC(RDMAMemoryRegionFixedBuffer);
DEFINE_OBJ_GC(NetDriverRDMA);
DEFINE_OBJ_GC(NetDriverRDMAWithOob);
DEFINE_OBJ_GC(NetAsyncEndpoint);
DEFINE_OBJ_GC(NetSyncEndpoint);

#ifdef UB_BUILD_ENABLED
DEFINE_OBJ_GC(UBContext);
DEFINE_OBJ_GC(UBWorker);
DEFINE_OBJ_GC(NetDriverUB);
DEFINE_OBJ_GC(NetDriverUBWithOob);
DEFINE_OBJ_GC(NetUBAsyncEndpoint);
DEFINE_OBJ_GC(NetUBSyncEndpoint);
DEFINE_OBJ_GC(UBJfc);
DEFINE_OBJ_GC(UBJetty);
DEFINE_OBJ_GC(UBPublicJetty);
DEFINE_OBJ_GC(UBMemoryRegion);
DEFINE_OBJ_GC(UBMemoryRegionFixedBuffer);
#endif

DEFINE_OBJ_GC(NetDriverSockWithOOB);
DEFINE_OBJ_GC(NetAsyncEndpointSock);
DEFINE_OBJ_GC(NetSyncEndpointSock);
DEFINE_OBJ_GC(SockWorker);
DEFINE_OBJ_GC(SockBuff);
DEFINE_OBJ_GC(Sock);

DEFINE_OBJ_GC(NetDriverShmWithOOB);
DEFINE_OBJ_GC(NetAsyncEndpointShm);
DEFINE_OBJ_GC(NetSyncEndpointShm);
DEFINE_OBJ_GC(ShmChannel);
DEFINE_OBJ_GC(ShmChannelKeeper);
DEFINE_OBJ_GC(ShmDataChannel);
DEFINE_OBJ_GC(ShmHandle);
DEFINE_OBJ_GC(ShmMemoryRegion);
DEFINE_OBJ_GC(ShmQueue);
DEFINE_OBJ_GC(ShmWorker);
DEFINE_OBJ_GC(ShmSyncEndpoint);

void NetObjStatistic::Dump()
{
    std::ostringstream ossDump;
    ossDump << "Object global count:\n";
#ifdef ENABLE_OBJ_GLOBAL_STATISTICS
    OBJ_GC_DUMP(NetService);
    OBJ_GC_DUMP(NetServiceDefaultImp);
    OBJ_GC_DUMP(NetChannel);
    OBJ_GC_DUMP(NetPeriodicManager);
    OBJ_GC_DUMP(Publisher);
    OBJ_GC_DUMP(MultiCastPeriodicManager);
    OBJ_GC_DUMP(MultiCastServiceTimer);
    OBJ_GC_DUMP(NetMemPoolFixed);
    OBJ_GC_DUMP(NetServiceCtxStore);
    OBJ_GC_DUMP(NetServiceTimer);

    OBJ_GC_DUMP(UBSHcomNetDriver);
    OBJ_GC_DUMP(UBSHcomNetEndpoint);
    OBJ_GC_DUMP(UBSHcomNetMessage);
    OBJ_GC_DUMP(UBSHcomNetRequestContext);
    OBJ_GC_DUMP(UBSHcomNetResponseContext);

    OBJ_GC_DUMP(RDMAWorker);
    OBJ_GC_DUMP(RDMAEndpoint);
    OBJ_GC_DUMP(RDMACq);
    OBJ_GC_DUMP(RDMAContext);
    OBJ_GC_DUMP(RDMAQp);
    OBJ_GC_DUMP(RDMAAsyncEndPoint);
    OBJ_GC_DUMP(RDMASyncEndpoint);
    OBJ_GC_DUMP(RDMAMemoryRegion);
    OBJ_GC_DUMP(RDMAMemoryRegionFixedBuffer);
    OBJ_GC_DUMP(NetDriverRDMA);
    OBJ_GC_DUMP(NetDriverRDMAWithOob);
    OBJ_GC_DUMP(NetAsyncEndpoint);
    OBJ_GC_DUMP(NetSyncEndpoint);

#ifdef UB_BUILD_ENABLED
    OBJ_GC_DUMP(UBContext);
    OBJ_GC_DUMP(UBWorker);
    OBJ_GC_DUMP(NetDriverUB);
    OBJ_GC_DUMP(NetDriverUBWithOob);
    OBJ_GC_DUMP(NetUBAsyncEndpoint);
    OBJ_GC_DUMP(NetUBSyncEndpoint);
    OBJ_GC_DUMP(UBJfc);
    OBJ_GC_DUMP(UBJetty);
    OBJ_GC_DUMP(UBPublicJetty);
    OBJ_GC_DUMP(UBMemoryRegion);
    OBJ_GC_DUMP(UBMemoryRegionFixedBuffer);
#endif

    OBJ_GC_DUMP(NetDriverSockWithOOB);
    OBJ_GC_DUMP(NetAsyncEndpointSock);
    OBJ_GC_DUMP(NetSyncEndpointSock);
    OBJ_GC_DUMP(SockWorker);
    OBJ_GC_DUMP(SockBuff);
    OBJ_GC_DUMP(Sock);

    OBJ_GC_DUMP(NetDriverShmWithOOB);
    OBJ_GC_DUMP(NetAsyncEndpointShm);
    OBJ_GC_DUMP(NetSyncEndpointShm);
    OBJ_GC_DUMP(ShmChannel);
    OBJ_GC_DUMP(ShmChannelKeeper);
    OBJ_GC_DUMP(ShmDataChannel);
    OBJ_GC_DUMP(ShmHandle);
    OBJ_GC_DUMP(ShmMemoryRegion);
    OBJ_GC_DUMP(ShmQueue);
    OBJ_GC_DUMP(ShmWorker);
    OBJ_GC_DUMP(ShmSyncEndpoint);
#else
    ossDump << "\tDisabled";
#endif
    NN_LOG_INFO(ossDump.str());
}
}
}
