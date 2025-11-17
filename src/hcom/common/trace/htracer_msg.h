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
#include "htracer_info.h"
#include "securec.h"
#include "htracer_tdigest.h"
#include "hcom_err.h"
#include "hcom_log.h"

#define TRACE_INFO_MAX_LEN 63

namespace ock {
namespace hcom {

constexpr uint32_t LOG_PATH_LENGTH = 260;

enum MessageOpcode {
    TRACE_OP_PING = 0,
    TRACE_OP_QUERY = 1,
    TRACE_OP_ENABLE_TRACE = 2,
    TRACE_OP_RESET = 3
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
        errno_t ret = strncpy_s(this->name, sizeof(this->name), name,
            std::min(strlen(name), static_cast<size_t>(TRACE_INFO_MAX_LEN)));
        if (ret != EOK) {
            NN_LOG_ERROR("[HTRACER] Failed to strncpy name, err: " << ret);
            this->name[0] = '\0';
        }
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

    TTraceInfo(const TraceInfo &info, double quantile, bool enableTp)
    {
        errno_t ret = strncpy_s(this->name, sizeof(this->name), info.GetName().c_str(),
            std::min(strlen(info.GetName().c_str()), static_cast<size_t>(TRACE_INFO_MAX_LEN)));
        if (ret != EOK) {
            NN_LOG_ERROR("[HTRACER] Failed to strncpy name, err: " << ret);
            this->name[0] = '\0';
            return;
        }
        begin = info.GetBegin();
        goodEnd = info.GetGoodEnd();
        badEnd = info.GetBadEnd();
        min = info.GetMin();
        max = info.GetMax();
        total = info.GetTotal();
        // get latency quantile
        if (enableTp) {
            latencyQuentile = -1.0;
            if (quantile > 0 && quantile < NN_NO100) {
                Tdigest tdigest = info.GetTdigest();
                tdigest.Merge();
                // "/1000" ns -> us
                latencyQuentile = tdigest.Quantile(quantile)/NN_NO1000;
            }
        }
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
        static uint64_t timeUnitStep[TP_TIME_UNIT] = {
            1,
            NN_NO1000,
            NN_NO1000000,
            NN_NO1000000000
        };

        static std::string timeUnitName[TP_TIME_UNIT] = {
            "ns",
            "us",
            "ms",
            "s"
        };
        std::string str;
        std::ostringstream os(str);
        os.flags(std::ios::fixed);
        os.precision(NN_NO3);
        auto unitStep = timeUnitStep[unit];
        auto unitName = timeUnitName[unit];
        os << "[" << std::left << std::setw(NN_NO50) << name << "]"
           << "\t" << std::left << std::setw(NN_NO15) << begin << "\t"
           << std::left << std::setw(NN_NO15) << goodEnd << "\t"
           << std::left << std::setw(NN_NO15) << badEnd << "\t"
           << std::left << std::setw(NN_NO15)
           << ((begin > goodEnd - badEnd) ? (begin - goodEnd - badEnd) : 0)
           << "\t" << std::left << std::setw(NN_NO15)
           << (min == UINT64_MAX ? 0 : ((double)min / unitStep))
           << "\t" << std::left << std::setw(NN_NO15)
           << (double)max / unitStep << "\t" << std::left << std::setw(NN_NO15)
           << (goodEnd == 0 ? 0 : (double)total / goodEnd / unitStep) << "\t"
           << std::left << std::setw(NN_NO15)
           << (double)total / unitStep << "\t" << std::left << std::setw(NN_NO15)
           << (latencyQuentile > 0 ? std::to_string(latencyQuentile) : "OFF");
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
            NN_LOG_WARN("[HTRACER] failed to malloc message data, size:" << messageSize);
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

        message.SetMsg(messageData, messageSize);

        return SER_OK;
    }
};

struct EnableTraceRequest : public MessageHeader {
    bool enable = false;
    bool enableTp = false;
    bool enableLog = false;
    char reserved[1] = {0};
    char logPath[LOG_PATH_LENGTH] = {0};
    EnableTraceRequest() : MessageHeader(TRACE_OP_ENABLE_TRACE) {}
};

struct EnableTraceResponse : public MessageHeader {
    EnableTraceResponse() : MessageHeader(TRACE_OP_ENABLE_TRACE) {}

    static SerCode BuildMessage(Message &message)
    {
        uint32_t messageSize = sizeof(EnableTraceResponse);
        void *messageData = malloc(messageSize);
        if (messageData == nullptr) {
            NN_LOG_WARN("[HTRACER] failed to malloc message data, size:" << messageSize);
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

        message.SetMsg(messageData, messageSize);

        return SER_OK;
    }
};

struct QueryTraceInfoRequest : public MessageHeader {
    uint16_t serviceId = INVALID_SERVICE_ID;
    double quantile = 0;
    QueryTraceInfoRequest() : MessageHeader(TRACE_OP_QUERY) {}
};

struct QueryTraceInfoResponse : public MessageHeader {
    uint32_t traceInfoNum = 0;
    pid_t pid = 0;
    TTraceInfo traceInfo[0];

    QueryTraceInfoResponse() : MessageHeader(TRACE_OP_QUERY) {}

    static SerCode BuildMessage(const std::vector<TTraceInfo> &tTranceInfos, Message &message)
    {
        uint32_t bodySize = sizeof(uint32_t) + sizeof(pid_t) + sizeof(TTraceInfo) * tTranceInfos.size();
        uint32_t messageSize = sizeof(MessageHeader) + bodySize;
        void *messageData = malloc(messageSize);
        if (messageData == nullptr) {
            NN_LOG_WARN("[HTRACER] failed to malloc message data, size:" << messageSize);
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
        message.SetMsg(messageData, messageSize);
        return SER_OK;
    }
};

}
}

#endif // HTRACE_MSG_H
