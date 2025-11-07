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
#ifndef OCK_HCOM_ENV_H
#define OCK_HCOM_ENV_H

#include "net_common.h"

namespace ock {
namespace hcom {

class HcomEnv {
public:
    // 双边inline阈值，inline上限和网卡有关（一般256Bytes），超出网卡上限的话，创建QP时会报错，如果设置太大在创建QP时提醒用户
    static inline uint32_t InlineThreshold()
    {
        static long threshold = [] () {
            auto value = NetFunc::NN_GetLongEnv("HCOM_INLINE_THRESHOLD", 0, UINT32_MAX, 0);
            NN_LOG_INFO("Inline threshold is: " << value);
            return static_cast<long>(value);
        }();
        return threshold;
    }

    // 双边rndv阈值，默认是UINT32_MAX，用户不设置默认不开启
    static inline uint32_t RndvThreshold()
    {
        static long threshold = [] () {
            auto value = NetFunc::NN_GetLongEnv("HCOM_RNDV_THRESHOLD", 0, UINT32_MAX, UINT32_MAX);
            NN_LOG_INFO("Rndv Threshold is: " << value);
            return static_cast<long>(value);
        }();
        return threshold;
    }
};

}
}

#endif