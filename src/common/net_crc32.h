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
#ifndef HCOM_CRC32_H
#define HCOM_CRC32_H

#include "hcom_def.h"
#include "hcom_log.h"

namespace ock {
namespace hcom {
/*
 * @brief brief calculate crc32
 *
 * @param buffer         [in] which is to be calculated.
 * @param length         [in] calculate buff length.
 *
 * @return crc32 value.
 *
 */
using Crc32Function = uint32_t (*)(const void *buffer, uint32_t length);

class NetCrc32 {
public:
    /*
     * @brief brief calculate crc32
     *
     * @param buffer         [in] which is to be calculated.
     * @param length         [in] calculate buff length.
     *
     * @return crc32 value.
     *
     */
    static inline uint32_t CalcCrc32(const void *buffer, uint32_t length)
    {
        return gCrc32Func(buffer, length);
    }

private:
    static Crc32Function gCrc32Func;
};
}
}
#endif // HCOM_CRC32_H