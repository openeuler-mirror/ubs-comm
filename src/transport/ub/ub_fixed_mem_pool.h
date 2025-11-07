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
#ifndef HCOM_UB_FIXED_MEM_POOL_H
#define HCOM_UB_FIXED_MEM_POOL_H
#ifdef UB_BUILD_ENABLED

#include "ub_common.h"
#include "net_bucket_linked_list.h"
#include "net_util.h"

namespace ock {
namespace hcom {
/*
 * Mini block allocated to end user
 */
struct UBMemPoolMinBlock {
    UBMemPoolMinBlock *next = nullptr;  /* link to next min block */
};

/*
 * Mem pool for fixed block size
 * UBFixedMemPool is now used only by public jetty
 */
class UBFixedMemPool {
public:
    UBFixedMemPool(uint16_t blkSize, uint16_t blkCnt = NN_NO64): mBlkSize(blkSize), mBlkCnt(blkCnt)
    {
        mTotalSize = mBlkSize * mBlkCnt;
    }
    ~UBFixedMemPool()
    {
        UnInitialize();
    }
    UResult Initialize();
    UResult MakeFreeList();
    bool GetFreeBuffer(uintptr_t &buf);
    bool ReturnBuffer(uintptr_t buf);
    void UnInitialize()
    {
        if (mBuf != 0) {
            free(reinterpret_cast<void *>(mBuf));
            mBuf = 0;
        }
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    uint16_t mBlkSize = NN_NO128;
    uint16_t mBlkCnt = NN_NO64;
    uint64_t mTotalSize = NN_NO8192;
    uintptr_t mBuf = 0;
    UBMemPoolMinBlock mHead {};
    std::mutex mMutex;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}
#endif
#endif // HCOM_UB_FIXED_MEM_POOL_H