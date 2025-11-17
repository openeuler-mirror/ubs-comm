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

#ifndef HCOM_UB_MR_POOL_H
#define HCOM_UB_MR_POOL_H
#ifdef UB_BUILD_ENABLED
#include <sys/mman.h>

#include "hcom.h"
#include "ub_common.h"
#include "ub_urma_wrapper_ctx.h"
#include "net_bucket_linked_list.h"
#include "under_api/urma/urma_api_wrapper.h"
#include "net_util.h"

namespace ock {
namespace hcom {
class UBMemoryRegion : public UBSHcomNetMemoryRegion {
public:
    static UResult Create(const std::string &name, UBContext *ctx, uint64_t size, UBMemoryRegion *&buf);
    static UResult Create(const std::string &name, UBContext *ctx, uintptr_t address, uint64_t size,
        UBMemoryRegion *&buf);
    void *GetMemorySeg() override
    {
        return mMemSeg;
    }

    void GetVa(uint64_t &va, uint64_t &vaLen, uint32_t &tokenId) override
    {
        va = mMemSeg->seg.ubva.va;
        vaLen = mMemSeg->seg.len;
        tokenId = mMemSeg->seg.token_id;
    }

public:
    UBMemoryRegion() = delete;
    UBMemoryRegion(const UBMemoryRegion &other) = delete;
    UBMemoryRegion(UBMemoryRegion &&other) = delete;
    UBMemoryRegion &operator = (const UBMemoryRegion &) = delete;
    UBMemoryRegion &operator = (UBMemoryRegion &&) = delete;

    ~UBMemoryRegion() override
    {
        OBJ_GC_DECREASE(UBMemoryRegion);
    }

    UResult Initialize() override;
    UResult InitializeForOneSide();
    void UnInitialize() override;
    inline UBSHcomNetDriverProtocol GetProtocol()
    {
        return mUBContext->protocol;
    }

public:
    UBContext *mUBContext = nullptr;

protected:
    UBMemoryRegion(const std::string &name, UBContext *ctx, uint64_t size, unsigned long memid, int flag)
        : UBSHcomNetMemoryRegion(name, false, 0, size), mUBContext(ctx), mMemid(memid)
    {
        // increase the reference count of context
        if (ctx != nullptr) {
            ctx->IncreaseRef();
        }

        OBJ_GC_INCREASE(UBMemoryRegion);
    }

    UBMemoryRegion(const std::string &name, UBContext *ctx, uintptr_t address, uint64_t size)
        : UBSHcomNetMemoryRegion(name, true, address, size), mUBContext(ctx)
    {
        // increase the reference count of context
        if (ctx != nullptr) {
            ctx->IncreaseRef();
        }

        OBJ_GC_INCREASE(UBMemoryRegion);
    }

protected:
    urma_target_seg_t *mMemSeg = nullptr;
    unsigned long mMemid = 0;
    static uint64_t gPageSize;
};
}
}
#endif
#endif // HCOM_UB_MR_POOL_H
