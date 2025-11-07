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
#ifndef OCK_HCOM_NET_MONOTONIC_H
#define OCK_HCOM_NET_MONOTONIC_H

#include <fstream>
#ifdef __x86_64__
#include <x86intrin.h>
#endif

#include "hcom.h"
#include "net_common.h"
#include "net_util.h"

namespace ock {
namespace hcom {
constexpr int32_t INIT_FAILURE_RET = NN_NO1;

class NetMonotonic {
#ifdef USE_PROCESS_MONOTONIC
public:
#ifdef __aarch64__
    /*
     * @brief init tick for us
     *
     */
    template <int32_t FAILURE_RET> static int32_t InitTickUs()
    {
        /* get frequ */
        uint64_t tmpFreq = 0;
        __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(tmpFreq));
        auto freq = static_cast<uint32_t>(tmpFreq);

        /* calculate */
        freq = freq / 1000L / 1000L;
        if (freq == 0) {
            NN_LOG_ERROR("Failed to get tick as freq is " << freq);
            return FAILURE_RET;
        }

        return freq;
    }

    /*
     * @brief Get monotonic time in ns, is not absolution time
     */
    static inline uint64_t TimeNs()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        uint64_t timeValue = 0;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(timeValue));
        return timeValue * 1000L / TICK_PER_US;
    }

    /*
     * @brief Get monotonic time in us, is not absolution time
     */
    static inline uint64_t TimeUs()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        uint64_t timeValue = 0;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(timeValue));
        return timeValue / TICK_PER_US;
    }

    /*
     * @brief Get monotonic time in ms, is not absolution time
     */
    static inline uint64_t TimeMs()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        uint64_t timeValue = 0;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(timeValue));
        return timeValue / (TICK_PER_US * 1000L);
    }

    /*
     * @brief Get monotonic time in sec, is not absolution time
     */
    static inline uint64_t TimeSec()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        uint64_t timeValue = 0;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(timeValue));
        return timeValue / (TICK_PER_US * 1000000L);
    }

#elif __x86_64__
    template <int32_t FAILURE_RET> static int32_t InitTickUs()
    {
        const std::string path = "/proc/cpuinfo";
        const std::string prefix = "model name";
        const std::string gHZ = "GHz";

        std::ifstream inConfFile(path);
        if (!inConfFile) {
            NN_LOG_ERROR("Failed to get tick as failed to open " << path);
            return FAILURE_RET;
        }

        bool found = false;
        std::string strLine;
        while (getline(inConfFile, strLine)) {
            if (strLine.compare(0, prefix.size(), prefix) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            NN_LOG_ERROR("Failed to get tick as failed to find " << prefix);
            return FAILURE_RET;
        }

        std::vector<std::string> splitVec;
        NetFunc::NN_SplitStr(strLine, " ", splitVec);
        if (splitVec.empty()) {
            NN_LOG_ERROR("Failed to get tick as failed to get line " << prefix);
            return FAILURE_RET;
        }

        std::string lastWord = splitVec[splitVec.size() - 1];
        auto index = lastWord.find(gHZ);
        if (index == std::string::npos) {
            NN_LOG_ERROR("Failed to get tick as failed to get " << gHZ);
            return FAILURE_RET;
        }

        auto strGhz = lastWord.substr(0, index);
        float fhz = 0.0f;
        if (!NetFunc::NN_Stof(strGhz, fhz)) {
            NN_LOG_ERROR("Failed to get tick as failed to convert " << strGhz << " to float");
            return FAILURE_RET;
        }

        NN_LOG_TRACE_INFO("ghz " << strGhz << ", " << fhz << ", " << static_cast<int32_t>(fhz * 1000L));

        return static_cast<int32_t>(fhz * 1000L);
    }

    /*
     * @brief Get monotonic time in ns, is not absolution time
     */
    static inline uint64_t TimeNs()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        if (TICK_PER_US == INIT_FAILURE_RET) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (static_cast<uint64_t>(ts.tv_sec)) * 1000000000L + ts.tv_nsec;
        }
        return __rdtsc() * 1000L / TICK_PER_US;
    }

    /*
     * @brief Get monotonic time in us, is not absolution time
     */
    static inline uint64_t TimeUs()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        if (TICK_PER_US == INIT_FAILURE_RET) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (static_cast<uint64_t>(ts.tv_sec)) * 1000000L + ts.tv_nsec / 1000L;
        }
        return __rdtsc() / TICK_PER_US;
    }

    /*
     * @brief Get monotonic time in ms, is not absolution time
     */
    static inline uint64_t TimeMs()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        if (TICK_PER_US == INIT_FAILURE_RET) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (static_cast<uint64_t>(ts.tv_sec)) * 1000L + ts.tv_nsec / 1000000L;
        }
        return __rdtsc() / (TICK_PER_US * 1000L);
    }

    /*
     * @brief Get monotonic time in sec, is not absolution time
     */
    static inline uint64_t TimeSec()
    {
        const static int32_t TICK_PER_US = InitTickUs<INIT_FAILURE_RET>();
        if (TICK_PER_US == INIT_FAILURE_RET) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (static_cast<uint64_t>(ts.tv_sec)) + ts.tv_nsec / 1000000000L;
        }
        return __rdtsc() / (TICK_PER_US * 1000000L);
    }

#endif /* __x86_64__ || __aarch64__ */

#else /* USE_PROCESS_MONOTONIC */
public:
    template <int32_t FAILURE_RET> static int32_t InitTickUs()
    {
        return NN_OK;
    }

    static inline uint64_t TimeNs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<uint64_t>(ts.tv_sec)) * 1000000000L + ts.tv_nsec;
    }

    static inline uint64_t TimeUs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<uint64_t>(ts.tv_sec)) * 1000000L + ts.tv_nsec / 1000L;
    }

    static inline uint64_t TimeMs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<uint64_t>(ts.tv_sec)) * 1000L + ts.tv_nsec / 1000000L;
    }

    static inline uint64_t TimeSec()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (static_cast<uint64_t>(ts.tv_sec)) + ts.tv_nsec / 1000000000L;
    }
#endif /* USE_PROCESS_MONOTONIC */
};
}
}

#endif // OCK_HCOM_NET_MONOTONIC_H
