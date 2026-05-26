/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_PROF_H
#define UBS_COMM_UBSOCKET_PROF_H

#include <cstdint>
#include <ctime>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

extern int ubsocket_prof_enabled;

int ubsocket_prof_init(uint32_t tracepoint_count, const char* dumpPath, uint16_t dumpIntervalMin);

int ubsocket_prof_uninit();

int ubsocket_prof_record(uint32_t tracepoint_id, const char* tracepoint_name, uint64_t timestamp, bool good);

static __always_inline uint64_t ubsocket_get_timeNs()
{
    struct timespec tpDelay = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &tpDelay);
    return tpDelay.tv_sec * 1000000000ULL + tpDelay.tv_nsec;
}

#define PROF_START(TP_ID)                          \
    uint64_t tpBegin##TP_ID = 0;                   \
    if (ubsocket_prof_enabled == 1) {              \
        tpBegin##TP_ID = ubsocket_get_timeNs();    \
    }
 
#define PROF_END(TP_ID, GOOD)                                                            \
    if(ubsocket_prof_enabled == 1){                                                      \
        ubsocket_prof_record(TP_ID, #TP_ID, ubsocket_get_timeNs()-tpBegin##TP_ID, GOOD); \
    }
    
#define PROF_RECORD(TP_ID, TP_NUM, GOOD)                   \
    if(ubsocket_prof_enabled == 1){                        \
        ubsocket_prof_record(TP_ID, #TP_ID, TP_NUM, GOOD); \
    }

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_PROFILING_H
