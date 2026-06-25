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
#ifndef UBS_COMM_UBSOCKET_VERSION_H
#define UBS_COMM_UBSOCKET_VERSION_H

#include <cstdint>

#include "ubsocket_logger.h"

namespace ock {
namespace ubs {

// ======================== 协议版本号编码 ========================
// 32-bit Protocol Version Number:
// ┌──────────┬──────────────┬──────────────────┐
// │ Major(6) │  Minor(12)   │   Patch(14)      │
// │  bits    │   bits       │    bits           │
// └──────────┴──────────────┴──────────────────┘
//
// 版本号来源: 由 CMake/Bazel 从 UBSOCKET_VERSION 文件自动生成 ubsocket_version_defs.h，
// 全代码仓统一一个版本号，协议版本与软件发布版本一致。
//
// 位提取宏带 PROTOCOL_ 前缀，与版本常量区分:
//   本文件: UBS_PROTOCOL_VERSION_MAJOR(v) (位操作函数宏)
//   ubsocket_version_defs.h: UBS_VERSION_MAJOR=1 (整数常量)

#include "ubsocket_version_defs.h"

#define UBS_PROTOCOL_VERSION_MAJOR(v) ((v >> 26) & 0x3F)  // 6 bits
#define UBS_PROTOCOL_VERSION_MINOR(v) ((v >> 14) & 0xFFF) // 12 bits
#define UBS_PROTOCOL_VERSION_PATCH(v) (v & 0x3FFF)        // 14 bits
#define UBS_MAKE_PROTOCOL_VERSION(major, minor, patch) (((major) << 26) | ((minor) << 14) | (patch))

// 当前协议版本 — 复用编译定义的软件版本号
// UBS_VERSION_PATCH 映射到补丁版本字段
constexpr uint32_t UBS_PROTOCOL_VERSION =
    UBS_MAKE_PROTOCOL_VERSION(UBS_VERSION_MAJOR, UBS_VERSION_MINOR, UBS_VERSION_PATCH);

// 字符串版本号宏 — 供ubsocket_version() API使用
#define UBS_LIB_VERSION UBS_VERSION_STR
#define UBS_LIB_VERSION_FULL UBS_VERSION_FULL_STR

// ======================== 版本校验结果 ========================

enum class VersionCheckResult : int32_t
{
    kCompatible = 0,     // Major一致，可正常通信
    kMajorMismatch = -1, // Major不一致，协议格式不兼容
    kRecvFailed = -2,    // 接收version字段失败
};

// ======================== 版本协商函数 ========================

// 合并校验+协商：Major不一致返回kMajorMismatch，一致则计算negotiated_version
// 协商公式：Major一致→取较低Minor，取较低Patch
inline VersionCheckResult NegotiateVersion(uint32_t local_version, uint32_t peer_version, uint32_t &negotiated_version)
{
    uint32_t local_major = UBS_PROTOCOL_VERSION_MAJOR(local_version);
    uint32_t local_minor = UBS_PROTOCOL_VERSION_MINOR(local_version);
    uint32_t local_patch = UBS_PROTOCOL_VERSION_PATCH(local_version);
    uint32_t peer_major = UBS_PROTOCOL_VERSION_MAJOR(peer_version);
    uint32_t peer_minor = UBS_PROTOCOL_VERSION_MINOR(peer_version);
    uint32_t peer_patch = UBS_PROTOCOL_VERSION_PATCH(peer_version);

    if (peer_major != local_major) {
        UBS_VLOG_WARN("Version negotiate failed: major mismatch, local %u.%u.%u peer %u.%u.%u", local_major,
                      local_minor, local_patch, peer_major, peer_minor, peer_patch);
        return VersionCheckResult::kMajorMismatch;
    }
    uint32_t minor = (peer_minor < local_minor) ? peer_minor : local_minor;
    uint32_t patch = (peer_patch < local_patch) ? peer_patch : local_patch;
    negotiated_version = UBS_MAKE_PROTOCOL_VERSION(local_major, minor, patch);
    UBS_VLOG_INFO("Version negotiated: local %u.%u.%u peer %u.%u.%u -> %u.%u.%u", local_major, local_minor, local_patch,
                  peer_major, peer_minor, peer_patch, local_major, minor, patch);
    return VersionCheckResult::kCompatible;
}

// ======================== 版本校验函数 ========================

// 校验对端协商结果：Major必须一致，Minor/Patch不应高于本地（协商取较低值）
// Connector侧使用：收到Acceptor发来的negotiated_version后校验
inline VersionCheckResult ValidateNegotiatedVersion(uint32_t local_version, uint32_t negotiated_version)
{
    if (UBS_PROTOCOL_VERSION_MAJOR(negotiated_version) != UBS_PROTOCOL_VERSION_MAJOR(local_version)) {
        return VersionCheckResult::kMajorMismatch;
    }
    return VersionCheckResult::kCompatible;
}

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_VERSION_H