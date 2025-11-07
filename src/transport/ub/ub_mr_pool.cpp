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
#include <malloc.h>

#include "ub_common.h"
#include "ub_mr_pool.h"
#include "under_api/urma/urma_api_wrapper.h"
#include "under_api/obmm/obmm_api_wrapper.h"

namespace ock {
namespace hcom {
uint64_t UBMemoryRegion::gPageSize = sysconf(_SC_PAGESIZE);

UResult UBMemoryRegion::Create(const std::string &name, UBContext *ctx, uint64_t size, UBMemoryRegion *&buf)
{
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to create ub memory region as ctx is nullptr");
        return UB_PARAM_INVALID;
    }

    auto tmpBuf = new (std::nothrow) UBMemoryRegion(name, ctx, size, 0, 0);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        NN_LOG_ERROR("Failed to create ub memory region");
        return UB_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return UB_OK;
}

UResult UBMemoryRegion::Create(const std::string &name, UBContext *ctx, uintptr_t address, uint64_t size,
    UBMemoryRegion *&buf)
{
    if (NN_UNLIKELY(ctx == nullptr)) {
        NN_LOG_ERROR("Failed to create ub memory region as ctx is nullptr");
        return UB_PARAM_INVALID;
    }

    auto tmpBuf = new (std::nothrow) UBMemoryRegion(name, ctx, address, size);
    if ((NN_UNLIKELY(tmpBuf == nullptr))) {
        NN_LOG_ERROR("Failed to create ub memory region");
        return UB_NEW_OBJECT_FAILED;
    }

    buf = tmpBuf;

    return UB_OK;
}

UResult UBMemoryRegion::Initialize()
{
    if (mMemSeg != nullptr) {
        return UB_OK;
    }

    if (mUBContext == nullptr) {
        NN_LOG_ERROR("Failed to initialize UBMemoryRegion as ctx is nullptr");
        return UB_PARAM_INVALID;
    }

    urma_target_seg_t *tmpMR = nullptr;

    urma_reg_seg_flag_t flag{};
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;

    urma_seg_cfg_t seg_cfg{};
    seg_cfg.len = mSize;
    seg_cfg.flag = flag;

    if (mExternalMemory) {
        // the memory is allocated externally
        // register mr directly
        NN_LOG_WARN("externally allocated memory");
        auto tmpBuf = reinterpret_cast<uint64_t>(mBuf);
        seg_cfg.va = tmpBuf;
        tmpMR = HcomUrma::RegisterSeg(mUBContext->mUrmaContext, &seg_cfg);
        if (tmpMR == nullptr) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to register ex mem for UBMemoryRegion " << mName << " error " <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return UB_MR_REG_FAILED;
        }
    } else {
        // allocate memory
        if (gPageSize <= 0) {
            NN_LOG_ERROR("Failed to get page size from system, page size: " << gPageSize);
            return UB_PARAM_INVALID;
        }
        auto tmpBuf = memalign(gPageSize, mSize);
        if (tmpBuf == nullptr) {
            NN_LOG_ERROR("Failed to allocate memory for UBMemoryRegion " << mName << " with size " << mSize);
            return UB_MEMORY_ALLOCATE_FAILED;
        }

        seg_cfg.va = reinterpret_cast<uint64_t>(tmpBuf);
        // register memory region to card
        tmpMR = HcomUrma::RegisterSeg(mUBContext->mUrmaContext, &seg_cfg);
        if (tmpMR == nullptr) {
            free(tmpBuf);
            tmpBuf = nullptr;
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to register memory for UBMemoryRegion " << mName << " error " <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return UB_MR_REG_FAILED;
        }
        mBuf = reinterpret_cast<uintptr_t>(tmpBuf);
    }

    mMemSeg = tmpMR;

    return UB_OK;
}

UResult UBMemoryRegion::InitializeForOneSide()
{
    if (mMemSeg != nullptr) {
        return UB_OK;
    }

    if (mUBContext == nullptr) {
        NN_LOG_ERROR("Failed to initialize UBMemoryRegion as UBContex is null");
        return UB_PARAM_INVALID;
    }

    urma_target_seg_t *tmpMR = nullptr;

    urma_reg_seg_flag_t flag{};
    flag.bs.access = URMA_ACCESS_READ | URMA_ACCESS_WRITE;
    flag.bs.token_policy = URMA_TOKEN_PLAIN_TEXT;

    uint32_t tokenValue = GenerateSecureRandomUint32();
    urma_seg_cfg_t seg_cfg{};
    seg_cfg.len = mSize;
    seg_cfg.token_value = {tokenValue};
    seg_cfg.flag = flag;

    if (mExternalMemory) {
        // the memory is allocated externally
        // register mr directly
        auto tmpBuf = reinterpret_cast<uint64_t>(mBuf);
        seg_cfg.va = tmpBuf;
        tmpMR = HcomUrma::RegisterSeg(mUBContext->mUrmaContext, &seg_cfg);
        if (tmpMR == nullptr) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to register external memory for UBMemoryRegion " << mName << ", error " <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) <<
                ", buffer " << tmpBuf << " with size " << mSize);
            return UB_MR_REG_FAILED;
        }
    } else {
        // allocate memory
        if (gPageSize <= 0) {
            NN_LOG_ERROR("Failed to get system page size, page size: " << gPageSize);
            return UB_PARAM_INVALID;
        }
        auto tmpBuf = memalign(gPageSize, mSize);
        if (tmpBuf == nullptr) {
            NN_LOG_ERROR("Failed to allocate memory for UBMemoryRegion " << mName << " with size " << mSize);
            return UB_MEMORY_ALLOCATE_FAILED;
        }
        seg_cfg.va = reinterpret_cast<uint64_t>(tmpBuf);
        // register memory region to card
        tmpMR = HcomUrma::RegisterSeg(mUBContext->mUrmaContext, &seg_cfg);
        if (tmpMR == nullptr) {
            free(tmpBuf);
            tmpBuf = nullptr;
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to register memory for UBMemoryRegion " << mName << ", error " <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE) << " with size " << mSize);
            return UB_MR_REG_FAILED;
        }

        mBuf = reinterpret_cast<uintptr_t>(tmpBuf);
    }

    mMemSeg = tmpMR;
    mLKey = (static_cast<uint64_t>(tokenValue) << NN_NO32) | (tmpMR->seg.token_id);

    return UB_OK;
}

UResult UBMemoryRegion::InitializeWithPA(unsigned long memid)
{
    int fd_e = HcomObmm::ObmmOpen(memid);
    if (fd_e < 0) {
        NN_LOG_ERROR("Failed to get fd with memid " << memid << " errno " << errno);
        return UB_MEMORY_ALLOCATE_FAILED;
    }
    mMemFd = fd_e;
    auto tmpBuf = mmap(NULL, mSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_e, 0);
    if (tmpBuf == MAP_FAILED) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("mmap error: " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        close(fd_e);
        return UB_MEMORY_ALLOCATE_FAILED;
    }

    mBuf = reinterpret_cast<uintptr_t>(tmpBuf);
    mGetBufWithMapping = true;
    return UB_OK;
}

void UBMemoryRegion::UnInitialize()
{
    if (mMemSeg != nullptr) {
        HcomUrma::UnregisterSeg(mMemSeg);
    }

    if (!mExternalMemory && mBuf != 0) {
        free(reinterpret_cast<void *>(mBuf));
    }
    mUBContext->DecreaseRef();

    mMemSeg = nullptr;
    mBuf = 0;
    mUBContext = nullptr;
}
}
}

#endif
