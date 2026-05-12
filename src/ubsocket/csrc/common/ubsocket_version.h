/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef HCOM_UBSOCKET_VERSION_H
#define HCOM_UBSOCKET_VERSION_H

namespace ock {
namespace ubs {

/* version information, these variables are read in cmake from VERSION file */
#ifndef UBS_VERSION_MAJOR
#define UBS_VERSION_MAJOR 0
#define UBS_VERSION_MINOR 0
#define UBS_VERSION_FIX 0
#endif

/* second level marco define 'CONCAT' to get string */
#define CONCAT(x, y, z) x.##y.##z
#define STR(x) #x
#define CONCAT2(x, y, z) CONCAT(x, y, z)
#define STR2(x) STR(x)

/* get cancat version string */
#define UBS_LIB_VERSION STR2(CONCAT2(UBS_VERSION_MAJOR, UBS_VERSION_MINOR, UBS_VERSION_FIX))

#ifndef UBS_GIT_LAST_COMMIT
#define UBS_GIT_LAST_COMMIT empty
#endif

/*
 * global lib version string with build time
 */
[[maybe_unused]] static const char *UBS_LIB_VERSION_FULL = "library version: " UBS_LIB_VERSION ", build time: " __DATE__
                                                           " " __TIME__ ", commit: " STR2(UBS_GIT_LAST_COMMIT);

} // namespace ubs
} // namespace ock

#endif // HCOM_UBSOCKET_VERSION_H
