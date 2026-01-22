/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: stub for urma
 * Create: 2026
 */

#ifndef URMA_STUB_H
#define URMA_STUB_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

double get_cpu_mhz_stub(bool cpu_freq_warn);

typedef void* urma_device_t;
typedef void* urma_eid_t;

int urma_init(void);
void urma_cleanup(void);
urma_device_t urma_get_device(const char* name);
urma_eid_t urma_get_eid(urma_device_t dev, const char* addr);

int urma_common_init(void);
void urma_common_cleanup(void);
int urma_get_version(void);
void* urma_common_alloc(size_t size);
void urma_common_free(void* ptr);

#endif