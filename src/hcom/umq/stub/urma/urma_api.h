/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: stub for urma_api.h
 * Create: 2026
 */

/* All types and functions in this file must match the external URMA library interface
 * (snake_case). Note: Type names use CamelCase to satisfy internal coding standards. */

#ifndef URMA_API_H
#define URMA_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stub types - minimal opaque handles */
/* opaque */
typedef struct {} UrmaDeviceT;
/* opaque */
typedef struct {} UrmaEidT;
/* opaque */
typedef struct {} UrmaJfrT;
/* opaque */
typedef struct {} UrmaJfsT;
/* opaque */
typedef struct {} UrmaEjbT;
/* opaque */
typedef struct {} UrmaJettyT;

/* Stub functions - these must match the real URMA library interface exactly */
int UrmaInit(void);
void UrmaCleanup(void);
UrmaDeviceT UrmaGetDevice(const char *name);
UrmaEidT UrmaGetEid(UrmaDeviceT dev, const char *addr);
int UrmaCommonInit(void);
void UrmaCommonCleanup(void);
int UrmaGetVersion(void);
uint8_t *UrmaCommonAlloc(size_t size);
void UrmaCommonFree(uint8_t *ptr);

#ifdef __cplusplus
}
#endif

#endif /* URMA_API_H */
