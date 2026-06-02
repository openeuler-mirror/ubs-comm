/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef STATISTICS_STATSMGR_H
#define STATISTICS_STATSMGR_H

#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "cli_message.h"
#include "common/ubsocket_logger.h"
#include "under_api/umq_api.h"

#define RPC_VAR (2)

namespace Statistics {
class Recorder {
public:
    const static uint32_t NAME_WIDTH_MAX = 30;
    const static uint32_t FIELD_WIDTH_MAX = 21;
    const static uint32_t FD_WIDTH_MAX = 10;
    const static uint32_t DEFAULT_PRECISION = 3;
    const static uint32_t TOTAL_PRECISION = 7;

    Recorder(const char *name)
    {
        if (name == nullptr) {
            throw std::runtime_error("Input invalid name");
        }

        m_name = name;
        if (m_name.length() > NAME_WIDTH_MAX) {
            throw std::runtime_error("Input name length(" + std::to_string(m_name.length()) + ") exceeds upper limit(" +
                                     std::to_string(NAME_WIDTH_MAX) + ")");
        }
    }

    ALWAYS_INLINE void Update(uint32_t input)
    {
        /* Here use Welford algorithm to calculate mean and variance
         * (1) mean update equation: M(new) = M(old) + (x(new) - M(old)) / n
         * (2) variance update equation: M2(new) = M2(old) + (x(new) - M(old)) * (x(new) - M(new))
         * (3) sample variance: s^2 = M2 / (n - 1)
         * M: mean of inputsamples
         * x: new input
         * n: number of total input
         * M2: Intermediate quantilties used for calculating sample variance,
         * the sum of squares of differences from the current mean */
        m_cnt += input;
    }

    uint64_t GetCnt()
    {
        return m_cnt;
    }

    double GetMean()
    {
        return m_mean;
    }

    double GetVar()
    {
        return (m_cnt < RPC_VAR) ? 0 : m_m2 / (m_cnt - 1);
    }

    double GetStd()
    {
        return std::sqrt(GetVar());
    }

    double GetCV()
    {
        /* CV < 1: Indicates it has relatively low dispersion. The volatility is below the average level.
         * CV = 1: Indicates it has dispersion comparable to the average level.
         * CV > 1: Indicates it has relatively high dispersion. The volatility is above the average level.
         * CV >1.5 or higher: Typically suggests it has very high volatility, possibly containing extreme
         * values or multipe distinct groups. */
        return (m_cnt == 0 || IsZero(m_mean)) ? 0 : GetStd() / m_mean;
    }

    void Reset()
    {
        m_cnt = 0;
        m_mean = 0.0;
        m_m2 = 0.0;
        m_max = 0;
        m_min = UINT32_MAX;
    }

    void GetInfo(int fd, std::ostringstream &oss)
    {
        if (m_min == UINT32_MAX && m_max == 0) {
            /* When both the maximum and minimum values remain unchanged, it is considered that no statistical
             * information for this variable has been recorded, and a '-' is directly output. */
            oss << std::left << std::setw(FD_WIDTH_MAX) << std::to_string(fd) << std::setw(NAME_WIDTH_MAX) << m_name
                << std::setw(FIELD_WIDTH_MAX) << "-" << std::endl;
            return;
        }

        oss << std::left << std::setw(FD_WIDTH_MAX) << std::to_string(fd) << std::setw(NAME_WIDTH_MAX) << m_name
            << std::setw(FIELD_WIDTH_MAX) << m_cnt << std::endl;
    }

    static void GetTitle(std::ostringstream &oss)
    {
        oss << std::left << std::setw(FD_WIDTH_MAX) << "fd" << std::setw(NAME_WIDTH_MAX) << "type"
            << std::setw(FIELD_WIDTH_MAX) << "total" << std::endl;
    }

    static void FillEmptyForm(std::ostringstream &oss)
    {
        static std::once_flag once_flag;
        std::call_once(once_flag, []() {
            std::ostringstream title_oss;
            GetTitle(title_oss);
            m_title_len = title_oss.str().length();
        });

        /* Here, the use if length rather than content comparison is to enhance the efficiency of the comparsion,
         * with the caller ensuring that the content does not deviate from expectations. */
        if (oss.str().length() != m_title_len) {
            return;
        }

        oss << std::left << std::setw(FD_WIDTH_MAX) << "-" << std::setw(NAME_WIDTH_MAX) << "-"
            << std::setw(FIELD_WIDTH_MAX) << "-" << std::setw(FIELD_WIDTH_MAX) << "-" << std::setw(FIELD_WIDTH_MAX)
            << "-" << std::setw(FIELD_WIDTH_MAX) << "-" << std::setw(FIELD_WIDTH_MAX) << "-" << std::endl;
    }

private:
    bool IsZero(double a)
    {
        return std::fabs(a) < std::numeric_limits<double>::epsilon();
    }

    uint64_t m_cnt = 0;
    double m_mean = 0;
    double m_m2 = 0;
    uint32_t m_max = 0;
    uint32_t m_min = UINT32_MAX;
    std::string m_name;
    static uint32_t m_title_len;
};
class StatsMgr {
public:
    enum trace_stats_type
    {
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

    StatsMgr()
    {
        InitStatsMgr();
    }
    ~StatsMgr() = default;

    bool InitStatsMgr()
    {
        for (int i = 0; i < TRACE_STATE_TYPE_MAX; ++i) {
            try {
                m_recorder_vec.emplace_back(GetStatsStr((enum trace_stats_type)i));
            } catch (std::exception &e) {
                UBS_VLOG_ERR("Failed to construct statistics manager, %s\n", e.what());
                return false;
            }
        }

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

        oss << "\""
            << "totalConnections"
            << "\":" << mConnCount.load() << ",";
        oss << "\""
            << "activeConnections"
            << "\":" << mActiveConnCount.load() << ",";
        oss << "\""
            << "reTxCount"
            << "\":" << mReTxCount.load() << ",";
        oss << "\""
            << "sendPackets"
            << "\":" << mTxPacketCount.load() << ",";
        oss << "\""
            << "receivePackets"
            << "\":" << mRxPacketCount.load() << ",";
        oss << "\""
            << "sendBytes"
            << "\":" << mTxByteCount.load() << ",";
        oss << "\""
            << "receiveBytes"
            << "\":" << mRxByteCount.load() << ",";
        oss << "\""
            << "errorPackets"
            << "\":" << mTxErrorPacketCount.load() << ",";
        oss << "\""
            << "lostPackets"
            << "\":" << mTxLostPacketCount.load() << "";

        oss << "}"
            << "}";
    }

    static void UpdateReTxCount(umq_trans_mode_t umq_trans_mode);

    // data plane interface, caller ensure input validation
    ALWAYS_INLINE void UpdateTraceStats(enum trace_stats_type type, uint32_t value)
    {
        if (!m_stats_enable) {
            return;
        }

        switch (type) {
            case CONN_COUNT:
                mConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case ACTIVE_OPEN_COUNT:
                mActiveConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case RX_PACKET_COUNT:
                mRxPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_PACKET_COUNT:
                mTxPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case RX_BYTE_COUNT:
                mRxByteCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_BYTE_COUNT:
                mTxByteCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_ERROR_PACKET_COUNT:
                mTxErrorPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
                break;

            case TX_LOST_PACKET_COUNT:
                mTxLostPacketCount.fetch_add(value, std::memory_order_relaxed);
                m_recorder_vec[type].Update(value);
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

    void OutputStats(int fd, std::ostringstream &oss)
    {
        if (!m_stats_enable) {
            return;
        }

        for (int i = 0; i < TRACE_STATE_TYPE_MAX; ++i) {
            m_recorder_vec[i].GetInfo(fd, oss);
        }
    }

    const char *GetStatsStr(enum trace_stats_type type)
    {
        const static char *state_type_str[TRACE_STATE_TYPE_MAX] = {
            "totalConnections", "activeConnections", "sendPackets",  "receivePackets",
            "sendBytes",        "receiveBytes",      "errorPackets", "lostPackets",
        };

        return state_type_str[type];
    }

    void GetSocketCLIData(Statistics::CLISocketData *data)
    {
        if (!m_stats_enable || data == nullptr) {
            return;
        }
        data->sendPackets = m_recorder_vec[TX_PACKET_COUNT].GetCnt();
        data->recvPackets = m_recorder_vec[RX_PACKET_COUNT].GetCnt();
        data->sendBytes = m_recorder_vec[TX_BYTE_COUNT].GetCnt();
        data->recvBytes = m_recorder_vec[RX_BYTE_COUNT].GetCnt();
        data->errorPackets = m_recorder_vec[TX_ERROR_PACKET_COUNT].GetCnt();
        data->lostPackets = m_recorder_vec[TX_LOST_PACKET_COUNT].GetCnt();
    }

protected:
    std::vector<Statistics::Recorder> m_recorder_vec;
    bool m_stats_enable = false;
};
} // namespace Statistics

#endif