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
#include <stdexcept>

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

#include "ubsocket_version_defs.h"

// 位宽上限 — Major(6bit) Minor(12bit) Patch(14bit)
constexpr uint32_t kProtocolMajorBits = 6;
constexpr uint32_t kProtocolMinorBits = 12;
constexpr uint32_t kProtocolPatchBits = 14;
constexpr uint32_t kProtocolMajorMax = 1u << kProtocolMajorBits;
constexpr uint32_t kProtocolMinorMax = 1u << kProtocolMinorBits;
constexpr uint32_t kProtocolPatchMax = 1u << kProtocolPatchBits;

// ======================== 版本一致性校验规则 ========================
//
// 编码: Major(6bit) | Minor(12bit) | Patch(14bit) = 32bit  (小端: Patch=bit0-13, Minor=bit14-25, Major=bit26-31)
//
// 兼容性判定:
//   Major 不一致 → kMajorMismatch  双方都回退到 TCP（Major 不兼容，线格式可能不同）
//   Minor 不一致 → kCompatible     双方使用较低版本号继续 RDMA/UB 通信（Min/Patch 向下兼容）
//   Patch 不一致 → kCompatible     同上
//
// 协商:
//   Accept 侧  → Negotiate(peer, &negotiated)   协商整体版本号
//   Connect 侧 → ValidateNegotiated(local)       校验Major版本是否一致

enum class VersionCheckResult : int32_t
{
    kCompatible = 0,     // Major一致，可正常通信
    kMajorMismatch = -1, // Major不一致，协议格式不兼容
    kRecvFailed = -2,    // 接收version字段失败
};

// 协议版本union: 位域存取(小端: patch低位→major高位) + uint32_t wire值
union UBSVersion {
    struct {
        uint32_t patch : 14; // bits 0-13
        uint32_t minor : 12; // bits 14-25
        uint32_t major : 6;  // bits 26-31
    };
    uint32_t whole;

    constexpr UBSVersion() : whole(UINT32_MAX) {}
    constexpr UBSVersion(uint32_t v) : whole(v) {}
    constexpr UBSVersion(uint32_t maj, uint32_t min, uint32_t pat) : patch(pat), minor(min), major(maj) {}

    constexpr uint32_t GetWhole() const
    {
        return whole;
    }

    friend std::ostream &operator<<(std::ostream &os, const UBSVersion &v)
    {
        return os << v.major << '.' << v.minor << '.' << v.patch;
    }

    // 协商: 取两边较低版本; Major不一致返回kMajorMismatch
    VersionCheckResult Negotiate(const UBSVersion &peer, UBSVersion &negotiated) const
    {
        if (major != peer.major) {
            UBS_SLOG_WARN("Version negotiate failed: major mismatch, local " << *this << " peer " << peer);
            return VersionCheckResult::kMajorMismatch;
        }
        negotiated = (whole < peer.whole) ? *this : peer;
        UBS_SLOG_DEBUG("Version negotiated: local " << *this << " peer " << peer << " -> " << negotiated);
        return VersionCheckResult::kCompatible;
    }

    // 校验协商结果: Major必须与local一致, Connector侧使用
    VersionCheckResult ValidateNegotiated(const UBSVersion &local) const
    {
        return (major != local.major) ? VersionCheckResult::kMajorMismatch : VersionCheckResult::kCompatible;
    }
};

// 当前协议版本 — 复用编译定义的软件版本号
constexpr UBSVersion UBS_PROTOCOL_VERSION(UBS_VERSION_MAJOR, UBS_VERSION_MINOR, UBS_VERSION_PATCH);
static_assert(UBS_VERSION_MAJOR < kProtocolMajorMax, "UBS_VERSION_MAJOR exceeds 6-bit limit (max 63)");
static_assert(UBS_VERSION_MINOR < kProtocolMinorMax, "UBS_VERSION_MINOR exceeds 12-bit limit (max 4095)");
static_assert(UBS_VERSION_PATCH < kProtocolPatchMax, "UBS_VERSION_PATCH exceeds 14-bit limit (max 16383)");

// 字符串化辅助宏
#define UBS_STR(x) #x
#define UBS_XSTR(x) UBS_STR(x)

// UBS_GIT_LAST_COMMIT 由 CMake config_last_commit.cmake 通过 add_compile_definitions 注入
// Bazel 构建中不注入此宏，fallback 为 "unknown"
#ifndef UBS_GIT_LAST_COMMIT
#define UBS_GIT_LAST_COMMIT unknown
#endif

// 短版本号 — ubsocket_version() API 的返回值
#define UBS_LIB_VERSION UBS_VERSION_STR

// 全版本号 — 含构建时间与commit hash，供 golden -v / ubsocket_version() 日志使用
#define UBS_LIB_VERSION_FULL                                                   \
    "library version: " UBS_VERSION_STR ", build time: " __DATE__ " " __TIME__ \
    ", commit: " UBS_XSTR(UBS_GIT_LAST_COMMIT)

} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_VERSION_H