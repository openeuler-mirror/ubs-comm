/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli message, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#ifndef CLI_MESSAGE_H
#define CLI_MESSAGE_H

#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <sstream>
#include <iomanip>
#include <securec.h>
#include "umq_dfx_types.h"

#define CLI_LOG(fmt, ...) \
do { \
    printf("[%s:%d %s] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
} while (0)

namespace Statistics {
enum class CLICommand : uint8_t {
    INVALID = 0,
    TOPO = 1,
    STAT = 2,
    DELAY = 3,
    FC = 4,
};

enum class CLITypeParam : uint8_t {
    INVALID = 0,
    TRACE_OP_QUERY,
    TRACE_OP_RESET,
    TRACE_OP_ENABLE_TRACE,
};

enum class CLISwitchPosition : uint8_t {
    IS_TRACE_ENABLE = 0,
    IS_LATENCY_QUANTILE_ENABLE,
    IS_TRACE_LOG_ENABLE,
    INVALID = 16
};

enum class CLIErrorCode : uint8_t {
    OK = 0,
    INTERNAL_ERROR = 1
};

struct __attribute__((packed)) CLIControlHeader {
    CLICommand mCmdId;
    CLIErrorCode mErrorCode;
    uint16_t mDataSize;
    umq_eid_t srcEid;
    umq_eid_t dstEid;
    CLITypeParam mType;
    uint16_t mSwitch;
    double mValue;

    void SetSwitch(CLISwitchPosition p, bool enable)
    {
        if (p >= CLISwitchPosition::INVALID) {
            return;
        }
        if (enable) {
            mSwitch |= (1 << (uint8_t)p);
        } else {
            mSwitch &= ~(1 << (uint8_t)p);
        }
    }

    bool GetSwitch(CLISwitchPosition p)
    {
        if (p >= CLISwitchPosition::INVALID) {
            return false;
        }
        return (mSwitch & (1 << (uint8_t)p)) != 0;
    }

    void Reset()
    {
        mCmdId = CLICommand::INVALID;
        mErrorCode = CLIErrorCode::OK;
        mDataSize = 0;
        mType = CLITypeParam::INVALID;
        mSwitch = 0;
        mValue = 0;
    }
};

struct __attribute__((packed)) CLIDataHeader {
    uint32_t socketNum;
    uint32_t connNum;
    uint32_t activeConn;
    uint32_t reTxCount;
};

struct __attribute__((packed)) CLISocketData {
    uint64_t socketId;
    uint64_t createTime;
    char remoteIp[UMQ_IPV6_SIZE];
    uint8_t localEid[UMQ_EID_SIZE];
    uint8_t remoteEid[UMQ_EID_SIZE];
    uint64_t recvPackets;
    uint64_t sendPackets;
    uint64_t recvBytes;
    uint64_t sendBytes;
    uint64_t errorPackets;
    uint64_t lostPackets;
};

struct CLIFlowControlData {
    uint64_t socketId;
    uint64_t createTime;
    umq_flow_control_stats_t umqFlowControlStat;
};

struct __attribute__((packed)) CLIDelayHeader {
    int32_t retCode;
    uint32_t tracePointNum;
    CLIDelayHeader()
    {
        retCode = 0;
        tracePointNum = 0;
    }
};

class CLIMessage {
public:
    CLIMessage() {}
    ~CLIMessage()
    {
        if (mBuf != nullptr) {
            free(mBuf);
            mBuf = nullptr;
        }
    }

    inline uint32_t DataLen() const
    {
        return mDataLen;
    }

    inline void *Data() const
    {
        return mBuf;
    }
    inline bool SetDataLen(uint32_t len)
    {
        if (len > mBufLen) {
            printf("Invalid datalen\n");
            return false;
        }
        mDataLen = len;
        return true;
    }

    uint32_t GetBufLen() const
    {
        return mBufLen;
    }

    void ResetBuf() const
    {
        if (mBuf == nullptr || mBufLen == 0) {
            return;
        }
        memset_s(mBuf, mBufLen, 0, mBufLen);
    }

    bool AllocateIfNeed(uint32_t newSize);

    CLIMessage(const CLIMessage &) = delete;
    CLIMessage(CLIMessage &&) = delete;
    CLIMessage &operator=(const CLIMessage &) = delete;
    CLIMessage &operator=(CLIMessage &&) = delete;

private:
    uint32_t mBufLen = 0;
    uint32_t mDataLen = 0;
    void *mBuf = nullptr;
};
}
#endif