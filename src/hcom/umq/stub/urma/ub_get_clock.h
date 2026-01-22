/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: stub for urma
 * Create: 2026
 */

#ifndef STUB_UB_GET_CLOCK_H
#define STUB_UB_GET_CLOCK_H

#include "urma_stub.h"

#define CLOCK_SIZE_OF_INT_STUB (32)
#define CLOCK_SIZE_OF_INT CLOCK_SIZE_OF_INT_STUB

static inline uint64_t get_cycles_stub(void)
{
    uint64_t cycle = 1;
    return cycle;
}
#define get_cycles get_cycles_stub

// static inline double get_cpu_mhz_stub(bool cpu_freq_warn) {
//     (void)cpu_freq_warn;
//     double mhz = 2400.0;
//     return mhz;
// }
// #define get_cpu_mhz get_cpu_mhz_stub

#endif
