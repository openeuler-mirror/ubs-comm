/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_ZCOPY_ADAPTER_H
#define UBS_COMM_ZCOPY_ADAPTER_H

#include "ubsocket_common_includes.h"

namespace ock {
namespace ubs {
class UbsZcopyAdapter {
    typedef void *(*blockmem_allocate_t)(size_t);
    typedef void (*blockmem_deallocate_t)(void *);

    void *blockmem_allocate_zero_copy(size_t);
    void blockmem_deallocate_zero_copy(void *);
};
} // namespace ubs
} // namespace ock
#endif
