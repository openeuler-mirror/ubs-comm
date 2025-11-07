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

#ifndef HCOM_UB_COMMON_H
#define HCOM_UB_COMMON_H
#ifdef UB_BUILD_ENABLED

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <strings.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "hcom.h"
#include "hcom_def.h"
#include "hcom_num_def.h"
#include "hcom_log.h"
#include "net_common.h"
#include "net_obj_pool.h"
#include "under_api/urma/urma_api_wrapper.h"

namespace ock {
namespace hcom {
/*
 * return type
 */
using UResult = int;

enum UBCode {
    UB_OK = 0,
    UB_PARAM_INVALID = 200,
    UB_MEMORY_ALLOCATE_FAILED = 201,
    UB_NEW_OBJECT_FAILED = 202,
    UB_OPEN_FILE_FAILED = 203,
    UB_READ_FILE_FAILED = 204,
    UB_DEVICE_FAILED_OPEN = 205,
    UB_DEVICE_INDEX_OVERFLOW = 206,
    UB_DEVICE_OPEN_FAILED = 207,
    UB_DEVICE_FAILED_GET_IP_ADDRESS = 208,
    UB_DEVICE_NO_IP_MATCHED = 209,
    UB_DEVICE_NO_IP_TO_GID_MATCHED = 210,
    UB_DEVICE_INVALID_IP_MASK = 211,
    UB_MR_REG_FAILED = 212,
    UB_CQ_NOT_INITIALIZED = 213,
    UB_CQ_POLLING_FAILED = 214,
    UB_CQ_POLLING_TIMEOUT = 215,
    UB_CQ_POLLING_ERROR_RESULT = 216,
    UB_CQ_POLLING_UNMATCHED_OPCODE = 217,
    UB_CQ_EVENT_GET_FAILED = 218,
    UB_CQ_EVENT_NOTIFY_FAILED = 219,
    UB_CQ_WC_WRONG = 220,
    UB_CQ_EVENT_GET_TIMOUT = 221,
    UB_QP_CREATE_FAILED = 222,
    UB_QP_NOT_INITIALIZED = 223,
    UB_QP_CHANGE_STATE_FAILED = 224,
    UB_QP_POST_RECEIVE_FAILED = 225,
    UB_QP_POST_SEND_FAILED = 226,
    UB_QP_POST_READ_FAILED = 227,
    UB_QP_POST_WRITE_FAILED = 228,
    UB_QP_RECEIVE_CONFIG_ERR = 229,
    UB_QP_POST_SEND_WR_FULL = 230,
    UB_QP_ONE_SIDE_WR_FULL = 231,
    UB_QP_CTX_FULL = 232,
    UB_QP_CHANGE_ERR = 233,
    UB_QP_IMPORT_FAILED = 234,
    UB_QP_BIND_FAILED = 235,
    UB_EP_NOT_INITIALIZED = 236,
    UB_WORKER_NOT_INITIALIZED = 237,
    UB_WORKER_BIND_CPU_FAILED = 238,
    UB_WORKER_REQUEST_HANDLER_NOT_SET = 239,
    UB_WORKER_SEND_POSTED_HANDLER_NOT_SET = 240,
    UB_WORKER_ONE_SIDE_DONE_HANDLER_NOT_SET = 241,
    UB_WORKER_FAILED_ADD_QP = 242,
    UB_ERROR = 243,
};

/* constant variable */
constexpr uint32_t TARGET_JETTY_ID_OFFSET = NN_NO10000;
constexpr uint32_t JETTY_MAX_SEND_WR = NN_NO256;
constexpr uint32_t JETTY_MAX_RECV_WR = NN_NO256;
constexpr uint32_t JETTY_MIN_RNR_TIMER = NN_NO12;
constexpr uint32_t JETTY_TIMEOUT = NN_NO14;
constexpr uint32_t JETTY_RETRY_COUNT = NN_NO7;
constexpr uint32_t JETTY_RNR_RETRY = NN_NO7;
constexpr uint32_t JFC_COUNT = NN_NO1024;

/*
 * class forward declaration
 */
class UBMemoryRegionFixedBuffer;
class NetDriverUB;
class UBFixedMemPool;

// verbs wrappers
class UBDeviceHelper;
class UBContext;
class UBJetty;
class UBJfc;
class UBMemoryRegion;

// logic part
class UBWorker;

// oob for qp setup
class OOBTCPConnection;
class OOBTCPServer;
class OOBTCPClient;

using UBSendSglInlineHeader = UBSHcomNetTransHeader;
using UBSendReadWriteRequest = UBSHcomNetTransRequest;
using UBSendSglRWRequest = UBSHcomNetTransSglRequest;

// the size of UBOpContextInfo is 64 bytes which fit to single CPU cache line
struct UBOpContextInfo {
    enum OpType : uint8_t {
        SEND = 0,
        SEND_RAW = 1,
        SEND_RAW_SGL = 2,
        RECEIVE = 3,
        RECEIVE_RAW = 4,
        WRITE = 5,
        READ = 6,
        SGL_WRITE = 7,
        SGL_READ = 8,
        HB_WRITE = 9,
        SEND_SGL_INLINE = 10,
    };

    enum OpResultType : uint8_t {
        SUCCESS = 0,
        ERR_TIMEOUT = 1,
        ERR_CANCELED = 2,
        ERR_IO_ERROR = 3,
        ERR_EP_BROKEN = 4,
        ERR_EP_CLOSE = 5,
        ERR_ACCESS_ABRT = 6,
        ERR_ACK_TIMEOUT = 7,

        INVALID_MAGIC = 0xFF,
    };

    struct UBOpContextInfo *prev = nullptr;            /* link to prev context */
    struct UBOpContextInfo *next = nullptr;            /* link to next context */
    UBJetty *ubJetty = nullptr;                        /* pointer to qp */
    uintptr_t mrMemAddr = 0;                           /* address of the buffer */
    urma_target_seg_t *localSeg;                       /* local target segment */
    uint64_t lKey = 0;                                 /* local key */
    uint32_t dataSize = 0;                             /* actual data size */
    uint32_t qpNum = 0;                                /* qp ID */
    OpType opType = RECEIVE;                           /* op type */
    OpResultType opResultType = OpResultType::SUCCESS; /* op result */
    uint16_t upCtxSize = 0;                            /* up context size stored in upCtx[] */
    char upCtx[NN_NO16] = {};                          /* 16 bytes for upper context */

    bool HasInternalError() const
    {
        switch (opResultType) {
            // 成功不是一个内部错误
            case OpResultType::SUCCESS:
                return false;

            // 超时对用户不可见，例如 TPACK 超时
            case OpResultType::ERR_TIMEOUT:
                return true;

            // 内部错误，hcom 自治
            case OpResultType::ERR_CANCELED:
            case OpResultType::ERR_IO_ERROR:
                return true;

            // 这两个错误码仅当在判定 EP 出错、处理时才会设置，正常通过 CQE 上报的不会是此状态
            case OpResultType::ERR_EP_BROKEN:
            case OpResultType::ERR_EP_CLOSE:
                return true;

            // 远端内存访问失败，可能是远端 RQE 没有准备好
            case OpResultType::ERR_ACCESS_ABRT:
                return false;

            // AckTimeout，可能是远端 RQE 用尽了
            case OpResultType::ERR_ACK_TIMEOUT:
                return false;

            default:
                return true;
        }
    }

    static inline NResult GetNResult(OpResultType opResult)
    {
        switch (opResult) {
            case OpResultType::SUCCESS:
                return NN_OK;
            case OpResultType::ERR_TIMEOUT:
                return NN_MSG_TIMEOUT;
            case OpResultType::ERR_CANCELED:
                return NN_MSG_CANCELED;
            case OpResultType::ERR_EP_BROKEN:
                return NN_EP_BROKEN;
            case OpResultType::ERR_EP_CLOSE:
                return NN_EP_CLOSE;
            case OpResultType::ERR_ACCESS_ABRT:
                return NN_URMA_ACCESS_ABRT;
            case OpResultType::ERR_ACK_TIMEOUT:
                return NN_URMA_ACK_TIMEOUT;
            default:
                return NN_MSG_ERROR;
        }
    }

    static inline OpResultType OpResult(urma_cr_t &result)
    {
        switch (result.status) {
            case URMA_CR_SUCCESS:
                return OpResultType::SUCCESS;
            case URMA_CR_ACK_TIMEOUT_ERR:
                return OpResultType::ERR_ACK_TIMEOUT;
            case URMA_CR_RNR_RETRY_CNT_EXC_ERR:
                return OpResultType::ERR_TIMEOUT;
            case URMA_CR_WR_FLUSH_ERR:
            case URMA_CR_WR_FLUSH_ERR_DONE:
            case URMA_CR_WR_SUSPEND_DONE:
                return OpResultType::ERR_CANCELED;
            case URMA_CR_REM_ACCESS_ABORT_ERR:
                return OpResultType::ERR_ACCESS_ABRT;
            default:
                NN_LOG_ERROR("Operation result: " << static_cast<int>(result.status));
                return OpResultType::ERR_IO_ERROR;
        }
    }
} __attribute__((packed));

struct UBSglContextInfo {
    UBJetty *qp = nullptr; // the qp pointer which posted from
    UBSHcomNetTransSgeIov iov[NET_SGE_MAX_IOV] = {};
    NResult result = NN_OK;
    uint32_t reserve1 = 0;
    uint16_t refCount = 0; // equal to iovCount
    uint16_t iovCount = 0; // max count:NN_NO16
    uint16_t upCtxSize = 0;
    uint16_t reserve2 = 0;
    char upCtx[NN_NO16] = {}; // 16 bytes for upper context
} __attribute__((packed));

struct UBSgeCtxInfo {
    UBSglContextInfo *ctx = nullptr;
    uint16_t idx = 0;

    UBSgeCtxInfo() = default;
    explicit UBSgeCtxInfo(UBSglContextInfo *sglCtx) : ctx(sglCtx) {}
} __attribute__((packed));

enum UBPollingMode : uint8_t {
    UB_BUSY_POLLING = 0,
    UB_EVENT_POLLING = 1,
};

struct JettyOptions {
    uint32_t maxSendWr = JETTY_MAX_SEND_WR;
    uint32_t maxReceiveWr = JETTY_MAX_RECV_WR;
    uint32_t mrSegSize = NN_NO1024;
    uint32_t mrSegCount = NN_NO64;
    uint8_t slave = 1;
    UBSHcomUbcMode ubcMode = UBSHcomUbcMode::LowLatency;

    JettyOptions() = default;

    JettyOptions(uint32_t maxSendWrNum, uint32_t maxReceiveWrNum, uint32_t segSize, uint32_t segCount, uint8_t slave,
                 UBSHcomUbcMode mode = UBSHcomUbcMode::LowLatency)
        : maxSendWr(maxSendWrNum),
          maxReceiveWr(maxReceiveWrNum),
          mrSegSize(segSize),
          mrSegCount(segCount),
          slave(slave),
          ubcMode(mode)
    {
    }
} __attribute__((packed));

struct UBVaSge {
    uint64_t va;
    int fd;
    urma_target_seg_t *targetSeg = nullptr;
    urma_target_seg_t *dstSeg = nullptr;
};
}
}
#endif
#endif // HCOM_UB_COMMON_H
