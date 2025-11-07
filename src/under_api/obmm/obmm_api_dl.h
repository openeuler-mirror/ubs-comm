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
#ifndef HCOM_UNDER_API_OBMM_API_DL_H
#define HCOM_UNDER_API_OBMM_API_DL_H
#ifdef UB_BUILD_ENABLED

#include <numa.h>
#include "hcom.h"

namespace ock {
namespace hcom {

#define OBMM_SO_PATH "libobmm.so"

using OBMM_EXPORT = uint64_t (*)(size_t size, bitmask *nodes, unsigned long flags, struct obmm_mem_desc *desc);
using OBMM_EXPORT_USERADDR = uint64_t (*)(void *addr, size_t size, unsigned long flags, struct obmm_mem_desc *desc);
using OBMM_UNEXPORT = int (*)(uint64_t id, unsigned long flags);
using OBMM_IMPORT = uint64_t (*)(struct obmm_mem_desc *desc, unsigned long flags, int *numa);
using OBMM_UNIMPORT = int (*)(uint64_t id, unsigned long flags);
using OBMM_OPEN = int (*)(uint64_t id);

class ObmmAPI {
public:
    static OBMM_EXPORT hcomObmmExport;
    static OBMM_EXPORT_USERADDR hcomObmmExportUseraddr;
    static OBMM_UNEXPORT hcomObmmUnexport;
    static OBMM_IMPORT hcomObmmImport;
    static OBMM_UNIMPORT hcomObmmUnimport;
    static OBMM_OPEN hcomObmmOpen;

    static int LoadObmmAPI();

private:
    static bool gLoaded;
};
}
}
#endif
#endif /* UAPI_OBMM_H */