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
#include <malloc.h>

#include "verbs_api_wrapper.h"
#include "rdma_mr_pool.h"

namespace ock {
namespace hcom {
uint64_t RDMAMemoryRegion::gPageSize = sysconf(_SC_PAGESIZE);

RResult RDMAMemoryRegion::Create(const std::string &name, RDMAContext *ctx, uint64_t size, RDMAMemoryRegion *&buf)
{
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Create rdma mr param invalid");
        return RR_PARAM_INVALID;
    }

    auto tmpBuf = new (std::nothrow) RDMAMemoryRegion(name, ctx, size);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        NN_LOG_ERROR("Failed to create rdma mr");
        return RR_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return RR_OK;
}

RResult RDMAMemoryRegion::Create(const std::string &name, RDMAContext *ctx, uintptr_t address, uint64_t size,
    RDMAMemoryRegion *&buf)
{
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Create rdma mr param invalid");
        return RR_PARAM_INVALID;
    }

    auto tmpBuf = new (std::nothrow) RDMAMemoryRegion(name, ctx, address, size);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        NN_LOG_ERROR("Failed to create rdma mr");
        return RR_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return RR_OK;
}

RResult RDMAMemoryRegion::Initialize()
{
    if (mMemReg != nullptr) {
        return RR_OK;
    }

    if (mRDMAContext == nullptr || mRDMAContext->mProtectDomain == nullptr) {
        NN_LOG_ERROR("Failed to initialize RDMAMemoryRegion as rdma context or pd is null");
        return RR_PARAM_INVALID;
    }
    char buf[NET_STR_ERROR_BUF_SIZE] = {0};
    ibv_mr *tmpMR = nullptr;
    if (mExternalMemory) {
        // the memory is allocated externally
        // register mr directly
        auto tmpBuf = reinterpret_cast<void *>(mBuf);
        tmpMR = HcomIbv::RegMr(mRDMAContext->mProtectDomain, tmpBuf, mSize,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
        if (tmpMR == nullptr) {
            NN_LOG_ERROR("Failed to register external memory for RDMAMemoryRegion " << mName << ", error " <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) << ", buffer " << tmpBuf);
            return RR_MR_REG_FAILED;
        }
    } else {
        // allocate memory
        if (gPageSize <= 0) {
            NN_LOG_ERROR("Failed to get system page size, page size: " << gPageSize);
            return RR_PARAM_INVALID;
        }
        auto tmpBuf = memalign(gPageSize, mSize);
        if (tmpBuf == nullptr) {
            NN_LOG_ERROR("Failed to allocate memory for RDMAMemoryRegion " << mName << " with size " << mSize);
            return RR_MEMORY_ALLOCATE_FAILED;
        }

        // register memory region to card
        tmpMR = HcomIbv::RegMr(mRDMAContext->mProtectDomain, tmpBuf, mSize,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
        if (tmpMR == nullptr) {
            free(tmpBuf);
            tmpBuf = nullptr;
            NN_LOG_ERROR("Failed to register memory for RDMAMemoryRegion " << mName << ", error "
                    << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return RR_MR_REG_FAILED;
        }

        mBuf = reinterpret_cast<intptr_t>(tmpBuf);
    }

    mMemReg = tmpMR;
    mLKey = mMemReg->lkey;

    return RR_OK;
}

void RDMAMemoryRegion::UnInitialize()
{
    if (mMemReg == nullptr) {
        return;
    }

    HcomIbv::DeregMr(mMemReg);
    if (!mExternalMemory) {
        if (mBuf != 0) {
            free(reinterpret_cast<void *>(mBuf));
        }
    }
    mRDMAContext->DecreaseRef();

    mMemReg = nullptr;
    mBuf = 0;
    mRDMAContext = nullptr;
}

}
}
#endif