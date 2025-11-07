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
#ifndef OCK_HCOM_CPP_HCOM_SPLIT_H
#define OCK_HCOM_CPP_HCOM_SPLIT_H

#include "hcom.h"

namespace ock {
namespace hcom {
using SerResult = int;

/// SplitSend 专用: 2G 为最大可发送包大小
const uint32_t SERVICE_MAX_TOTAL_LENGTH = 2U * 1024 * 1024 * 1024;

SerResult SyncSpliceMessage(UBSHcomNetResponseContext &ctx, UBSHcomNetEndpoint *ep, int32_t timeout,
    std::string &acc, void *&data, uint32_t &dataLen);

enum class SpliceMessageResultType {
    OK,
    ERROR,
    INDETERMINATE,
};

}  // namespace hcom
}  // namespace ock

#endif  // OCK_HCOM_CPP_HCOM_SPLIT_H
