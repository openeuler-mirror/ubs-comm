/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide statistic class
 * Create: 2026
*/

#ifndef UBSOCKET_STATISTICS_H
#define UBSOCKET_STATISTICS_H

#include <sys/time.h>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "ubsocket_logger.h"
#include "under_api/umq_api.h"

namespace Statistics {

class StatsMgr {
public:
    enum trace_stats_type {
        CONN_COUNT,
        ACTIVE_OPEN_COUNT,
        RX_PACKET_COUNT,
        TX_PACKET_COUNT,
        RX_BYTE_COUNT,
        TX_BYTE_COUNT,
        TX_ERROR_PACKET_COUNT,
        TX_LOST_PACKET_COUNT,

        TRACE_STATE_TYPE_MAX
    };

    bool InitStatsMgr()
    {
        m_stats_enable = true;

        return true;
    }

    inline static std::atomic<uint64_t> mConnCount{0};
    inline static std::atomic<uint64_t> mActiveConnCount{0};
    inline static std::atomic<uint64_t> mReTxCount{0};
    inline static std::atomic<uint64_t> mRxPacketCount{0};
    inline static std::atomic<uint64_t> mTxPacketCount{0};
    inline static std::atomic<uint64_t> mRxByteCount{0};
    inline static std::atomic<uint64_t> mTxByteCount{0};
    inline static std::atomic<uint64_t> mTxErrorPacketCount{0};
    inline static std::atomic<uint64_t> mTxLostPacketCount{0};

    static uint64_t GetConnCount()
    {
        return mConnCount.load(std::memory_order_relaxed);
    }

    static uint64_t GetActiveConnCount()
    {
        return mActiveConnCount.load(std::memory_order_relaxed);
    }

    static uint64_t GetReTxCount()
    {
        return mReTxCount.load(std::memory_order_relaxed);
    }

    static ALWAYS_INLINE void OutputAllStats(std::ostringstream &oss, uint32_t pid)
    {
        constexpr int timeBufSize = 32;
        time_t now = time(nullptr);
        char timeBuf[timeBufSize];
        struct tm timeInfo;
        if (localtime_r(&now, &timeInfo) != nullptr) {
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeInfo);
        } else {
            timeBuf[0] = '\0';
            UBS_VLOG_ERR("Failed to create timeStamp.\n");
        }

        oss << "{"
            << "\"timeStamp\":\"" << timeBuf << "\","
            << "\"pid\":\"" << pid << "\","
            << "\"trafficRecords\":{";

        oss << "\"" << "totalConnections" << "\":" << mConnCount.load() << ",";
        oss << "\"" << "activeConnections" << "\":" << mActiveConnCount.load() << ",";
        oss << "\"" << "reTxCount" << "\":" << mReTxCount.load() << ",";
        oss << "\"" << "sendPackets" << "\":" << mTxPacketCount.load() << ",";
        oss << "\"" << "receivePackets" << "\":" << mRxPacketCount.load() << ",";
        oss << "\"" << "sendBytes" << "\":" << mTxByteCount.load() << ",";
        oss << "\"" << "receiveBytes" << "\":" << mRxByteCount.load() << ",";
        oss << "\"" << "errorPackets" << "\":" << mTxErrorPacketCount.load() << ",";
        oss << "\"" << "lostPackets" << "\":" << mTxLostPacketCount.load() << "";

        oss << "}" << "}";
    }

    static void UpdateReTxCount(umq_trans_mode_t umq_trans_mode);

    // data plane interface, caller ensure input validation
    static ALWAYS_INLINE void UpdateTraceStats(enum trace_stats_type type, uint32_t value)
    {
        switch (type) {
            case CONN_COUNT:
                mConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case ACTIVE_OPEN_COUNT:
                mActiveConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case RX_PACKET_COUNT:
                mRxPacketCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case TX_PACKET_COUNT:
                mTxPacketCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case RX_BYTE_COUNT:
                mRxByteCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case TX_BYTE_COUNT:
                mTxByteCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case TX_ERROR_PACKET_COUNT:
                mTxErrorPacketCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case TX_LOST_PACKET_COUNT:
                mTxLostPacketCount.fetch_add(value, std::memory_order_relaxed);
                break;

            default:
                break;
        }
    }

    static ALWAYS_INLINE void SubMConnCount()
    {
        if (mConnCount.load() >= 1) {
            mConnCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    static ALWAYS_INLINE void SubMActiveConnCount()
    {
        if (mActiveConnCount.load() >= 1) {
            mActiveConnCount.fetch_sub(1, std::memory_order_relaxed);
        }
    }

protected:
    const char *GetStatsStr(enum trace_stats_type type)
    {
        const static char *state_type_str[TRACE_STATE_TYPE_MAX] = {
            "totalConnections", "activeConnections", "sendPackets",  "receivePackets",
            "sendBytes",        "receiveBytes",      "errorPackets", "lostPackets",
        };

        return state_type_str[type];
    }

    void OutputStats(std::ostringstream &oss)
    {
        if (!m_stats_enable) {
            return;
        }
    }

    int m_output_fd = -1;
    bool m_stats_enable = false;
};

}; // namespace Statistics

#endif
