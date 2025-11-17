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
#ifndef OCK_RDMA_MR_12342341232433_H
#define OCK_RDMA_MR_12342341232433_H
#ifdef RDMA_BUILD_ENABLED

#include "hcom.h"

#include "net_bucket_linked_list.h"
#include "rdma_verbs_wrapper_ctx.h"
#include "net_util.h"

namespace ock {
namespace hcom {
class RDMAMemoryRegion : public UBSHcomNetMemoryRegion {
public:
    static RResult Create(const std::string &name, RDMAContext *ctx, uint64_t size, RDMAMemoryRegion *&buf);
    static RResult Create(const std::string &name, RDMAContext *ctx, uintptr_t address, uint64_t size,
        RDMAMemoryRegion *&buf);

    RDMAMemoryRegion() = delete;
    RDMAMemoryRegion(const RDMAMemoryRegion &other) = delete;
    RDMAMemoryRegion(RDMAMemoryRegion &&other) = delete;
    RDMAMemoryRegion &operator = (const RDMAMemoryRegion &) = delete;
    RDMAMemoryRegion &operator = (RDMAMemoryRegion &&) = delete;

    ~RDMAMemoryRegion() override
    {
        OBJ_GC_DECREASE(RDMAMemoryRegion);
    }

    RResult Initialize() override;
    void UnInitialize() override;

    void *GetMemorySeg() override
    {
        return nullptr;
    }

    void GetVa(uint64_t &va, uint64_t &vaLen, uint32_t &tokenId) override
    {
        return;
    }

public:
    RDMAContext *mRDMAContext = nullptr;

protected:
    RDMAMemoryRegion(const std::string &name, RDMAContext *ctx, uint64_t size)
        : UBSHcomNetMemoryRegion(name, false, 0, size), mRDMAContext(ctx)
    {
        // increase the reference count of context
        if (ctx != nullptr) {
            ctx->IncreaseRef();
        }

        OBJ_GC_INCREASE(RDMAMemoryRegion);
    }

    RDMAMemoryRegion(const std::string &name, RDMAContext *ctx, uintptr_t address, uint64_t size)
        : UBSHcomNetMemoryRegion(name, true, address, size), mRDMAContext(ctx)
    {
        // increase the reference count of context
        if (ctx != nullptr) {
            ctx->IncreaseRef();
        }

        OBJ_GC_INCREASE(RDMAMemoryRegion);
    }

protected:
    ibv_mr *mMemReg = nullptr;

    static uint64_t gPageSize;
};
}
}
#endif
#endif // _OCK_RDMA_MR_12342341232433_H
