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

#ifndef HCOM_UB_JETTY_PTR_MAP_H
#define HCOM_UB_JETTY_PTR_MAP_H
#ifdef UB_BUILD_ENABLED

#include "ub_common.h"
#include "ub_urma_wrapper_jetty.h"

#include <tuple>

namespace ock {
namespace hcom {
/// JettyPtrMap 支持通过 urma jetty id 查找 `UBJetty*`
class JettyPtrMap {
public:
    JettyPtrMap() = default;

    JettyPtrMap(const JettyPtrMap &) = delete;
    JettyPtrMap(JettyPtrMap &&rhs) noexcept = delete;
    JettyPtrMap &operator=(const JettyPtrMap &) = delete;
    JettyPtrMap &operator=(JettyPtrMap &&rhs) noexcept = delete;

    ~JettyPtrMap()
    {
        if (mId2Jetty) {
            munmap(mId2Jetty, mId2JettySize * sizeof(UBJetty *));
            mId2Jetty = nullptr;
            mId2JettySize = 0;
        }
    }

    UResult Initialize()
    {
        const size_t jettyIdMax = NN_NO65536;
        void *id2Jetty = mmap(nullptr, jettyIdMax * sizeof(UBJetty *), PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
        if (id2Jetty == MAP_FAILED) {
            NN_LOG_ERROR("Unable to mmap with size: " << (jettyIdMax * sizeof(UBJetty *)));
            return UB_MEMORY_ALLOCATE_FAILED;
        }

        mId2Jetty = reinterpret_cast<UBJetty **>(id2Jetty);
        mId2JettySize = static_cast<uint32_t>(jettyIdMax);
        return UB_OK;
    }

    UBJetty *Lookup(uint32_t jettyId) const
    {
        if (NN_UNLIKELY(jettyId >= mId2JettySize)) {
            NN_LOG_ERROR("The given jetty id " << jettyId << " exceeds the size " << mId2JettySize
                                               << " when looking-up");
            return nullptr;
        }

        return mId2Jetty[jettyId];
    }

    UResult Emplace(uint32_t jettyId, UBJetty *jetty)
    {
        if (NN_UNLIKELY(jettyId >= mId2JettySize)) {
            NN_LOG_ERROR("The given jetty id " << jettyId << " exceeds the size " << mId2JettySize
                                               << " when inserting");
            return UB_ERROR;
        }

        mId2Jetty[jettyId] = jetty;
        return UB_OK;
    }

    UResult Modify(uint32_t jettyId, UBJetty *jetty)
    {
        if (NN_UNLIKELY(jettyId >= mId2JettySize)) {
            NN_LOG_ERROR("The given jetty id " << jettyId << " exceeds the size " << mId2JettySize
                                               << " when modifying");
            return UB_ERROR;
        }

        mId2Jetty[jettyId] = jetty;
        return UB_OK;
    }

    UResult Clear(uint32_t jettyId)
    {
        return Modify(jettyId, nullptr);
    }

private:
    UBJetty **mId2Jetty = nullptr;  ///< UBJetty::mUrmaJettyId -> UBJetty* 映射表
    uint32_t mId2JettySize = 0;     ///< mId2Jetty 映射表大小
};

} // namespace hcom
} // namespace ock

#endif  // HCOM_UB_JETTY_PTR_MAP_H
#endif