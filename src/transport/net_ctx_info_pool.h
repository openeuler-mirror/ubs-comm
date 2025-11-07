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
#ifndef COMMUNICATION_NET_CTX_INFO_POOL_H
#define COMMUNICATION_NET_CTX_INFO_POOL_H

#include "hcom_def.h"
#include "net_mem_pool_fixed.h"

namespace ock {
namespace hcom {
template <typename T> class OpContextInfoPool {
public:
    inline NResult Initialize(const NetMemPoolFixedPtr &opCtxMemPool)
    {
        mOpCtxMemPool = opCtxMemPool;
        return NN_OK;
    }

    inline NResult Initialize(const NetMemPoolFixedPtr &opCtxMemPool, const UBSHcomNetDriverProtocol t)
    {
        mOpCtxMemPool = opCtxMemPool;
        mProtocol = t;
        return NN_OK;
    }

    inline NResult UnInitialize()
    {
        mOpCtxMemPool.Set(nullptr);
        return NN_OK;
    }

    inline T *Get()
    {
        return GetOrReturn(nullptr);
    }

    inline void Return(T *info)
    {
        (void)GetOrReturn(info, false);
    }

private:
    /* alloc/free in the same function to make sure use the same thread_local variable */
    inline T *GetOrReturn(T *returnCtx, bool get = true)
    {
        if (mProtocol == UBSHcomNetDriverProtocol::UDS) {
            static thread_local NetTCacheFixed udsThreadCache(mOpCtxMemPool.Get());
            if (get) {
                return udsThreadCache.Allocate<T>();
            } else {
                udsThreadCache.Free<T>(returnCtx);
                return nullptr;
            }
        }

        static thread_local NetTCacheFixed threadCache(mOpCtxMemPool.Get());
        if (get) {
            return threadCache.Allocate<T>();
        } else {
            threadCache.Free<T>(returnCtx);
            return nullptr;
        }
    }

    NetMemPoolFixedPtr mOpCtxMemPool;
    UBSHcomNetDriverProtocol mProtocol;
};
}
}

#endif // COMMUNICATION_NET_CTX_INFO_POOL_H