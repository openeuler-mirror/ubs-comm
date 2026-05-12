/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_REF_H
#define UBS_COMM_UBSOCKET_REF_H

#include <atomic>
#include <cstdint>

#include "ubsocket_defines.h"

namespace ock {
namespace ubs {
/*
 * 1 base class smart ptr
 * 2 macro for master ptr if not use base class
 */
class Referable {
public:
    Referable() = default;
    virtual ~Referable() = default;

    void IncreaseRef();
    void DecreaseRef();

protected:
    std::atomic<int16_t> ref_count_{0};
};

ALWAYS_INLINE void Referable::IncreaseRef()
{
    ref_count_.fetch_add(1, std::memory_order_relaxed);
}

ALWAYS_INLINE void Referable::DecreaseRef()
{
    // delete itself if reference count equal to 0
    if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 0) {
        delete this;
    }
}

#define DECLARE_REF_COUNT_VARIABLE std::atomic<int16_t> ref_count_{0};

#define DEFINE_INCREASE_REF_FUNC                            \
    ALWAYS_INLINE void IncreaseRef()                        \
    {                                                       \
        ref_count_.fetch_add(1, std::memory_order_relaxed); \
    }

#define DEFINE_DECREASE_REF_FUNC                                       \
    ALWAYS_INLINE void IncreaseRef()                                   \
    {                                                                  \
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 0) { \
            delete this;                                               \
        }                                                              \
    }

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_REF_H
