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

#ifndef HTRACER_INFO_H
#define HTRACER_INFO_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <cstring>
#include <sys/cdefs.h>
#include "htracer_utils.h"
#include "htracer_tdigest.h"
#include <iostream>

#define SERVICE_ID(TP_ID_) (((TP_ID_) >> 16) & 0xFFFF)
#define INNER_ID(TP_ID_) ((TP_ID_) & 0xFFFF)
#define INVALID_SERVICE_ID (0xFFFF)
#define MAX_SERVICE_NUM (256)
#define MAX_INNER_ID_NUM (2)

namespace ock {
namespace hcom {

class TraceInfo {
public:
    __always_inline void DelayBegin(const char *tpName)
    {
        if (!isSetName) {
            std::lock_guard<std::mutex> lock(traceLock);
            if (!isSetName) {
                name = tpName;
                isSetName = true;
            }
        }

        begin++;
    }

    __always_inline void DelayEnd(uint64_t diff, int32_t retCode, bool lateQuantileEnable)
    {
        if (retCode != 0) {
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

    __always_inline void SetName(const std::string &name)
    {
        this->name = name;
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
        return HTracerUtils::FormatString(name, begin, goodEnd, badEnd, min, max, total);
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
        return HTracerUtils::FormatString(name, interBegin, interGoodEnd, interBadEnd, interMin, interMax, interTotal);
    }

private:
    std::string name = "";
    volatile bool isSetName = false;
    std::mutex traceLock;

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

}
}

#endif