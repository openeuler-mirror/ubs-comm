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

#include <cstdint>

namespace ock {
namespace ubs {
class Referable {
public:
    Referable() = default;
    virtual ~Referable() = default;

    void IncreaseRef()
    {
        refCount_.fetch_add(1, std::memory_order_relaxed);
    }

    void DecreaseRef()
    {
        // delete itself if reference count equal to 0
        if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 0) {
            delete this;
        }
    }

protected:
    std::atomic<int16_t> refCount_{0};
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_REF_H
