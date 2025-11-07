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

#include "ub_mr_fixed_buf.h"

namespace ock {
namespace hcom {

UResult UBMemoryRegionFixedBuffer::Create(const std::string &name, UBContext *ctx, uint32_t singleSegSize,
    uint32_t segCount, unsigned long memid, UBMemoryRegionFixedBuffer *&buf)
{
    auto tmp = new (std::nothrow) UBMemoryRegionFixedBuffer(name, ctx, memid, singleSegSize, segCount);
    if (tmp == nullptr) {
        return UB_NEW_OBJECT_FAILED;
    }
    buf = tmp;
    return UB_OK;
}

UResult UBMemoryRegionFixedBuffer::Initialize()
{
    UResult result = UB_OK;
    if ((result = UBMemoryRegion::Initialize()) != UB_OK) {
        return result;
    }

    // init un-allocated
    uintptr_t address = mBuf;
    for (uint32_t i = 0; i < mSegCount; i++) {
        mLinkList.PushFront(address);
        address += mSingleSegSize;
    }

    return UB_OK;
}

void UBMemoryRegionFixedBuffer::UnInitialize()
{
    UBMemoryRegion::UnInitialize();
}
}
}
#endif