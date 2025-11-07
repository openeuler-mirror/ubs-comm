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

#ifndef OCK_HCOM_SOCK_BUFF_H
#define OCK_HCOM_SOCK_BUFF_H

#include <sys/ioctl.h>
#include <linux/sockios.h>
 
#include "net_common.h"

namespace ock {
namespace hcom {

/* ***************************************************************************************************** */
/*
 * @brief sock buffer for receive
 */
class SockBuff {
public:
    SockBuff()
    {
        OBJ_GC_INCREASE(SockBuff);
    }

    ~SockBuff()
    {
        if (NN_LIKELY(mBuf != nullptr)) {
            free(mBuf);
            mBuf = nullptr;
        }

        OBJ_GC_DECREASE(SockBuff);
    }

    inline bool ExpandIfNeed(uint32_t size)
    {
        if (NN_UNLIKELY(size == NN_NO0)) {
            NN_LOG_ERROR("Invalid size 0");
            return false;
        }

        if (NN_UNLIKELY(size > mSize)) {
            /*
             * 1 free the previous allocated memory
             * 2 allocate new one
             * 3 set mSize to new size
             */
            if (mBuf != nullptr) {
                free(mBuf);
            }

            mBuf = malloc(size);
            if (NN_LIKELY(mBuf != nullptr)) {
                mSize = size;
                return true;
            }

            /* return false, if not allocated */
            mBuf = nullptr;
            mSize = NN_NO0;
            return false;
        }

        return true;
    }

    inline void *Data() const
    {
        return mBuf;
    }

    inline uintptr_t DataIntPtr() const
    {
        return reinterpret_cast<uintptr_t>(mBuf);
    }

    inline void ActualDataSize(uint32_t size)
    {
        mActualDataSize = size;
    }

    inline uint32_t ActualDataSize() const
    {
        return mActualDataSize;
    }

    inline uint32_t Size() const
    {
        return mSize;
    }

private:
    void *mBuf = nullptr;
    uint32_t mSize = 0;
    uint32_t mActualDataSize = 0;
};
}
}
#endif