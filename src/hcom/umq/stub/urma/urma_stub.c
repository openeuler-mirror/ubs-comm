/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: stub for urma
 * Create: 2026
 */

#include "urma_stub.h"

int urma_init(void) {
    return 0;
}

void urma_cleanup(void) {
    // 空
}

urma_device_t urma_get_device(const char* name) {
    return (urma_device_t)0x1;
}

urma_eid_t urma_get_eid(urma_device_t dev, const char* addr) {
    return (urma_eid_t)0x1;
}

int urma_common_init(void) {
    return 0;
}

void urma_common_cleanup(void) {
    // 空
}

int urma_get_version(void) {
    return 1;
}

void* urma_common_alloc(size_t size) {
    if (size <= 0) {
        return NULL;
    }
    return malloc(size);
}

void urma_common_free(void* ptr) {
    free(ptr);
}

double get_cpu_mhz(bool cpu_freq_warn) {
    (void)cpu_freq_warn;
    double mhz = 2400.0;
    return mhz;
}