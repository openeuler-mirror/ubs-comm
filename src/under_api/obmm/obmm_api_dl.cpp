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
#include "hcom_def.h"
#include "common/net_common.h"
#include "obmm_api_dl.h"

using namespace ock::hcom;

OBMM_EXPORT ObmmAPI::hcomObmmExport = nullptr;
OBMM_EXPORT_USERADDR ObmmAPI::hcomObmmExportUseraddr = nullptr;
OBMM_UNEXPORT ObmmAPI::hcomObmmUnexport = nullptr;
OBMM_IMPORT ObmmAPI::hcomObmmImport = nullptr;
OBMM_UNIMPORT ObmmAPI::hcomObmmUnimport = nullptr;
OBMM_OPEN ObmmAPI::hcomObmmOpen = nullptr;

bool ObmmAPI::gLoaded = false;

#define DLSYM(type, ptr, sym)                                                                                        \
    do {                                                                                                             \
        auto ptr1 = dlsym(handle, sym);                                                                              \
        if (ptr1 == nullptr) {                                                                                       \
            NN_LOG_ERROR("Failed to load function " << sym << ", error " << dlerror());                              \
            dlclose(handle);                                                                                         \
            return NN_NOF1;                                                                                          \
        }                                                                                                            \
        ptr = (type)ptr1;                                                                                            \
    } while (0)

int ObmmAPI::LoadObmmAPI()
{
    if (gLoaded) {
        return 0;
    }

    auto handle = dlopen(OBMM_SO_PATH, RTLD_NOW | RTLD_GLOBAL);
    if (handle == nullptr) {
        NN_LOG_ERROR("Failed to load obmm so " << OBMM_SO_PATH << ", error " << dlerror());
        return NN_NOF1;
    }
    DLSYM(OBMM_EXPORT, ObmmAPI::hcomObmmExport, "obmm_export");
    DLSYM(OBMM_UNEXPORT, ObmmAPI::hcomObmmUnexport, "obmm_unexport");
    DLSYM(OBMM_IMPORT, ObmmAPI::hcomObmmImport, "obmm_import");
    DLSYM(OBMM_UNIMPORT, ObmmAPI::hcomObmmUnimport, "obmm_unimport");
    DLSYM(OBMM_OPEN, ObmmAPI::hcomObmmOpen, "obmm_open");

    NN_LOG_INFO("Success to load obmm");
    gLoaded = true;

    return 0;
}
#endif
