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
#include "umq_types.h"

#define CLI_LOG(fmt, ...) \
do { \
    printf("[%s:%d %s] " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
} while (0)

namespace Statistics {
enum class CLICommand : uint8_t {
    INVALID = 0,
    TOPO = 1,
    STAT = 2
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

    void Reset()
    {
        mCmdId = CLICommand::INVALID;
        mErrorCode = CLIErrorCode::OK;
        mDataSize = 0;
    }
};

struct __attribute__((packed)) CLIDataHeader {
    uint32_t socketNum;
    uint32_t connNum;
    uint32_t acceptNum;
};

struct __attribute__((packed)) CLISocketData {
    uint64_t socketId;
    uint64_t recvPackets;
    uint64_t sendPackets;
    uint64_t recvBytes;
    uint64_t sendBytes;
    uint64_t errorPackets;
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