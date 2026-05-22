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
#ifndef UBS_COMM_UBSOCKET_PROFILING_H
#define UBS_COMM_UBSOCKET_PROFILING_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/**
 *
 * @param tracepoint_count
 * @return
 */
int ubsocket_prof_init(uint32_t tracepoint_count);

int ubsocket_prof_uninit();

int ubsocket_prof_record(uint32_t tracepoint_id, uint64_t timestamp, bool good);

#define PROF_START(TP_ID)
#define PROF_END(TP_ID, GOOD)
#define PROF_RECORD(TP_ID, GOOD)

#ifdef __cplusplus
}
#endif

#endif // UBS_COMM_UBSOCKET_PROFILING_H
