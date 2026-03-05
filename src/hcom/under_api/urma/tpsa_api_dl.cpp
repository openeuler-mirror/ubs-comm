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
#ifdef UB_BUILD_ENABLED
#include <dlfcn.h>

#include "hcom_log.h"
#include "tpsa_api_dl.h"

using namespace ock::hcom;

UVS_GET_ROUTE_LIST TpsaAPI::hcomUvsGetRouteList = nullptr;

bool TpsaAPI::gLoaded = false;

bool TpsaAPI::IsLoaded()
{
    return gLoaded;
}

#define DLSYM(type, ptr, sym)                                                                 \
    do {                                                                                      \
        auto ptr1 = dlsym(handle, sym);                                                       \
        if (ptr1 == nullptr) {                                                                \
            NN_LOG_ERROR("Failed to load function " << sym << ", error " << dlerror()); \
            dlclose(handle);                                                                  \
            return -1;                                                                        \
        }                                                                                     \
        ptr = (type)ptr1;                                                                     \
    } while (0)

int TpsaAPI::LoadTpsaAPI()
{
    if (gLoaded) {
        return 0;
    }

    void *handle = dlopen(TPSA_SO_PATH, RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        NN_LOG_ERROR("Failed to load verbs so " << TPSA_SO_PATH << ", error " << dlerror());
        return -1;
    }

    DLSYM(UVS_GET_ROUTE_LIST, TpsaAPI::hcomUvsGetRouteList, "uvs_get_route_list");

    NN_LOG_INFO("Success to load Tpsa api");
    gLoaded = true;

    return 0;
}

#endif
