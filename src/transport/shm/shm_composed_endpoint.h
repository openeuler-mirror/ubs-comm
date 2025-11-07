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

#ifndef HCOM_SHM_COMPOSED_ENDPOINT_H
#define HCOM_SHM_COMPOSED_ENDPOINT_H

#include "hcom.h"
#include "hcom_def.h"
#include "hcom_obj_statistics.h"
#include "net_monotonic.h"
#include "shm_common.h"
#include "shm_handle.h"
#include "shm_queue.h"
#include "shm_channel.h"
namespace ock {
namespace hcom {
class ShmSyncEndpoint {
public:
    static HResult Create(const std::string &name, uint16_t eventQueueLength, ShmPollingMode mode,
        ShmSyncEndpointPtr &ep);

public:
    ShmSyncEndpoint(const std::string &name, uint16_t eventQueueLength, ShmPollingMode mode)
        : mName(name), mEventQueueLength(eventQueueLength), mShmMode(mode)
    {
        OBJ_GC_INCREASE(ShmSyncEndpoint);
    }

    ~ShmSyncEndpoint()
    {
        if (mEventQueue != nullptr) {
            mEventQueue->DecreaseRef();
            mEventQueue = nullptr;
        }
        OBJ_GC_DECREASE(ShmSyncEndpoint);
    }

    HResult PostSend(ShmChannel *ch, const UBSHcomNetTransRequest &req, uint64_t offset, uint32_t immData,
        int32_t defaultTimeout);

    HResult PostSendRawSgl(ShmChannel *ch, const UBSHcomNetTransRequest &req, const UBSHcomNetTransSglRequest &sglReq,
        uint64_t offset, uint32_t immData, int32_t defaultTimeout);
    HResult PostRead(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap);
    HResult PostRead(ShmChannel *ch, const UBSHcomNetTransSglRequest &req, ShmMRHandleMap &mrHandleMap);
    HResult PostWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap);
    HResult PostWrite(ShmChannel *ch, const UBSHcomNetTransSglRequest &req, ShmMRHandleMap &mrHandleMap);
    HResult Receive(int32_t timeout, ShmOpContextInfo &opCtx, uint32_t &immData);
    HResult DequeueEvent(int32_t timeout, ShmEvent &opEvent);

    inline bool FillQueueExchangeInfo(ShmConnExchangeInfo &info)
    {
        if (NN_UNLIKELY(mEventQueue != nullptr)) {
            info.qCapacity = mEventQueue->Capacity();
        }

        if (NN_LIKELY(mHandleEventQueue.Get() != nullptr)) {
            info.queueFd = mHandleEventQueue->Fd();
            return info.SetQueueName(mHandleEventQueue->FullPath());
        }

        info.mode = mShmMode;
        return false;
    }

    inline std::string GetName()
    {
        return mName;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    HResult CreateEventQueue();
    HResult FillSglCtx(ShmSglOpContextInfo *sglCtx, const UBSHcomNetTransSglRequest &sglReq);
    HResult SendLocalEventForOneSideDone(ShmOpContextInfo *ctx, ShmOpContextInfo::ShmOpType type);
    HResult PostReadWriteSgl(ShmChannel *ch, const UBSHcomNetTransSglRequest &req, ShmMRHandleMap &mrHandleMap,
        ShmOpContextInfo::ShmOpType type);

    HResult PostReadWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req, ShmMRHandleMap &mrHandleMap,
        ShmOpContextInfo::ShmOpType type);

    uint64_t inline GetFinishTime()
    {
        if (mDefaultTimeout > 0) {
            return NetMonotonic::TimeNs() + static_cast<uint64_t>(mDefaultTimeout) * 1000000000UL;
        } else if (mDefaultTimeout < 0) {
            return UINT64_MAX;
        }

        return 0;
    }

    static bool inline NeedRetry(HResult result)
    {
        if (NN_UNLIKELY(result == ShmEventQueue::SHM_QUEUE_FULL)) {
            return true;
        }

        return false;
    }

private:
    std::string mName;
    uint16_t mEventQueueLength = NN_NO2048;
    ShmEventQueue *mEventQueue = nullptr;
    ShmHandlePtr mHandleEventQueue; /* handle of event queue */
    ShmPollingMode mShmMode = SHM_EVENT_POLLING;
    int32_t mDefaultTimeout = -1;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

inline HResult ShmSyncEndpoint::PostRead(ShmChannel *ch, const UBSHcomNetTransRequest &req,
    ShmMRHandleMap &mrHandleMap)
{
    return PostReadWrite(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_READ);
}

inline HResult ShmSyncEndpoint::PostRead(ShmChannel *ch, const UBSHcomNetTransSglRequest &req,
    ShmMRHandleMap &mrHandleMap)
{
    return PostReadWriteSgl(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_SGL_READ);
}

inline HResult ShmSyncEndpoint::PostWrite(ShmChannel *ch, const UBSHcomNetTransRequest &req,
    ShmMRHandleMap &mrHandleMap)
{
    return PostReadWrite(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_WRITE);
}

inline HResult ShmSyncEndpoint::PostWrite(ShmChannel *ch, const UBSHcomNetTransSglRequest &req,
    ShmMRHandleMap &mrHandleMap)
{
    return PostReadWriteSgl(ch, req, mrHandleMap, ShmOpContextInfo::ShmOpType::SH_SGL_WRITE);
}
}
}
#endif // HCOM_SHM_COMPOSED_ENDPOINT_H
