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

#ifndef HTRACE_MSG_H
#define HTRACE_MSG_H

#include <cstring>
#include <ios>
#include <ostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <sstream>
#include <iomanip>
#include "rpc_msg.h"
#include "htracer_log.h"
#include "hcom/hcom_num_def.h"
#include "hcom/hcom_err.h"
#include "securec.h"

#define TRACE_INFO_MAX_LEN 63

using namespace ock::hcom;

constexpr uint32_t LOG_PATH_LENGTH = 260;

enum MessageOpcode {
    TRACE_OP_PING = 0,
    TRACE_OP_QUERY = 1,
    TRACE_OP_ENABLE_TRACE = 2,
    TRACE_OP_RESET = 3
};

struct HandlerConfPara {
    bool enable;    // enable trace
    bool enableTp;  // enable tp
    bool enableLog; // enable log
    char reserved[1];
    char logPath[LOG_PATH_LENGTH];
    HandlerConfPara(bool enable1, bool enable2, bool enable3, const std::string &path)
        : enable(enable1), enableTp(enable2), enableLog(enable3)
    {
        if (path.size() < sizeof(logPath)) {
            strcpy_s(logPath, path.length() + 1, path.c_str());
        }
    }
};

struct TTraceInfo {
    char name[TRACE_INFO_MAX_LEN + 1] = {0};
    uint64_t begin = 0;
    uint64_t goodEnd = 0;
    uint64_t badEnd = 0;
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    uint64_t total = 0;
    double latencyQuentile = 0.0;

    explicit TTraceInfo(const char *name)
    {
        strncpy_s(this->name, TRACE_INFO_MAX_LEN + 1, name, TRACE_INFO_MAX_LEN);
    }

    void operator += (const TTraceInfo &other)
    {
        begin += other.begin;
        goodEnd += other.goodEnd;
        badEnd += other.badEnd;
        if (min >= other.min) {
            min = other.min;
        }
        if (max <= other.max) {
            max = other.max;
        }
        total += other.total;
    }

    enum TracePointTimeUnit {
        NANO_SECOND,
        MICRO_SECOND,
        MILLI_SECOND,
        SECOND,
        TP_TIME_UNIT
    };

    std::string ToString(TracePointTimeUnit unit = MICRO_SECOND) const
    {
        static uint64_t TIME_UNIT_STEP[TP_TIME_UNIT] = {
            1,
            NN_NO1000,
            NN_NO1000000,
            NN_NO1000000000
        };

        static std::string TIME_UNIT_NAME[TP_TIME_UNIT] = {
            "ns",
            "us",
            "ms",
            "s"
        };
        std::string str;
        std::ostringstream os(str);
        os.flags(std::ios::fixed);
        os.precision(NN_NO3);
        auto unitStep = TIME_UNIT_STEP[unit];
        auto unitName = TIME_UNIT_NAME[unit];
        os << "[" << std::left << std::setw(NN_NO50) << name << "]"
           << "\t" << std::left << std::setw(NN_NO15) << begin << "\t" << std::left << std::setw(NN_NO15) << goodEnd <<
            "\t" << std::left << std::setw(NN_NO15) << badEnd << "\t" << std::left << std::setw(NN_NO15) <<
            ((begin > goodEnd - badEnd) ? (begin - goodEnd - badEnd) : 0) << "\t" << std::left << std::setw(NN_NO15) <<
            (min == UINT64_MAX ? 0 : ((double)min / unitStep)) << "\t" << std::left << std::setw(NN_NO15) <<
            (double)max / unitStep << "\t" << std::left << std::setw(NN_NO15) <<
            (goodEnd == 0 ? 0 : (double)total / goodEnd / unitStep) << "\t" << std::left << std::setw(NN_NO15) <<
            (double)total / unitStep << "\t" << std::left << std::setw(NN_NO15) <<
            (latencyQuentile > 0 ? std::to_string(latencyQuentile) : "OFF");
        return os.str();
    }

    static std::string HeaderString()
    {
        std::stringstream ss;
        ss << "\t[" << std::left << std::setw(NN_NO50) << "TP_NAME"
           << "]"
           << "\t" << std::left << std::setw(NN_NO15) << "TOTAL"
           << "\t" << std::left << std::setw(NN_NO15) << "SUCCESS"
           << "\t" << std::left << std::setw(NN_NO15) << "FAILURE"
           << "\t" << std::left << std::setw(NN_NO15) << "UNFINISHED"
           << "\t" << std::left << std::setw(NN_NO15) << "MIN(us)"
           << "\t" << std::left << std::setw(NN_NO15) << "MAX(us)"
           << "\t" << std::left << std::setw(NN_NO15) << "AVG(us)"
           << "\t" << std::left << std::setw(NN_NO15) << "TOTAL(us)"
           << "\t" << std::left << std::setw(NN_NO15) << "TPX(us)";
        return ss.str();
    }
};

struct PingRequest : public MessageHeader {
    PingRequest() : MessageHeader(TRACE_OP_PING) {}
};

struct PingResponse : public MessageHeader {
    PingResponse() : MessageHeader(TRACE_OP_PING) {}
    pid_t pid;

    static SerCode BuildMessage(Message &message)
    {
        uint32_t bodySize = sizeof(pid_t);
        uint32_t messageSize = sizeof(MessageHeader) + bodySize;
        void *messageData = malloc(messageSize);
        auto pingResponse = reinterpret_cast<PingResponse *>(messageData);
        if (messageData == nullptr) {
            LOG_ERR("failed to malloc message data, size:" << messageSize);
            return SER_ERROR;
        }
        pingResponse->version = VERSION;
        pingResponse->magicCode = MAGIC_CODE;
        pingResponse->crc = 0;
        pingResponse->opcode = TRACE_OP_QUERY;
        pingResponse->bodySize = bodySize;
        pingResponse->pid = getpid();
        message.SetData(messageData);
        message.SetSize(messageSize);
        return SER_OK;
    }
};

struct ResetTraceInfoRequest : public MessageHeader {
    ResetTraceInfoRequest() : MessageHeader(TRACE_OP_RESET) {}
};

struct ResetTraceInfoResponse : public MessageHeader {
    ResetTraceInfoResponse() : MessageHeader(TRACE_OP_RESET) {}

    static SerCode BuildMessage(Message &message)
    {
        uint32_t messageSize = sizeof(ResetTraceInfoResponse);
        void *messageData = malloc(messageSize);
        if (messageData == nullptr) {
            LOG_ERR("failed to malloc message data, size:" << messageSize);
            return SER_ERROR;
        }
        bzero(messageData, messageSize);

        // fill message header.
        auto queryResponse = reinterpret_cast<ResetTraceInfoResponse *>(messageData);
        queryResponse->version = VERSION;
        queryResponse->magicCode = MAGIC_CODE;
        queryResponse->crc = 0;
        queryResponse->opcode = TRACE_OP_RESET;
        queryResponse->bodySize = 0;

        message.SetData(messageData);
        message.SetSize(messageSize);

        return SER_OK;
    }
};

struct EnableTraceRequest : public MessageHeader {
    bool enable;
    bool enableTp;
    bool enableLog;
    char reserved[1];
    char logPath[LOG_PATH_LENGTH];
    EnableTraceRequest() : MessageHeader(TRACE_OP_ENABLE_TRACE) {}
};

struct EnableTraceResponse : public MessageHeader {
    EnableTraceResponse() : MessageHeader(TRACE_OP_ENABLE_TRACE) {}

    static SerCode BuildMessage(Message &message)
    {
        uint32_t messageSize = sizeof(EnableTraceResponse);
        void *messageData = malloc(messageSize);
        if (messageData == nullptr) {
            LOG_ERR("failed to malloc message data, size:" << messageSize);
            return SER_ERROR;
        }
        bzero(messageData, messageSize);

        // fill message header.
        auto queryResponse = reinterpret_cast<EnableTraceResponse *>(messageData);
        queryResponse->version = VERSION;
        queryResponse->magicCode = MAGIC_CODE;
        queryResponse->crc = 0;
        queryResponse->opcode = TRACE_OP_ENABLE_TRACE;
        queryResponse->bodySize = 0;

        message.SetData(messageData);
        message.SetSize(messageSize);

        return SER_OK;
    }
};

struct QueryTraceInfoRequest : public MessageHeader {
    uint16_t serviceId;
    double quantile;
    QueryTraceInfoRequest() : MessageHeader(TRACE_OP_QUERY) {}
};

struct QueryTraceInfoResponse : public MessageHeader {
    uint32_t traceInfoNum = 0;
    pid_t pid;
    TTraceInfo traceInfo[0];

    QueryTraceInfoResponse() : MessageHeader(TRACE_OP_QUERY) {}

    static SerCode BuildMessage(const std::vector<TTraceInfo> &tTranceInfos, Message &message)
    {
        uint32_t bodySize = sizeof(uint32_t) + sizeof(pid_t) + sizeof(TTraceInfo) * tTranceInfos.size();
        uint32_t messageSize = sizeof(MessageHeader) + bodySize;
        void *messageData = malloc(messageSize);
        if (messageData == nullptr) {
            LOG_ERR("failed to malloc message data, size:" << messageSize);
            return SER_ERROR;
        }
        bzero(messageData, messageSize);

        // fill message header.
        auto queryResponse = reinterpret_cast<QueryTraceInfoResponse *>(messageData);
        queryResponse->version = VERSION;
        queryResponse->magicCode = MAGIC_CODE;
        queryResponse->crc = 0;
        queryResponse->opcode = TRACE_OP_QUERY;
        queryResponse->bodySize = bodySize;
        queryResponse->pid = getpid();
        queryResponse->traceInfoNum = tTranceInfos.size();

        // file message body.
        int i = 0;
        for (const auto &info : tTranceInfos) {
            queryResponse->traceInfo[i++] = info;
        }
        message.SetData(messageData);
        message.SetSize(messageSize);
        return SER_OK;
    }
};

#endif // HTRACE_MSG_H