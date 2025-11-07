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

#ifndef HCOM_RDMA_MR_DM_BUF_H
#define HCOM_RDMA_MR_DM_BUF_H
#ifdef RDMA_BUILD_ENABLED
#ifdef RDMA_CX5_BUILD_ENABLED

#include "rdma_mr_pool.h"

namespace ock {
namespace hcom {

struct RDMAMemoryRegionDMMgr {
    uintptr_t next;
    uint64_t offset;
};

class RDMAMemoryRegionDmBuffer : public RDMAMemoryRegion {
public:
    static RResult Create(const std::string &name, RDMAContext *ctx, uint32_t singleSegSize, uint32_t segCount,
                          RDMAMemoryRegionDmBuffer *&buf);

public:
    RDMAMemoryRegionDmBuffer(const std::string &name, RDMAContext *ctx, uint32_t singleSegSize, uint32_t segCount)
        : RDMAMemoryRegion(name, ctx, static_cast<uint64_t>(singleSegSize) * static_cast<uint64_t>(segCount)),
          mSingleSegSize(singleSegSize),
          mSegCount(segCount)
    {
        OBJ_GC_INCREASE(RDMAMemoryRegionDmBuffer);
    }

    ~RDMAMemoryRegionDmBuffer() override
    {
        UnInitialize();
        OBJ_GC_DECREASE(RDMAMemoryRegionDmBuffer);
    }

    RResult Initialize() override;

    inline bool GetFreeBuffer(uintptr_t &item)
    {
        return mLinkList.Pop(item);
    }

    inline bool ReturnBuffer(uintptr_t value)
    {
        mLinkList.PushFront(value);
        return true;
    }

protected:
    void UnInitialize() override;

private:
    uint32_t mSingleSegSize = MR_DM_BUFFER_DEFAULT_SEG_SIZE;
    uint32_t mSegCount = MR_DM_BUFFER_DEFAULT_SEG_COUNT;

    // uintptr_p store the start address of each mr segment
    NetBucketLinkedList mLinkList;
};
}
}
#endif
#endif
#endif // HCOM_RDMA_MR_DM_BUF_H