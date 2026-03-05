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
#ifndef HCOM_DYLOADER_ITPSA_H
#define HCOM_DYLOADER_ITPSA_H
#ifdef UB_BUILD_ENABLED

#include <errno.h>
#include <linux/types.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "uvs_types.h"

#define TPSA_SO_PATH "libtpsa.so.0"

using UVS_GET_ROUTE_LIST = int (*)(const uvs_route_t *route, uvs_route_list_t *route_list);

class TpsaAPI {
    public:
        static UVS_GET_ROUTE_LIST hcomUvsGetRouteList;
        static bool IsLoaded();

        static int LoadTpsaAPI()

    private:
        static bool gLoaded;
};

#endif
#endif // HCOM_DYLOADER_ITPSA_H
