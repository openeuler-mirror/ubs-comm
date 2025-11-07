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
#ifndef OCK_HCOM_SHM_COMMON_H
#define OCK_HCOM_SHM_COMMON_H

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <thread>

#include "securec.h"
#include "hcom.h"
#include "net_ctx_info_pool.h"
#include "net_delay_release_timer.h"
#include "net_mem_pool_fixed.h"
#include "shm_lock_guard.h"
#include "shm_mr_handle_map.h"

namespace ock {
namespace hcom {
class ShmHandle;
class ShmDataChannel;
template <typename T> class ShmQueue;
class ShmChannel;
class ShmWorker;
class ShmChannelKeeper;
class ShmSyncEndpoint;
class ShmHandleFds;

using ShmHandlePtr = NetRef<ShmHandle>;
using ShmDataChannelPtr = NetRef<ShmDataChannel>;
using ShmChannelPtr = NetRef<ShmChannel>;
using ShmChannelKeeperPtr = NetRef<ShmChannelKeeper>;
using DelayReleaseTimerPtr = NetRef<NetDelayReleaseTimer>;
using ShmSyncEndpointPtr = NetRef<ShmSyncEndpoint>;

enum ShmPollingMode : uint8_t {
    SHM_EVENT_POLLING = 0,
    SHM_BUSY_POLLING = 1,
};

inline std::string &ShmPollingModeToStr(ShmPollingMode v)
{
    static std::string STRINGS[NN_NO2] = {"event", "busy"};
    return STRINGS[v];
}

/*
 * @brief exchange info for uds
 */
struct ShmConnExchangeInfo {
    char qName[NN_NO32] {};
    char dcName[NN_NO64] {};
    uint64_t channelId = 0;
    int channelFd = 0;
    uintptr_t channelAddress = 0;
    uint32_t qCapacity = 0;
    int queueFd = 0;
    uint32_t dcBuckSize = 0;
    uint32_t dcBuckCount = 0;
    uint16_t payLoadSize = 0;
    ShmPollingMode mode = SHM_EVENT_POLLING;

    inline bool SetQueueName(const std::string &v)
    {
        NN_SET_CHAR_ARRAY_FROM_STRING(qName, v);
    }

    inline std::string GetQueueName() const
    {
        return NN_CHAR_ARRAY_TO_STRING(qName);
    }

    inline bool SetDCName(const std::string &v)
    {
        NN_SET_CHAR_ARRAY_FROM_STRING(dcName, v);
    }

    inline std::string GetDCName() const
    {
        return NN_CHAR_ARRAY_TO_STRING(dcName);
    }

    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << "qName: " << GetQueueName() << ", dcName " << dcName << ", chId " << channelId << ", qCap: " <<
            qCapacity << ", dcBuckSize: " << dcBuckSize << ", dcBuckCnt: " << dcBuckCount;
        return oss.str();
    }
} __attribute__((packed));

using ShmIdleHandler = UBSHcomNetDriverIdleHandler;

/*
 * @brief shm operation context
 * make sure it is 64bits which equal to one cache line of CPU
 */
struct ShmOpContextInfo {
    enum ShmOpType : uint8_t {
        SH_SEND = 0,
        SH_RECEIVE = 1,
        SH_WRITE = 2,
        SH_READ = 3,
        SH_SGL_WRITE = 4,
        SH_SGL_READ = 5,
        SH_SEND_RAW = 6,
        SH_RECEIVE_RAW = 7,
        SH_SEND_RAW_SGL = 8,
    };

    enum ShmErrorType : uint8_t {
        SH_NO_ERROR = 0,
        SH_OPERATE_FAILURE = 1,
        SH_RESET_BY_PEER = 2,
        SH_OUT_OF_MEM = 3,
        SH_TIMEOUT = 4,
    };

    ShmOpContextInfo *prev = nullptr;   /* previous one for bi-direct link */
    ShmOpContextInfo *next = nullptr;   /* next one for bi-direct link */
    ShmChannel *channel = nullptr;      /* shm channel */
    uintptr_t dataAddress = 0;          /* data address */
    uint32_t dataSize = 0;              /* data size */
    uint32_t lKey = 0;                  /* lKey of read write MR */
    uintptr_t mrMemAddr = 0;            /* address of read write MR */
    ShmOpType opType = SH_RECEIVE;      /* receive by default */
    ShmErrorType errType = SH_NO_ERROR; /* by default no error */
    uint16_t upCtxSize = 0;             /* up context size */
    char upCtx[NN_NO16] = {};           /* 16 bytes for upper context */

    ShmOpContextInfo() = default;

    ShmOpContextInfo(ShmChannel *ch, uintptr_t da, uint32_t ds, ShmOpType op, ShmErrorType et)
        : channel(ch), dataAddress(da), dataSize(ds), opType(op), errType(et)
    {}

    static inline NResult GetNResult(ShmErrorType opResult)
    {
        switch (opResult) {
            case ShmErrorType::SH_NO_ERROR:
                return NN_OK;
            case ShmErrorType::SH_TIMEOUT:
                return NN_MSG_TIMEOUT;
            case ShmErrorType::SH_OUT_OF_MEM:
                return NN_MALLOC_FAILED;
            default:
                return NN_MSG_ERROR;
        }
    }
} __attribute__((packed));

struct ShmSglOpContextInfo {
    UBSHcomNetTransSgeIov iov[NET_SGE_MAX_IOV] = {};
    uint16_t iovCount = 0; // max count:NN_NO4
    uint16_t upCtxSize = 0;
    char upCtx[NN_NO16] = {}; // 16 bytes for upper context
    NResult result = NN_OK;
} __attribute__((packed));

struct ShmOpCompInfo {
    UBSHcomNetTransHeader header {};
    ShmChannel *channel = nullptr; /* shm channel */
    UBSHcomNetTransRequest request {};
    uint16_t upCtxSize = 0;        /* up context size */
    char upCtx[NN_NO16] = {};      /* 16 bytes for upper context */
    ShmOpCompInfo *prev = nullptr; /* previous one for bi-direct link */
    ShmOpCompInfo *next = nullptr; /* next one for bi-direct link */
    ShmOpContextInfo::ShmOpType opType = ShmOpContextInfo::ShmOpType::SH_SEND;
    ShmOpContextInfo::ShmErrorType errType = ShmOpContextInfo::ShmErrorType::SH_NO_ERROR; /* by default no error */
} __attribute__((packed));

struct ShmSglOpCompInfo {
    ShmSglOpContextInfo *ctx = nullptr;

    ShmSglOpCompInfo() = default;
    explicit ShmSglOpCompInfo(ShmSglOpContextInfo *sglCtx) : ctx(sglCtx) {}
} __attribute__((packed));

/*
 * @brief Shm event struct
 */
struct ShmEvent {
    uint32_t immData = 0;             /* imm data */
    uint32_t dataSize = 0;            /* size of data */
    uint64_t dataOffset = 0;          /* offset of the data based address of sender */
    uint64_t channelId = 0;           /* sender channel id */
    uint64_t peerChannelId = 0;       /* peer channel id, i.e. receiver channel id */
    uintptr_t peerChannelAddress = 0; /* channel address */
    ShmChannel *shmChannel = nullptr; /* sender ch address */
    ShmOpContextInfo::ShmOpType opType = ShmOpContextInfo::ShmOpType::SH_SEND; /* op type */

    ShmEvent() = default;

    ShmEvent(uint32_t s, uint32_t ds, uint64_t o, uint64_t myId, uint64_t pId, uintptr_t pa, uint8_t op)
        : immData(s),
          dataSize(ds),
          dataOffset(o),
          channelId(myId),
          peerChannelId(pId),
          peerChannelAddress(pa),
          opType(static_cast<ShmOpContextInfo::ShmOpType>(op))
    {}

    ShmEvent(uintptr_t pa, uint8_t op) : peerChannelAddress(pa), opType(static_cast<ShmOpContextInfo::ShmOpType>(op)) {}

    void SetChannel(ShmChannel *channel)
    {
        shmChannel = channel;
    }

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "imm-data " << immData << ", ch-id " << channelId << ", peer-ch-id " << peerChannelId <<
            ", peer-channel-address: " << peerChannelAddress << ", data-offset " << dataOffset << ", data-size: " <<
            dataSize << ", opType: " << opType;
        return oss.str();
    }
};

enum ShmChannelState : uint8_t {
    CH_NEW = 0,
    CH_BROKEN = 1,
};

/*
 * @brief Event queue for both busy polling and event polling
 */
using ShmEventQueue = ShmQueue<ShmEvent>;
using ShmEventQueuePtr = NetRef<ShmEventQueue>;

const std::string SHM_F_EVENT_QUEUE_PREFIX = "hcom-eq";
const std::string SHM_F_DC_PREFIX = "hcom-dc";

using HResult = int32_t;
enum ShCode {
    SH_OK = 0,
    SH_ERROR = 300,
    SH_PARAM_INVALID = 301,
    SH_MEMORY_ALLOCATE_FAILED = 302,
    SH_NEW_OBJECT_FAILED = 303,
    SH_FILE_OP_FAILED = 304,
    SH_NOT_INITIALIZED = 305,
    SH_TIME_OUT = 306,
    SH_OP_CTX_FULL = 307,
    SH_CH_BROKEN = 308,
    SH_CREATE_KEEPER_EPOLL_FAILURE = 309,
    SH_DUP_CH_IN_KEEPER = 310,
    SH_CH_ADD_FAILURE_IN_KEEPER = 311,
    SH_CH_REMOVE_FAILURE_IN_KEEPER = 312,
    SH_RETRY_FULL = 313,
    SH_SEND_COMPLETION_CALLBACK_FAILURE = 314,
    SH_FDS_QUEUE_FULL = 315,
    SH_PEER_FD_ERROR = 316,
    SH_OP_CTX_REMOVED = 317,
};

using ShmOpCompInfoPool = OpContextInfoPool<ShmOpCompInfo>;
using ShmOpContextInfoPool = OpContextInfoPool<ShmOpContextInfo>;
using ShmSglContextInfoPool = OpContextInfoPool<ShmSglOpContextInfo>;
}
}

#endif // OCK_HCOM_SHM_COMMON_H
