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
#ifndef HCOM_TPSA_API_WRAPPER_H
#define HCOM_TPSA_API_WRAPPER_H
#ifdef UB_BUILD_ENABLED

#include "tpsa_api_dl.h"

namespace ock {
namespace hcom {
class HcomTpsa {
public:
    static inline int UvsGetRouteList(const uvs_route_t *route, uvs_route_list_t *route_list)
    {
        return TpsaAPI::hcomUvsGetRouteList(route, route_list);
    }

    static inline bool IsLoaded()
    {
        return TpsaAPI::IsLoaded();
    }

    static inline int Load()
    {
        return TpsaAPI::LoadTpsaAPI();
    }
};
}
}

#endif
#endif // HCOM_TPSA_API_WRAPPER_H