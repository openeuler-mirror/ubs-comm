/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: stub for urma_types.h
 * Create: 2026
 */

#ifndef URMA_TYPES_H
#define URMA_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Ensure handle types are available for these declarations. */
#include "urma_api.h"

typedef uint8_t *urma_buf_t;
typedef uint8_t **urma_buf_pp_t;

/* Stub types - minimal definitions for compilation */
/* NOLINT(readability-identifier-naming): These names must match the real URMA library interface */
typedef enum UrmaStatus { // NOLINT(readability-identifier-naming)
    URMA_SUCCESS = 0,  // NOLINT(readability-magic-numbers)
    URMA_ERROR = -1    // NOLINT(readability-magic-numbers)
} UrmaStatusT; // NOLINT(readability-identifier-naming)

typedef enum UrmaJfrType { // NOLINT(readability-identifier-naming)
    URMA_JFR_TYPE_NORMAL = 0,        // NOLINT(readability-magic-numbers)
    URMA_JFR_TYPE_LOW_LATENCY = 1    // NOLINT(readability-magic-numbers)
} UrmaJfrTypeT; // NOLINT(readability-identifier-naming)

typedef enum UrmaJfsType { // NOLINT(readability-identifier-naming)
    URMA_JFS_TYPE_NORMAL = 0,        // NOLINT(readability-magic-numbers)
    URMA_JFS_TYPE_LOW_LATENCY = 1    // NOLINT(readability-magic-numbers)
} UrmaJfsTypeT; // NOLINT(readability-identifier-naming)

typedef struct UrmaJfrAttr {
    uint32_t depth;
    UrmaJfrTypeT type;
    uint32_t segSize;
} UrmaJfrAttrT;

typedef struct UrmaJfsAttr {
    uint32_t depth;
    UrmaJfsTypeT type;
    uint32_t segSize;
} UrmaJfsAttrT;

typedef struct UrmaJettyAttr {
    UrmaJfrAttrT jfrAttr;
    UrmaJfsAttrT jfsAttr;
} UrmaJettyAttrT;

typedef struct UrmaSegAttr {
    uint64_t addr;
    uint64_t len;
    uint32_t access;
} UrmaSegAttrT;

/* Stub function declarations */
UrmaStatusT UrmaCreateJetty(UrmaEidT eid, UrmaJettyAttrT *attr, UrmaJettyT *jetty);
UrmaStatusT UrmaDestroyJetty(UrmaJettyT jetty);
UrmaJfrT UrmaJettyGetJfr(UrmaJettyT jetty);
UrmaJfsT UrmaJettyGetJfs(UrmaJettyT jetty);
UrmaStatusT UrmaJfrRecv(UrmaJfrT jfr, urma_buf_pp_t buf, uint64_t *len);  // NOLINT(hicpp-no-array-decay)
UrmaStatusT UrmaJfsSend(UrmaJfsT jfs, urma_buf_t buf, uint64_t len);  // NOLINT(hicpp-no-array-decay)
UrmaStatusT UrmaJfsSegAlloc(UrmaJfsT jfs, UrmaSegAttrT *attr, urma_jfs_seg_id_t *segId);
UrmaStatusT UrmaJfsSegFree(UrmaJfsT jfs, urma_jfs_seg_id_t segId);

#endif /* URMA_TYPES_H */
