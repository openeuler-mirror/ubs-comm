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

#include <array>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>

#include "cli_message.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_logger.h"
#include "under_api/umq_api.h"


namespace Statistics {
class Recorder {
public:
    const static uint32_t NAME_WIDTH_MAX = 30;
    const static uint32_t FIELD_WIDTH_MAX = 21;
    const static uint32_t FD_WIDTH_MAX = 10;
    const static uint32_t DEFAULT_PRECISION = 3;
    const static uint32_t TOTAL_PRECISION = 7;

    /*
     * Recorder 不再持有名字副本：统计项名字是编译期静态表（GetStatsStr），
     * 每个 socket 的 Recorder 各存一份 std::string 纯属浪费（每链路 16~32B + 超过
     * SSO 的名字还会额外堆分配）。打印时由调用方传入名字。
     */
    Recorder() = default;

    /*
     * Recorder 收敛为单一累加计数器：原 Welford 统计字段（m_mean/m_m2/m_max/m_min）
     * 与其读取接口（GetMean/GetVar/GetStd/GetCV）全仓无任何调用者，Update 亦从未
     * 更新过它们（仅累加 m_cnt），属死数据。删除后每槽位 32B -> 8B，
     * 每链路统计内联占用 264B -> ~72B。
     */
    ALWAYS_INLINE void Update(uint32_t input)
    {
        m_cnt += input;
    }

    uint64_t GetCnt()
    {
        return m_cnt;
    }

    void Reset()
    {
        m_cnt = 0;
    }

    void GetInfo(int fd, const char *name, std::ostringstream &oss)
    {
        if (m_cnt == 0) {
            /* Nothing has been accumulated for this variable, output a '-' directly. */
            oss << std::left << std::setw(FD_WIDTH_MAX) << std::to_string(fd) << std::setw(NAME_WIDTH_MAX) << name
                << std::setw(FIELD_WIDTH_MAX) << "-" << std::endl;
            return;
        }

        oss << std::left << std::setw(FD_WIDTH_MAX) << std::to_string(fd) << std::setw(NAME_WIDTH_MAX) << name
            << std::setw(FIELD_WIDTH_MAX) << m_cnt << std::endl;
    }

    static void GetTitle(std::ostringstream &oss)
    {
        oss << std::left << std::setw(FD_WIDTH_MAX) << "fd" << std::setw(NAME_WIDTH_MAX) << "type"
            << std::setw(FIELD_WIDTH_MAX) << "total" << std::endl;
    }

    static void FillEmptyForm(std::ostringstream &oss)
    {
        /* bthread 上禁用 std::call_once（原因见 ubsocket_leaky_singleton.h） */
        static pthread_mutex_t once_mtx = PTHREAD_MUTEX_INITIALIZER;
        static std::atomic<bool> once_done{false};
        if (!once_done.load(std::memory_order_acquire)) {
            (void)pthread_mutex_lock(&once_mtx);
            if (!once_done.load(std::memory_order_relaxed)) {
                std::ostringstream title_oss;
                GetTitle(title_oss);
                m_title_len = title_oss.str().length();
                once_done.store(true, std::memory_order_release);
            }
            (void)pthread_mutex_unlock(&once_mtx);
        }

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
    uint64_t m_cnt = 0;
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
        BIGDATA_CTRL_RECV_COUNT,
        BIGDATA_READ_COUNT,
        BIGDATA_CTRL_SEND_COUNT,

        TRACE_STATE_TYPE_MAX
    };

    StatsMgr()
    {
        InitStatsMgr();
    }
    ~StatsMgr() = default;

    bool InitStatsMgr()
    {
        if (ock::ubs::GlobalSetting::UBS_MONITOR_ENABLE) {
            m_recorder_vec.reset(new std::array<Recorder, TRACE_STATE_TYPE_MAX>());
            for (auto &r : *m_recorder_vec) {
                r.Reset();
            }
            m_stats_enable = true;
        }
        return true;
    }

    inline static std::atomic<uint32_t> mConnCount{0};
    inline static std::atomic<uint32_t> mActiveConnCount{0};
    inline static std::atomic<uint32_t> mReTxCount{0};
    inline static std::mutex gTpPerfSeqMutex;

    static uint32_t GetConnCount()
    {
        return mConnCount.load(std::memory_order_relaxed);
    }

    static uint32_t GetActiveConnCount()
    {
        return mActiveConnCount.load(std::memory_order_relaxed);
    }

    static uint32_t GetReTxCount()
    {
        return mReTxCount.load(std::memory_order_relaxed);
    }

    /* Aggregated per-socket totals (summed across all UmqSocket in ArraySet).
     * Filled by AggregatePerSocketStats() before OutputAllStats() so the KPI
     * JSON includes sendPackets/receivePackets/sendBytes/receiveBytes/errorPackets/
     * lostPackets without relying on the deleted global atomics. */
    struct AggregatedStats {
        uint64_t sendPackets = 0;
        uint64_t receivePackets = 0;
        uint64_t sendBytes = 0;
        uint64_t receiveBytes = 0;
        uint64_t errorPackets = 0;
        uint64_t lostPackets = 0;
    };

    static ALWAYS_INLINE void OutputAllStats(std::ostringstream &oss, uint32_t pid,
                                             const AggregatedStats &agg)
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
            << "\":" << agg.sendPackets << ",";
        oss << "\""
            << "receivePackets"
            << "\":" << agg.receivePackets << ",";
        oss << "\""
            << "sendBytes"
            << "\":" << agg.sendBytes << ",";
        oss << "\""
            << "receiveBytes"
            << "\":" << agg.receiveBytes << ",";
        oss << "\""
            << "errorPackets"
            << "\":" << agg.errorPackets << ",";
        oss << "\""
            << "lostPackets"
            << "\":" << agg.lostPackets << "";

        oss << "}"
            << "}";
    }

    static void UpdateReTxCount(umq_trans_mode_t umq_trans_mode);

    /* Sum per-socket Recorder values across all UmqSocket in ArraySet<Socket>.
     * Implemented in statistics.cpp (has access to ArraySet and UmqSocket). */
    static void AggregatePerSocketStats(AggregatedStats &out);

    // data plane interface, caller ensure input validation
    ALWAYS_INLINE void UpdateTraceStats(enum trace_stats_type type, uint32_t value)
    {
        switch (type) {
            case CONN_COUNT:
                mConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case ACTIVE_OPEN_COUNT:
                mActiveConnCount.fetch_add(value, std::memory_order_relaxed);
                break;

            case RX_PACKET_COUNT:
            case TX_PACKET_COUNT:
            case RX_BYTE_COUNT:
            case TX_BYTE_COUNT:
            case BIGDATA_CTRL_RECV_COUNT:
            case BIGDATA_READ_COUNT:
            case BIGDATA_CTRL_SEND_COUNT:
                if (m_recorder_vec != nullptr) {
                    (*m_recorder_vec)[type].Update(value);
                }
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
        if (m_recorder_vec == nullptr) {
            return;
        }

        for (int i = 0; i < TRACE_STATE_TYPE_MAX; ++i) {
            (*m_recorder_vec)[i].GetInfo(fd, GetStatsStr(static_cast<enum trace_stats_type>(i)), oss);
        }
    }

    const char *GetStatsStr(enum trace_stats_type type)
    {
        const static char *state_type_str[TRACE_STATE_TYPE_MAX] = {
            "totalConnections", "activeConnections", "sendPackets",  "receivePackets",
            "sendBytes",        "receiveBytes",      "bigdataCtrlRecv", "bigdataRead",
            "bigdataCtrlSend"};

        return state_type_str[type];
    }

    void GetSocketCLIData(Statistics::CLISocketData *data)
    {
        if (m_recorder_vec == nullptr || data == nullptr) {
            if (data != nullptr) {
                data->sendPackets = 0;
                data->recvPackets = 0;
                data->sendBytes = 0;
                data->recvBytes = 0;
                data->bigdataCtrlRecv = 0;
                data->bigdataRead = 0;
                data->bigdataCtrlSend = 0;
                data->errorPackets = 0;
                data->lostPackets = 0;
            }
            return;
        }
        data->sendPackets = (*m_recorder_vec)[TX_PACKET_COUNT].GetCnt();
        data->recvPackets = (*m_recorder_vec)[RX_PACKET_COUNT].GetCnt();
        data->sendBytes = (*m_recorder_vec)[TX_BYTE_COUNT].GetCnt();
        data->recvBytes = (*m_recorder_vec)[RX_BYTE_COUNT].GetCnt();
        data->bigdataCtrlRecv = (*m_recorder_vec)[BIGDATA_CTRL_RECV_COUNT].GetCnt();
        data->bigdataRead = (*m_recorder_vec)[BIGDATA_READ_COUNT].GetCnt();
        data->bigdataCtrlSend = (*m_recorder_vec)[BIGDATA_CTRL_SEND_COUNT].GetCnt();
        data->errorPackets = 0;
        data->lostPackets = 0;
    }

protected:
    std::unique_ptr<std::array<Statistics::Recorder, TRACE_STATE_TYPE_MAX>> m_recorder_vec;
    bool m_stats_enable = false;
};
} // namespace Statistics

#endif