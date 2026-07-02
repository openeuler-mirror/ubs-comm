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
#ifndef UBS_COMM_UBSOCKET_ERRNO_H
#define UBS_COMM_UBSOCKET_ERRNO_H

#include "ubsocket_defines.h"

namespace ock {
namespace ubs {
enum InnerCode : int32_t
{
    UBS_OK = 0,
    UBS_ERROR = static_cast<int32_t>(0x80000001u),
    UBS_DL_LOAD_SYM_FAILED = static_cast<int32_t>(0x80000100u),
    UBS_DL_OPEN_LIB_FAILED,
    UBS_INVALID_PARAM,
    UBS_MALLOC_FAILED,

    // UMQ 相关错误
    UBS_UMQ_CREATE = static_cast<int32_t>(0x80000401u),
    UBS_UMQ_DEV_ADD,
    UBS_UMQ_BIND_INFO_GET,
    UBS_UMQ_BIND,
    UBS_UMQ_BUF_ALLOC,
    UBS_UMQ_POST,
    UBS_UMQ_MAX,

    // ubsocket 相关错误
    UBS_SET_DEV_INFO = static_cast<int32_t>(0x80001001u),
    UBS_PREFILL_RX,
    UBS_INIT_SHARED_JFR_RX_QUEUE,
    UBS_NEW_SOCKET_FD,
    UBS_TCP_EXCHANGE,
    UBS_UB_ACCEPT,
    UBS_NO_MAIN_UMQ,
    UBS_MAX,
    UBS_UB_DEV_ERROR,
    UBS_UMQ_ERROR,
    UBS_CONN_RETRY_FAILED,
    UBS_CONN_ROUTE,

    // 特殊标记位，使用 bit 30 和 bit 29，最高位 (bit 31) 为 1 表示负数错误
    UBS_RETRYABLE_MASK = 0x40000000,  // 可重试标记 (bit 30)
    UBS_DEGRADABLE_MASK = 0x20000000, // 可降级标记 (bit 29)
};

constexpr int32_t UBS_FLAGS_MASK = static_cast<int32_t>(UBS_RETRYABLE_MASK) | static_cast<int32_t>(UBS_DEGRADABLE_MASK);

// static_cast<uint32_t> 会保留最高位的符号位，确保转换回 int32_t 时仍然为负数
inline InnerCode operator|(InnerCode lhs, InnerCode rhs)
{
    const uint32_t lhs_u = static_cast<uint32_t>(lhs);
    const uint32_t rhs_u = static_cast<uint32_t>(rhs);
    const uint32_t e = lhs_u | rhs_u;
    return static_cast<InnerCode>(static_cast<int32_t>(e));
}

inline InnerCode operator-(InnerCode lhs, InnerCode rhs)
{
    const uint32_t lhs_u = static_cast<uint32_t>(lhs);
    const uint32_t rhs_u = static_cast<uint32_t>(rhs);
    const uint32_t e = (lhs_u & (~rhs_u));
    return static_cast<InnerCode>(static_cast<int32_t>(e));
}

inline InnerCode WithoutFlags(InnerCode err)
{
    return err - InnerCode::UBS_RETRYABLE_MASK - InnerCode::UBS_DEGRADABLE_MASK;
}

/**
 * @brief 检查结果是否为成功
 * @param result 要检查的结果
 * @return true 表示成功，false 表示失败
 */
inline bool IsOk(Result result)
{
    return (result & ~UBS_FLAGS_MASK) == static_cast<Result>(UBS_OK);
}
/**
 * @brief 检查错误是否可重试
 * @param result 要检查的结果
 * @return true 表示可重试，false 表示不可重试
 */
inline bool IsRetryable(Result result)
{
    return (result & static_cast<Result>(UBS_RETRYABLE_MASK)) != 0;
}
/**
 * @brief 检查错误是否可降级
 * @param result 要检查的结果
 * @return true 表示可降级，false 表示不可降级
 */
inline bool IsDegradable(Result result)
{
    return (result & static_cast<Result>(UBS_DEGRADABLE_MASK)) != 0;
}
/**
 * @brief 获取纯错误码（去掉标记位）
 * @param result 包含标记位的结果
 * @return 纯错误码
 */
inline Result GetPureCode(Result result)
{
    return result & ~UBS_FLAGS_MASK;
}

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_ERRNO_H
