/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for cli message, etc
 * Author:
 * Create: 2026-03-02
 * Note:
 * History: 2026-03-02
*/

#ifndef UTRACER_INFO_H
#define UTRACER_INFO_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <cstring>
#include <sys/cdefs.h>
#include <iostream>
#include "utracer_utils.h"
#include "utracer_tdigest.h"
#include "rpc_adpt_vlog.h"
#include "ub_lock_ops.h"

namespace Statistics {

class UTracerInfo {
public:
    __always_inline void DelayBegin(const char *tpName)
    {
        if (!isSetName) {
            ScopedUbExclusiveLocker sLock(traceLock.GetMutex());
            if (!isSetName) {
                name = tpName;
                isSetName = true;
            }
        }

        begin++;
    }

    __always_inline void DelayEnd(uint64_t diff, int32_t retCode, bool lateQuantileEnable)
    {
        if (retCode < 0) {
            badEnd++;
            return;
        }
        if (diff < min) {
            min = diff;
        }
        if (diff > max) {
            max = diff;
        }

        if (diff < periodMin) {
            periodMin = diff;
        }
        if (diff > periodMax) {
            periodMax = diff;
        }

        if (lateQuantileEnable) {
            tdigest.Insert(diff);
        }
        
        total += diff;
        goodEnd++;
    }

    __always_inline void Reset()
    {
        begin = 0;
        goodEnd = 0;
        badEnd = 0;
        min = UINT64_MAX;
        max = 0;
        total = 0;
        tdigest.Reset();

        latestBegin = 0;
        latestGoodEnd = 0;
        latestBadEnd = 0;
        latestTotal = 0;
        periodMin = UINT64_MAX;
        periodMax = 0;
    }

    __always_inline void RecordLatest()
    {
        latestBegin = begin;
        latestGoodEnd = goodEnd;
        latestBadEnd = badEnd;
        latestTotal = total;

        periodMin = UINT64_MAX;
        periodMax = 0;
    }

    __always_inline const std::string GetName() const
    {
        return name;
    }

    __always_inline void SetName(const std::string &newName)
    {
        this->name = newName;
    }

    __always_inline uint64_t GetBegin() const
    {
        return begin;
    }

    __always_inline uint64_t GetGoodEnd() const
    {
        return goodEnd;
    }

    __always_inline uint64_t GetBadEnd() const
    {
        return badEnd;
    }

    __always_inline uint64_t GetMin() const
    {
        return min;
    }

    __always_inline uint64_t GetMax() const
    {
        return max;
    }

    __always_inline uint64_t GetTotal() const
    {
        return total;
    }

    __always_inline Tdigest GetTdigest() const
    {
        return tdigest;
    }

    __always_inline bool Valid() const
    {
        return isSetName;
    }

    __always_inline bool ValidPeriod() const
    {
        return (begin - latestBegin) > 0;
    }

    std::string ToString()
    {
        return UTracerUtils::FormatString(name, begin, goodEnd, badEnd, min, max, total);
    }

    std::string ToPeriodString()
    {
        uint64_t interBegin = begin - latestBegin;
        uint64_t interGoodEnd = goodEnd - latestGoodEnd;
        uint64_t interBadEnd = badEnd - latestBadEnd;
        uint64_t interTotal = total - latestTotal;
        uint64_t interMin = periodMin;
        uint64_t interMax = periodMax;
        RecordLatest();
        return UTracerUtils::FormatString(name, interBegin, interGoodEnd, interBadEnd, interMin, interMax, interTotal);
    }

private:
    std::string name = "";
    volatile bool isSetName = false;
    UbExclusiveLock traceLock;

    std::atomic<uint64_t> begin = {0};
    std::atomic<uint64_t> goodEnd = {0};
    std::atomic<uint64_t> badEnd = {0};
    std::atomic<uint64_t> min = {UINT64_MAX};
    std::atomic<uint64_t> max = {0};
    std::atomic<uint64_t> total = {0};
    Tdigest tdigest = Tdigest(20);

    uint64_t latestBegin = 0;
    uint64_t latestGoodEnd = 0;
    uint64_t latestBadEnd = 0;
    uint64_t latestTotal = 0;
    uint64_t periodMin = UINT64_MAX;
    uint64_t periodMax = 0;
};

class TranTraceInfo {
public:
    explicit TranTraceInfo(const char *name)
    {
        errno_t ret = strncpy_s(this->name, sizeof(this->name), name,
                                std::min(strlen(name), static_cast<size_t>(TRACE_INFO_MAX_LEN)));
        if (ret != EOK) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "[HTRACER] Failed to strncpy name, err: %d", ret);
            this->name[0] = '\0';
        }
    }

    void operator += (const TranTraceInfo &other)
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

    TranTraceInfo(const UTracerInfo &info, double quantile, bool enableTp)
    {
        errno_t ret = strncpy_s(this->name, sizeof(this->name), info.GetName().c_str(),
                                std::min(strlen(info.GetName().c_str()), static_cast<size_t>(TRACE_INFO_MAX_LEN)));
        if (ret != EOK) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "[HTRACER] Failed to strncpy name, err: %d", ret);
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
        os << "[" << std::left << std::setw(NN_NO20) << name << "]"
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
        ss << "[" << std::left << std::setw(NN_NO20) << "TP_NAME" << "]"
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

private:
    char name[TRACE_INFO_MAX_LEN + 1] = {0};
    uint64_t begin = 0;
    uint64_t goodEnd = 0;
    uint64_t badEnd = 0;
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    uint64_t total = 0;
    double latencyQuentile = 0.0;
};
}

#endif // UTRACER_INFO_H