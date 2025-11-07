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
#ifndef OCK_HCOM_SOCK_COMMON_H_2344
#define OCK_HCOM_SOCK_COMMON_H_2344

#include <arpa/inet.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <fcntl.h>
#include <mutex>
#include <netinet/tcp.h>
#include <string>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <thread>
#include <unordered_map>
#include <unistd.h>
#include "hcom.h"
#include "hcom_def.h"
#include "hcom_log.h"
#include "hcom_utils.h"

namespace ock {
namespace hcom {
/* pre-declare classes */
class SockBuff;
class SockBuffList;
class Sock;
class SockWorker;

using SockPtr = NetRef<Sock>;

using SockTransHeader = UBSHcomNetTransHeader;

enum SockType : uint8_t {
    SOCK_UDS = 0,     /* uds as transfer protocol */
    SOCK_TCP = 1,     /* tcp as transfer protocol */
    SOCK_UDS_TCP = 2, /* both tcp and uds, if local host use uds, otherwise use tcp */
};

inline const std::string &SockTypeToString(SockType v)
{
    static const std::string STRINGS[NN_NO3] = {"UDS", "TCP", "UDS&TCP"};
    return STRINGS[v];
}

struct SockOptions {
    uint16_t receiveBufSizeKB = NN_NO1; /* default receive buffer size, 1KB */
    uint16_t sendBufSizeKB = 0;         /* default send buffer size, 0KB */
    uint16_t sendQueueSize = NN_NO256;  /* send queue size */
    bool sendZCopy = false;             /* whether copy send request */
};

struct SockWorkerOptions {
    uint32_t pollingTimeoutMs = NN_NO500;     /* epoll or poll timeout */
    uint16_t pollingBatchSize = NN_NO16;      /* epoll or poll batch size */
    int16_t cpuId = -1;                       /* cpu id to bind */
    bool isServer = false;                    /* is serve or not */
    uint16_t sockReceiveBufKB = 0;            /* socket receive buffer in kernel */
    uint16_t sockSendBufKB = 0;               /* socket send buffer in kernel */
    uint32_t keepaliveIdleTime = NN_NO64;     /* idle 5 seconds to start to probe */
    uint32_t keepaliveProbeTimes = NN_NO7;    /* probe times */
    uint32_t keepaliveProbeInterval = NN_NO2; /* probe interval, in second */
    uint32_t sendQueueSize = NN_NO256;
    /* worker thread priority [-20,20], 20 is the lowest, -20 is the highest, 0 (default) means do not set priority */
    int threadPriority = 0;
    /* timeout during io (s), it should be [-1, 1024], -1 means do not set */
    int tcpUserTimeout = -1;
    bool tcpEnableNoDelay = true;
    bool tcpSendZCopy = false;

    inline std::string ToString() const
    {
        std::ostringstream oss;
        oss << "options polling-timeout-us: " << pollingTimeoutMs << "us, polling-batch-size: " << pollingBatchSize <<
            ", is-server: " << isServer << ", recv-buf-size: " << sockReceiveBufKB << "KB, send-buf-size: " <<
            sockSendBufKB << "KB, keepalive-idle-time: " << keepaliveIdleTime << "s, keepalive-probe-times: " <<
            keepaliveProbeTimes << ", keepalive-probe-interval: " << keepaliveProbeInterval << "s";
        return oss.str();
    }

    inline std::string ToShortString() const
    {
        std::ostringstream oss;
        oss << "options polling-timeout-us: " << pollingTimeoutMs << ", polling-batch-size: " << pollingBatchSize;
        return oss.str();
    }

    void SetValue(const UBSHcomNetDriverOptions& opt, bool isStartOobServer)
    {
        pollingTimeoutMs = opt.eventPollingTimeout;
        pollingBatchSize = opt.pollingBatchSize;
        isServer = isStartOobServer;
        sockSendBufKB = opt.tcpSendBufSize;
        sockReceiveBufKB = opt.tcpReceiveBufSize;
        keepaliveIdleTime = opt.heartBeatIdleTime;
        keepaliveProbeTimes = opt.heartBeatProbeTimes;
        keepaliveProbeInterval = opt.heartBeatProbeInterval;
        sendQueueSize = opt.qpSendQueueSize;
        threadPriority = opt.workerThreadPriority;
        tcpUserTimeout = opt.tcpUserTimeout;
        tcpEnableNoDelay = opt.tcpEnableNoDelay;
        tcpSendZCopy = opt.tcpSendZCopy;
    }
};

struct SockSglContextInfo {
    SockTransHeader sendHeader {}; // record header for raw/raw sgl/read/write/
    uint16_t iovCount = 0;         // max count:NET_SGE_MAX_IOV
    UBSHcomNetTransSgeIov iov[NET_SGE_MAX_IOV] = {};

    inline void Clone(SockTransHeader newHeader, UBSHcomNetTransSgeIov *newIov, uint16_t newIovCnt)
    {
        sendHeader = newHeader;
        iovCount = newIovCnt;
        for (uint16_t i = 0; i < iovCount; i++) {
            iov[i] = newIov[i];
        }
    }
} __attribute__((packed));

struct SockHeaderReqInfo {
    SockTransHeader sendHeader{};
    void *request = nullptr;
} __attribute__((packed));

struct SockOpContextInfo {
    enum SockOpType : uint8_t {
        SS_SEND = 0,
        SS_SEND_RAW = 1,
        SS_SEND_RAW_SGL = 2,
        SS_RECEIVE = 3,
        SS_WRITE = 4,
        SS_READ = 5,
        SS_SGL_WRITE = 6,
        SS_SGL_READ = 7,
        SS_WRITE_ACK = 8,
        SS_READ_ACK = 9,
        SS_SGL_WRITE_ACK = 10,
        SS_SGL_READ_ACK = 11,
    };

    enum SockErrorType : uint8_t {
        SS_NO_ERROR = 0,
        SS_OPERATE_FAILURE = 1,
        SS_RESET_BY_PEER = 2,
        SS_OUT_OF_MEM = 3,
        SS_TIMEOUT = 4,
    };

    SockTransHeader *header = nullptr;   /* receive header operation */
    Sock *sock = nullptr;                /* sock */
    uintptr_t dataAddress = 0;           /* receive data address */
    uint32_t dataSize = 0;               /* receive data size */
    SockOpType opType = SS_RECEIVE;      /* receive by default */
    SockErrorType errType = SS_NO_ERROR; /* by default no error */
    uint16_t upCtxSize = 0;              /* up context size */
    char upCtx[NN_NO16] = {};            /* 16 bytes for upper context */
    union {
        void *sendBuff = nullptr;         /* send or send raw: header + req */
        SockSglContextInfo *sendCtx;      /* send sgl, read or write */
        SockHeaderReqInfo *headerRequest; /* send, without copy */
    };
    bool isSent = false;   /* record the sendMsg is sent or not */
    char rsv[NN_NO7] = {}; /* reserve */

    static inline NResult GetNResult(SockErrorType errorType)
    {
        switch (errorType) {
            case SockErrorType::SS_NO_ERROR:
                return NN_OK;
            case SockErrorType::SS_RESET_BY_PEER:
                return NN_EP_CLOSE;
            case SockErrorType::SS_OUT_OF_MEM:
                return NN_MALLOC_FAILED;
            case SockErrorType::SS_TIMEOUT:
                return NN_MSG_TIMEOUT;
            default:
                return NN_MSG_ERROR;
        }
    }
};

using SResult = int32_t;

enum SCode {
    SS_OK = 0,
    SS_ERROR = 400, /* general error */
    SS_PARAM_INVALID = 401,
    SS_MEMORY_ALLOCATE_FAILED = 402,
    SS_NEW_OBJECT_FAILED = 403,
    SS_SOCK_LISTEN_FAILED = 404,
    SS_SOCK_CREATE_FAILED = 405,
    SS_SOCK_DATA_SIZE_UN_MATCHED = 406,
    SS_SOCK_EPOLL_OP_FAILED = 407,
    SS_SOCK_SEND_FAILED = 408,
    SS_SOCK_CONNECT_FAILED = 409,
    SS_TCP_SET_OPTION_FAILED = 410,
    SS_TCP_GET_OPTION_FAILED = 411,
    SS_WORKER_EPOLL_FAILED = 412,
    SS_TCP_RETRY = 413,
    SS_SOCK_SEND_EAGAIN = 414,
    SS_SOCK_ADD_QUEUE_FAILED = 415,
    SS_CTX_FULL = 416,
    SS_OOB_SSL_WRITE_ERROR = 417,
    SS_OOB_SSL_READ_ERROR = 418,
    SS_RESET_BY_PEER = 419,
    SS_SSL_READ_FAILED = 420,
    SS_TIMEOUT = 421,
};

constexpr uint32_t SOCK_CTX_MAP_RESERVATION = 8192;
}
}

#endif // OCK_HCOM_SOCK_COMMON_H_2344
