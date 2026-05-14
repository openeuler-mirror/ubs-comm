/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "umq_setting.h"
#include <cstdlib>
#include <string>
#include "../../common/ubsocket_global_setting.h"

namespace ock {
namespace ubs {
// 初始化静态成员
umq_trans_mode_t m_trans_mode = UMQ_TRANS_MODE_UB;
umq_buf_block_size_t UmqSetting::IO_BLOCK_TYPE = BLOCK_SIZE_8K;

void UmqSetting::Init() noexcept
{
    IO_BLOCK_TYPE = ParseBlockType(GlobalSetting::UBS_BLOCK_TYPE_STR);
}

uint32_t UmqSetting::GetIOBufSize() noexcept
{
    switch (IO_BLOCK_TYPE) {
        case BLOCK_SIZE_8K:  return SIZE_8K - IOBUF_DIFF;
        case BLOCK_SIZE_16K: return SIZE_16K - IOBUF_DIFF;
        case BLOCK_SIZE_32K: return SIZE_32K - IOBUF_DIFF;
        case BLOCK_SIZE_64K: return SIZE_64K - IOBUF_DIFF;
        default:
            return SIZE_8K - IOBUF_DIFF;
    }
}

uint64_t UmqSetting::FloorMask() noexcept
{
    switch (IO_BLOCK_TYPE) {
        case BLOCK_SIZE_8K:  return SIZE_8K - MASK_DIFF;
        case BLOCK_SIZE_16K: return SIZE_16K - MASK_DIFF;
        case BLOCK_SIZE_32K: return SIZE_32K - MASK_DIFF;
        case BLOCK_SIZE_64K: return SIZE_64K - MASK_DIFF;
        default:
            return SIZE_8K - MASK_DIFF;
    }
}

umq_buf_block_size_t UmqSetting::ParseBlockType(const std::string& typeStr) noexcept
{
    if (typeStr == DEFAULT_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_8K;
    } else if (typeStr == SMALL_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_16K;
    } else if (typeStr == MEDIUM_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_32K;
    } else if (typeStr == LARGE_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_64K;
    }
    // 如果字符串不匹配，返回默认值
    return BLOCK_SIZE_8K;
}
}
} // namespace ock::ubs