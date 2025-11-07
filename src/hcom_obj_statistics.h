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
#ifndef OCK_HCOM_NET_OBJ_STATISTICS_H
#define OCK_HCOM_NET_OBJ_STATISTICS_H

// macro object statistic
#ifdef ENABLE_OBJ_GLOBAL_STATISTICS
/* declare variable in NetObjStatistic */
#define DECLARE_OBJ_GC(CLASSNAME) static int32_t GC##CLASSNAME
/* initialize the variable define in NetObjStatistic */
#define DEFINE_OBJ_GC(CLASSNAME) int32_t NetObjStatistic::GC##CLASSNAME = 0
/* increase the object count, which should be added into constructor */
#define OBJ_GC_INCREASE(CLASSNAME) __sync_fetch_and_add(&NetObjStatistic::GC##CLASSNAME, 1)
/* decrease the object count, which should be added into destructor */
#define OBJ_GC_DECREASE(CLASSNAME) __sync_sub_and_fetch(&NetObjStatistic::GC##CLASSNAME, 1)
/* dump object count, which should be in dump function */
#define OBJ_GC_DUMP(CLASSNAME) ossDump << "\t" << #CLASSNAME << ": " << GC##CLASSNAME << "\n"
#else
#define DECLARE_OBJ_GC(CLASSNAME)
#define DEFINE_OBJ_GC(CLASSNAME)
#define OBJ_GC_INCREASE(CLASSNAME)
#define OBJ_GC_DECREASE(CLASSNAME)
#define OBJ_GC_DUMP(CLASSNAME)
#endif

namespace ock {
namespace hcom {
class NetObjStatistic {
public:
    DECLARE_OBJ_GC(NetService);
    DECLARE_OBJ_GC(UBSHcomService);
    DECLARE_OBJ_GC(HcomServiceImp);
    DECLARE_OBJ_GC(NetServiceDefaultImp);
    DECLARE_OBJ_GC(NetServiceMultiRailImp);
    DECLARE_OBJ_GC(ServiceNetDriverManager);
    DECLARE_OBJ_GC(ServiceDriverManagerOob);
    DECLARE_OBJ_GC(NetChannel);
    DECLARE_OBJ_GC(UBSHcomChannel);
    DECLARE_OBJ_GC(HcomChannelImp);
    DECLARE_OBJ_GC(MultiRailNetChannel);
    DECLARE_OBJ_GC(NetPeriodicManager);
    DECLARE_OBJ_GC(HcomPeriodicManager);
    DECLARE_OBJ_GC(NetMemPoolFixed);
    DECLARE_OBJ_GC(NetServiceCtxStore);
    DECLARE_OBJ_GC(HcomServiceCtxStore);
    DECLARE_OBJ_GC(NetServiceTimer);
    DECLARE_OBJ_GC(HcomServiceTimer);

    DECLARE_OBJ_GC(UBSHcomNetDriver);
    DECLARE_OBJ_GC(UBSHcomNetEndpoint);
    DECLARE_OBJ_GC(UBSHcomNetMessage);
    DECLARE_OBJ_GC(UBSHcomNetRequestContext);
    DECLARE_OBJ_GC(UBSHcomNetResponseContext);

    DECLARE_OBJ_GC(RDMAWorker);
    DECLARE_OBJ_GC(RDMAEndpoint);
    DECLARE_OBJ_GC(RDMACq);
    DECLARE_OBJ_GC(RDMAContext);
    DECLARE_OBJ_GC(RDMAQp);
    DECLARE_OBJ_GC(RDMAAsyncEndPoint);
    DECLARE_OBJ_GC(RDMASyncEndpoint);
    DECLARE_OBJ_GC(RDMAMemoryRegion);
    DECLARE_OBJ_GC(RDMAMemoryRegionFixedBuffer);
    DECLARE_OBJ_GC(NetDriverRDMA);
    DECLARE_OBJ_GC(NetDriverRDMAWithOob);
    DECLARE_OBJ_GC(NetAsyncEndpoint);
    DECLARE_OBJ_GC(NetSyncEndpoint);

#ifdef UB_BUILD_ENABLED
    DECLARE_OBJ_GC(UBContext);
    DECLARE_OBJ_GC(UBWorker);
    DECLARE_OBJ_GC(NetDriverUB);
    DECLARE_OBJ_GC(NetDriverUBWithOob);
    DECLARE_OBJ_GC(NetUBAsyncEndpoint);
    DECLARE_OBJ_GC(NetUBSyncEndpoint);
    DECLARE_OBJ_GC(UBJfc);
    DECLARE_OBJ_GC(UBJetty);
    DECLARE_OBJ_GC(UBPublicJetty);
    DECLARE_OBJ_GC(UBMemoryRegion);
    DECLARE_OBJ_GC(UBMemoryRegionFixedBuffer);
#endif

    DECLARE_OBJ_GC(NetDriverSockWithOOB);
    DECLARE_OBJ_GC(NetAsyncEndpointSock);
    DECLARE_OBJ_GC(NetSyncEndpointSock);
    DECLARE_OBJ_GC(SockWorker);
    DECLARE_OBJ_GC(SockBuff);
    DECLARE_OBJ_GC(Sock);

    DECLARE_OBJ_GC(NetDriverShmWithOOB);
    DECLARE_OBJ_GC(NetAsyncEndpointShm);
    DECLARE_OBJ_GC(NetSyncEndpointShm);
    DECLARE_OBJ_GC(ShmChannel);
    DECLARE_OBJ_GC(ShmChannelKeeper);
    DECLARE_OBJ_GC(ShmDataChannel);
    DECLARE_OBJ_GC(ShmHandle);
    DECLARE_OBJ_GC(ShmMemoryRegion);
    DECLARE_OBJ_GC(ShmQueue);
    DECLARE_OBJ_GC(ShmWorker);
    DECLARE_OBJ_GC(ShmSyncEndpoint);

    static void Dump();
};
}
}

#endif // OCK_HCOM_NET_OBJ_STATISTICS_H
