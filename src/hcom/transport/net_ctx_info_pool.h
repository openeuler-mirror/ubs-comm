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

#include <atomic>

#include "hcom_def.h"
#include "net_mem_pool_fixed.h"

namespace ock {
namespace hcom {
template <typename T>
class OpContextInfoPool {
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
        NN_LOG_INFO("[OPCTX-TRACE] op context pool initialized with mem pool " << mOpCtxMemPool.Get() << " protocol "
                                                                               << static_cast<int>(mProtocol));
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
    /*
     * [leak-trace] GetOrReturn is the ONLY choke point    /*
     * [leak-trace] GetOrReturn is the ONLY choke point where op contexts are taken from /
     * given back to the pool. Counting here answers the two leak questions:
     *   1) is Return() (invoked by the holder object's destructor, e.g. service_callback.h)
     *      actually called? sReturn vs sGet answers this -> "destructor not called" if
     *      sReturn << sGet.
     *   2) is the block returned here handed back to the SHARED pool, or pinned in this
     *      thread's NetTCacheFixed? pool-outstanding (printed by HCOM_OPCTX_TRACE via
     *      NetMemPoolFixed::OutstandingBlocks) answers this -> "called but not returned to
     *      shared" if pool-outstanding climbs while sReturn ~= sGet.
     *
     * DEFAULT PATH (bypass): when the pool's NetMemPoolTlsPolicy::enabled is false, GetOrReturn
     * uses TCAllocOne/TCFreeOne straight to the shared free-list (no per-thread cache, no
     * cross-thread pinning). When enabled, it uses the per-thread NetTCacheFixed instead. The
     * -DHCOM_OPCTX_LEAK_TRACE build flag is retained for compatibility.
     * observable in production without a special build.
     */
    /*
     * Verbose per-call trace. Always compiled in and emitted as a sampled WARN (every
     * 64K operations) so the op-context pool's Get/Return balance is observable in a
     * WARN-only deployment. The -DHCOM_OPCTX_LEAK_TRACE build flag is retained for
     * compatibility but no longer changes behavior.
     */
#define HCOM_OPCTX_TRACE(pool, tag, get, ctx)                                                                        \
    do {                                                                                                             \
        if (NN_UNLIKELY((sOps.fetch_add(1, std::memory_order_relaxed) & 0xFFFFULL) == 0)) {                          \
            NN_LOG_WARN("[OPCTX-TRACE] " << (tag) << " pool " << static_cast<void *>(pool) << " get " << sGet.load() \
                                         << ", return " << sReturn.load() << ", destructor-outstanding "             \
                                         << (sGet.load() - sReturn.load()) << ", pool-outstanding "                  \
                                         << (pool != nullptr ? pool->OutstandingBlocks() : 0) << ", last ctx "       \
                                         << static_cast<void *>(ctx));                                               \
        }                                                                                                            \
    } while (0)

    /*
     * alloc/free in the same function to make sure use the same thread_local variable.
     * POLICY-DRIVEN: GetOrReturn branches on the pool's NetMemPoolTlsPolicy::enabled.
     * Enabled => per-thread NetTCacheFixed (observe destructor vs pinning via HCOM_OPCTX_TRACE).
     * Disabled (default) => TCAllocOne/TCFreeOne bypass straight to the shared free-list.
     * See HCOM_OPCTX_TRACE / the [leak-trace] comment above for the two hypotheses.
     */
    inline T *GetOrReturn(T *returnCtx, bool get = true)
    {
        /* [leak-trace] program-wide (per pool type) get/return/outstanding counters used by
           HCOM_OPCTX_TRACE below. Kept as function-local statics (NOT members) so the class
           stays copyable — callers pass OpContextInfoPool by value (e.g.
           SetSockOpContextInfoPool). C++11 merges inline function-local statics into a single
           program-wide instance per template instantiation. */
        static std::atomic<uint64_t> sGet{0};
        static std::atomic<uint64_t> sReturn{0};
        static std::atomic<uint64_t> sOps{0};

        auto *pool = mOpCtxMemPool.Get();
        if (NN_UNLIKELY(pool == nullptr)) {
            static std::atomic<bool> sNullWarned{false};
            if (!sNullWarned.exchange(true)) {
                NN_LOG_WARN("[OPCTX-TRACE] op context pool is null; Get/Return cannot proceed");
            }
            return nullptr;
        }

        /* POLICY-DRIVEN: when the pool's NetMemPoolTlsPolicy::enabled is true, use the
           per-thread NetTCacheFixed (UDS and non-UDS share the same policy); otherwise take
           the DEFAULT bypass path (TCAllocOne/TCFreeOne straight to the shared free-list),
           which is safe against cross-thread pinning. Both paths keep the destructor-return
           accounting (sGet/sReturn) and the HCOM_OPCTX_TRACE balance check. */
        if (pool->TlsPolicy().enabled) {
            if (mProtocol == UBSHcomNetDriverProtocol::UDS) {
                static thread_local NetTCacheFixed udsThreadCache(pool, pool->TlsPolicy());
                if (get) {
                    T *ctx = udsThreadCache.Allocate<T>();
                    sGet.fetch_add(1, std::memory_order_relaxed);
                    HCOM_OPCTX_TRACE(pool, "get", true, ctx);
                    return ctx;
                }
                udsThreadCache.Free<T>(returnCtx);
                sReturn.fetch_add(1, std::memory_order_relaxed);
                HCOM_OPCTX_TRACE(pool, "return", false, returnCtx);
                return nullptr;
            }

            static thread_local NetTCacheFixed threadCache(pool, pool->TlsPolicy());
            if (get) {
                T *ctx = threadCache.Allocate<T>();
                sGet.fetch_add(1, std::memory_order_relaxed);
                HCOM_OPCTX_TRACE(pool, "get", true, ctx);
                return ctx;
            }
            threadCache.Free<T>(returnCtx);
            sReturn.fetch_add(1, std::memory_order_relaxed);
            HCOM_OPCTX_TRACE(pool, "return", false, returnCtx);
            return nullptr;
        }

        /* DEFAULT bypass path: TCAllocOne/TCFreeOne go straight to the shared free-list.
           pool-outstanding (printed by HCOM_OPCTX_TRACE) stays ~0 because every block is
           returned to shared. */
        if (get) {
            T *ctx = pool->TCAllocOne<T>();
            sGet.fetch_add(1, std::memory_order_relaxed);
            HCOM_OPCTX_TRACE(pool, "get", true, ctx);
            return ctx;
        }
        pool->TCFreeOne<T>(returnCtx);
        sReturn.fetch_add(1, std::memory_order_relaxed);
        HCOM_OPCTX_TRACE(pool, "return", false, returnCtx);
        return nullptr;
    }

    NetMemPoolFixedPtr mOpCtxMemPool;
    /* NOTE: mProtocol is NOT initialized here, Initialize(pool) leaves it indeterminate, see analysis doc */
    UBSHcomNetDriverProtocol mProtocol = UBSHcomNetDriverProtocol::UNKNOWN;
};
} // namespace hcom
} // namespace ock

#endif // COMMUNICATION_NET_CTX_INFO_POOL_H