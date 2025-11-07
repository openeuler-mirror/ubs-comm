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
#ifdef RDMA_BUILD_ENABLED

#include "rdma_mr_fixed_buf.h"

namespace ock {
namespace hcom {

RResult RDMAMemoryRegionFixedBuffer::Create(const std::string &name, RDMAContext *ctx, uint32_t singleSegSize,
    uint32_t segCount, RDMAMemoryRegionFixedBuffer *&buf)
{
    auto tmp = new (std::nothrow) RDMAMemoryRegionFixedBuffer(name, ctx, singleSegSize, segCount);
    if (tmp == nullptr) {
        NN_LOG_ERROR("Failed to create rdma mr fixed buffer");
        return RR_NEW_OBJECT_FAILED;
    }
    buf = tmp;
    return RR_OK;
}

RResult RDMAMemoryRegionFixedBuffer::Initialize()
{
    RResult result = RR_OK;
    if ((result = RDMAMemoryRegion::Initialize()) != RR_OK) {
        NN_LOG_ERROR("Failed to initialize rdma mr res = " << result);
        return result;
    }

    // init un-allocated
    uintptr_t address = mBuf;
    for (uint32_t i = 0; i < mSegCount; i++) {
        mLinkList.PushFront(address);
        address += mSingleSegSize;
    }

    return RR_OK;
}

void RDMAMemoryRegionFixedBuffer::UnInitialize()
{
    RDMAMemoryRegion::UnInitialize();
}
}
}
#endif