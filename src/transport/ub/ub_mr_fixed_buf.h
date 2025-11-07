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

#ifndef HCOM_UB_MR_FIXED_BUF_H
#define HCOM_UB_MR_FIXED_BUF_H
#ifdef UB_BUILD_ENABLED

#include "ub_mr_pool.h"

namespace ock {
namespace hcom {

class UBMemoryRegionFixedBuffer : public UBMemoryRegion {
public:
    static UResult Create(const std::string &name, UBContext *ctx, uint32_t singleSegSize, uint32_t segCount,
        unsigned long memid, UBMemoryRegionFixedBuffer *&buf);

public:
    UBMemoryRegionFixedBuffer(const std::string &name, UBContext *ctx, unsigned long memid, uint32_t singleSegSize,
        uint32_t segCount)
        : UBMemoryRegion(name, ctx, static_cast<uint64_t>(singleSegSize) * static_cast<uint64_t>(segCount), memid, 0),
          mSingleSegSize(singleSegSize),
          mSegCount(segCount)
    {
        OBJ_GC_INCREASE(UBMemoryRegionFixedBuffer);
    }

    ~UBMemoryRegionFixedBuffer() override
    {
        UnInitialize();
        OBJ_GC_DECREASE(UBMemoryRegionFixedBuffer);
    }

    UResult Initialize() override;

    inline bool GetFreeBuffer(uintptr_t &item)
    {
        return mLinkList.Pop(item);
    }

    inline bool ReturnBuffer(uintptr_t value)
    {
        mLinkList.PushFront(value);
        return true;
    }

    inline bool GetFreeBufferN(uintptr_t *&items, uint32_t n)
    {
        return mLinkList.PopN(items, n);
    }

    inline uint32_t GetSingleSegSize() const
    {
        return mSingleSegSize;
    }

    std::string ToString()
    {
        std::ostringstream oss;
        oss << "buf-address " << mBuf << ", mSingleSegSize " << mSingleSegSize << ", mSegCount " << mSegCount <<
            ", total buf size " << mSize;
        return oss.str();
    }

protected:
    void UnInitialize() override;

private:
    uint32_t mSingleSegSize = MR_FIXED_POOL_DEFAULT_SEG_SIZE;
    uint32_t mSegCount = MR_FIXED_POOL_DEFAULT_SEG_COUNT;

    // uintptr_p store the start address of each mr segment
    NetBucketLinkedList mLinkList;
};
}
}
#endif
#endif // HCOM_UB_MR_FIXED_BUF_H