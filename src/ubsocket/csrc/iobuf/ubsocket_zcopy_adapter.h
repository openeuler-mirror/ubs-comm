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

#include <cstddef>

#include "include/ubsocket.h"

namespace ock {
namespace ubs {
class UbsZeroCopyAllocator {
public:
    virtual ~UbsZeroCopyAllocator() = default;
    virtual void *allocate(size_t size, const ubs_iobuf_alloc_option_t *option) = 0;
    virtual void *allocate(size_t size)
    {
        return allocate(size, nullptr);
    }
    virtual void deallocate(void *ptr) = 0;
};

extern UbsZeroCopyAllocator *g_zcopy_allocator;
} // namespace ubs
} // namespace ock
#endif
