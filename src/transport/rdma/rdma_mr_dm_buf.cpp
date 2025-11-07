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
#ifdef RDMA_CX5_BUILD_ENABLED

#include "rdma_mr_dm_buf.h"

namespace ock {
namespace hcom {

RResult RDMAMemoryRegionDmBuffer::Create(const std::string &name, RDMAContext *ctx, uint32_t singleSegSize,
    uint32_t segCount, RDMAMemoryRegionDmBuffer *&buf)
{
    auto tmp = new (std::nothrow) RDMAMemoryRegionDmBuffer(name, ctx, singleSegSize, segCount);
    if (tmp == nullptr) {
        NN_LOG_ERROR("Failed to create rdma mr dm buffer");
        return RR_NEW_OBJECT_FAILED;
    }
    buf = tmp;
    return RR_OK;
}

RResult RDMAMemoryRegionDmBuffer::Initialize()
{
    RResult result = RR_OK;
    if ((result = RDMAMemoryRegion::InitializeForDm()) != RR_OK) {
        return result;
    }
    // init un-allocated
    mBuf = reinterpret_cast<intptr_t>(memalign(PAGE_ALIGN_H, sizeof(RDMAMemoryRegionDMMgr) * mSegCount));
    if (mBuf == 0) {
        NN_LOG_ERROR("Failed to allocate memory for RDMAMemoryRegionDmBuffer " << mName);
        return RR_MEMORY_ALLOCATE_FAILED;
    }
    uintptr_t address = mBuf;
    for (uint32_t i = 0; i < mSegCount; i++) {
        auto tmpDm = reinterpret_cast<RDMAMemoryRegionDMMgr *>(address);
        tmpDm->offset = mSingleSegSize * i;
        mLinkList.PushFront(address);
        address += sizeof(RDMAMemoryRegionDMMgr);
    }

    return RR_OK;
}

void RDMAMemoryRegionDmBuffer::UnInitialize()
{
    RDMAMemoryRegion::UnInitializeForDm();
}
}
}
#endif
#endif