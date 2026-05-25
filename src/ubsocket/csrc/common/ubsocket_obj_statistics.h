/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_OBJ_STATISTICS_H
#define UBS_COMM_UBSOCKET_OBJ_STATISTICS_H

#include "ubsocket_defines.h"

namespace ock {
namespace ubs {

enum ObjectEnum {
    /* ubsocket related */
    SOCKET = 0,

    /* urma related */
    URMA_CONTEXT,
    URMA_JETTY,
    URMA_JFS,
    URMA_JFR,
    URMA_JFC,

    /* umq related */
    UMQ_SOCKET,

    /* add before count */
    OBJECT_COUNT
};

#ifdef OBJ_COUNTING_ENABLED
#define OBJ_INC_COUNT(ENUM) ObjectStatistics::Instance().Increase(ENUM, #ENUM)
#define OBJ_DEC_COUNT(ENUM) ObjectStatistics::Instance().Decrease(ENUM)
#else
#define OBJ_INC_COUNT(ENUM)
#define OBJ_DEC_COUNT(ENUM)
#endif

class ObjectStatistics {
public:
    static ObjectStatistics &Instance()
    {
        static ObjectStatistics instance;
        return instance;
    }

    /**
     * Increase specific object count
     */
    void Increase(const ObjectEnum &o, const char *name) noexcept;

    /**
     * Decrease specific object count
     */
    void Decrease(const ObjectEnum &o) noexcept;

    /**
     * Dump count into string for print
     */
    std::string DumpStr() const noexcept;

public:
    std::atomic<int16_t> count_[OBJECT_COUNT]{};
    std::string name_[OBJECT_COUNT];
};

ALWAYS_INLINE void ObjectStatistics::Increase(const ObjectEnum &o, const char *name) noexcept
{
    auto tmp = count_[o].fetch_add(1, std::memory_order_relaxed);
    if (UNLIKELY(tmp <= 0)) {
        name_[o] = name;
    }
}

ALWAYS_INLINE void ObjectStatistics::Decrease(const ObjectEnum &o) noexcept
{
    count_[o].fetch_sub(1, std::memory_order_relaxed);
}

ALWAYS_INLINE std::string ObjectStatistics::DumpStr() const noexcept
{
#ifdef OBJ_COUNTING_ENABLED
    std::ostringstream oss;
    for (auto i = 0; i < OBJECT_COUNT; i++) {
        oss << "  " << name_[i] << ": " << count_[i] << std::endl;
    }
    return oss.str();
#else
    return "Object counting is not enabled, turn on with -DENABLE_OBJECT_COUNTING=ON during compile";
#endif
}
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_OBJ_STATISTICS_H
