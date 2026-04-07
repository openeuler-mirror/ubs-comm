/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: stub for uvs_api.h
 * Create: 2026
 */

 
#ifndef UVS_API_H
#define UVS_API_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "urma_stub.h"


/* UVS function stubs - these must match the real UVS library interface exactly */
/* NOLINT(readability-identifier-naming): Names must match external UVS library interface */
/* NOLINT(google-runtime-int): topo_num type must match external interface */
int UvsSetTopoInfo(const uvs_topo_t *topo, uint32_t topoNum); // NOLINT(readability-identifier-naming)
int UvsGetRouteList(const uvs_route_t *route, uvs_route_list_t *routeList); // NOLINT(readability-identifier-naming)
int UvsInit(void); // NOLINT(readability-identifier-naming)
void UvsCleanup(void); // NOLINT(readability-identifier-naming)

#endif /* UVS_API_H */
