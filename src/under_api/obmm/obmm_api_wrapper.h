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
#ifndef HCOM_OBMM_API_WRAPPER_H
#define HCOM_OBMM_API_WRAPPER_H
#ifdef UB_BUILD_ENABLED

#include "obmm_api_dl.h"

namespace ock {
namespace hcom {
class HcomObmm {
public:
    static inline uint64_t ObmmExport(size_t size, bitmask *nodes, unsigned long flags, struct obmm_mem_desc *desc)
    {
        return ObmmAPI::hcomObmmExport(size, nodes, flags, desc);
    }

    static inline uint64_t ObmmExportUseraddr(void *addr, size_t size, unsigned long flags,
                                                struct obmm_mem_desc *desc)
    {
        return ObmmAPI::hcomObmmExportUseraddr(addr, size, flags, desc);
    }

    static inline int ObmmUnexport(uint64_t id, unsigned long flags)
    {
        return ObmmAPI::hcomObmmUnexport(id, flags);
    }

    static inline int ObmmUnimport(uint64_t id, unsigned long flags)
    {
        return ObmmAPI::hcomObmmUnimport(id, flags);
    }

    static inline uint64_t ObmmImport(struct obmm_mem_desc *desc, unsigned long flags, int *numa)
    {
        return ObmmAPI::hcomObmmImport(desc, flags, numa);
    }

    static inline int ObmmOpen(uint64_t id)
    {
        return ObmmAPI::hcomObmmOpen(id);
    }

    static inline int Load()
    {
        return ObmmAPI::LoadObmmAPI();
    }
};
}
}
#endif
#endif  // HCOM_OBMM_API_WRAPPER_H