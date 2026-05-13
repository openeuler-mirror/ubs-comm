/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_DL_API_COMMON_H
#define UBS_COMM_DL_API_COMMON_H

#include <dlfcn.h>
#include <cstdarg>
#include <stdexcept>

#include "ubsocket_common_includes.h"

namespace ock {
namespace ubs {
#define DL_API_DECLARE(__api_name) static __api_name##_api __api_name##_ptr;
#define DL_API_DEFINE(CLASS_NAME, __api_name) __api_name##_api CLASS_NAME::__api_name##_ptr = nullptr;
#define DL_API_SET_NULL(__api_name) __api_name##_ptr = nullptr;

#define DL_LOAD_SYM(TARGET_FUNC_VAR, TARGET_FUNC_TYPE, FILE_HANDLE, SYMBOL_NAME) \
    do {                                                                         \
        TARGET_FUNC_VAR = (TARGET_FUNC_TYPE)dlsym(FILE_HANDLE, SYMBOL_NAME);     \
        if ((TARGET_FUNC_VAR) == nullptr) {                                      \
            dlclose(FILE_HANDLE);                                                \
            FILE_HANDLE = nullptr;                                               \
            UnLoadInner();                                                       \
            return UBS_DL_LOAD_SYM_FAILED;                                       \
        }                                                                        \
    } while (0)

#define DL_API_LOAD(__api_name) DL_LOAD_SYM(__api_name##_ptr, __api_name##_api, handle, #__api_name)

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_DL_API_COMMON_H
