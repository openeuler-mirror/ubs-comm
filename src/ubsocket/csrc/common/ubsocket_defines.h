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
#ifndef UBS_COMM_UBSOCKET_DEFINES_H
#define UBS_COMM_UBSOCKET_DEFINES_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>
#include <string>

namespace ock {
namespace ubs {

using Result = int32_t;

#define UBS_API __attribute__((visibility("default")))
#define ALWAYS_INLINE inline __attribute__((always_inline))

#ifndef LIKELY
#define LIKELY(x) (__builtin_expect(!!(x), 1) != 0)
#endif

#ifndef UNLIKELY
#define UNLIKELY(x) (__builtin_expect(!!(x), 0) != 0)
#endif

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define RPC_ADPT_FD_MAX (8192)

enum dev_schedule_policy
{
    ROUND_ROBIN = 1,
    CPU_AFFINITY = 2,
    CPU_AFFINITY_PRIORITY = 3,
};

enum ub_trans_mode
{
    RC_TP,
    RM_TP,
    RM_CTP,
    RC_CTP
};

// 描述在 Connect/Accept 间的握手状态
enum class UBHandshakeState : uint32_t
{
    kOK = 0,
    // 初次握手
    kSTART = 1,
    // 初次握手失败，再次尝试
    kRETRY = 2,
    // 再次握手失败，用以通知客户端需要降级成 TCP
    kRETRY_FAILED_CHECK_OTHER_ROUTE = 3,
    // UB 握手失败，降级至 TCP
    kDEGRADE = 4,
    // UB 握手失败
    kFAILED = 6,
};

enum ops_error_code
{
    OK,
    NORMAL_ERROR,
    FATAL_ERROR
};

enum class UBHandshakeMode : uint32_t
{
    TFO,
    UB_SOCK_OPT
};

typedef enum pool_type : uint8_t
{
    SINGLE,
    POOL
} pool_type_t;

/// | 选项             | 对应场景                                    | 说明                                                  |
/// |------------------|---------------------------------------------|-------------------------------------------------------|
/// | `BONDING_BACKUP` | 指定 bonding 设备，且 `backup_link=true`    | 通过 bonding 设备通信，bonding 本身提供主备冗余       |
/// | `BONDING_ROUTE`  | 指定 bonding 设备，但是 `backup_link=false` | 通过 bonding 设备获取裸设备路由信息，实际数据走裸设备 |
/// | `RAW_DEVICE`     | 指定裸设备                                  | 完全不依赖 bonding 设备，直接通过裸设备通信           |
enum class LinkSelectionPolicy : uint8_t
{
    BONDING_BACKUP = 0,
    BONDING_ROUTE,
    RAW_DEVICE,
};

enum class SplitTraceLevel : uint8_t
{
    LEVEL_NONE = 0,
    LEVEL_UBSOCKET = 1 << 0, // 0x01
    LEVEL_UMQ = 1 << 1,      // 0x02

    LEVEL_ALL = LEVEL_UBSOCKET | LEVEL_UMQ
};

// 重载按位与运算符，支持 enum class 直接参与位运算
inline SplitTraceLevel operator&(SplitTraceLevel lhs, SplitTraceLevel rhs)
{
    return static_cast<SplitTraceLevel>(static_cast<uint8_t>(lhs) & static_cast<uint8_t>(rhs));
}

// 重载按位或运算符，方便组合
inline SplitTraceLevel operator|(SplitTraceLevel lhs, SplitTraceLevel rhs)
{
    return static_cast<SplitTraceLevel>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

#ifndef TCP_UB_SOCKET_HANDSHAKE
#define TCP_UB_SOCKET_HANDSHAKE 144
#endif

#ifndef EID_FMT
#define EID_FMT "%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x"
#endif

#ifndef EID_RAW_ARGS
#define EID_RAW_ARGS(eid)                                                                                      \
    eid[0], eid[1], eid[2], eid[3], eid[4], eid[5], eid[6], eid[7], eid[8], eid[9], eid[10], eid[11], eid[12], \
        eid[13], eid[14], eid[15]
#endif

#ifndef EID_ARGS
#define EID_ARGS(eid) EID_RAW_ARGS((eid).raw)
#endif

#define ALWAYS_INLINE inline __attribute__((always_inline))

#define IO_SIZE_MB (1024ULL * 1024ULL)

constexpr uint64_t CONTROL_PLANE_PROTOCOL_NEGOTIATION = 0xff52504341445054;
constexpr uint32_t NEGOTIATE_SOCKET_ID_MAX_NUM = 256;
constexpr uint32_t UMQ_BIND_INFO_SIZE_MAX = 8192;
constexpr uint32_t NEGOTIATE_REQ_BUFFER_SIZE = 64;
constexpr uint32_t DIVIDED_NUMBER = 2;
constexpr uint32_t CACHE_LINE_ALIGNMENT = 64;
constexpr uint16_t TX_HANDLE_THRESHOLD = 2;
constexpr uint16_t TX_RETRIEVE_THRESHOLD = 32;
constexpr uint16_t TX_REPORT_THRESHOLD = 1;
constexpr uint16_t TX_REFILL_THRESHOLD = 32;
constexpr uint32_t TX_POST_BATCH_MAX = 64;
constexpr uint32_t TX_SGE_MAX = 1;
/* unsolicited bytes use the same setting as brpc
 * accumulated bytes exceed UNSOLICITED_BYTES_MAX will generate a solicited interrupt event at remote */
constexpr uint32_t TX_UNSOLICITED_BYTES_MAX = 1048576;
constexpr uint32_t NEGOTIATE_TIMEOUT_MS = 10;
constexpr uint32_t FLUSH_SOCKET_MSG_BUFFER_LEN = 1024;
constexpr uint32_t FLUSH_TIMEOUT_MS = 200;
constexpr uint32_t CONTROL_PLANE_TIMEOUT_MS = 200000;
constexpr uint64_t UMQ_MEM_MIN_EXPAND_SIZE_MB = 64;

constexpr uint64_t SIZE_4K = 4096;
constexpr uint64_t SIZE_8K = 8192;
constexpr uint64_t SIZE_16K = 16384;
constexpr uint64_t SIZE_32K = 32768;
constexpr uint64_t SIZE_64K = 65536;
constexpr uint64_t MASK_DIFF = 1;
constexpr uint64_t IOBUF_DIFF = 32;
constexpr uint16_t REFILL_THRESHOLD = 32;
constexpr int RETRY_NEEDED = 1;
// to improve the efficiency, do one ack event operation per GET_PER_ACK times get event operation(same as brpc)
constexpr uint32_t GET_PER_ACK = 32;
// currently, poll batch use 32 is for the balance of performance and efficiency
// 256 is better on RM_CTP.
// see #66
constexpr uint32_t POLL_BATCH_MAX = 256;

constexpr uint32_t POLL_TX_RETRY_MAX_CNT = 50;

constexpr const uint32_t NET_STR_ERROR_BUF_SIZE = 128;

constexpr uint32_t BLOCK_TYPE_STR_LEN_MAX = 64;
constexpr const char *TINY_QBUF_BLOCK_TYPE = "tiny";       // 4k
constexpr const char *DEFAULT_QBUF_BLOCK_TYPE = "default"; // 8k
constexpr const char *SMALL_QBUF_BLOCK_TYPE = "small";     // 16k
constexpr const char *MEDIUM_QBUF_BLOCK_TYPE = "medium";   // 32k
constexpr const char *LARGE_QBUF_BLOCK_TYPE = "large";     // 64k

constexpr uint32_t BRPC_SYM_STR_LEN_MAX = 128;
constexpr uint32_t BRPC_ALLOC_DEFAULT_BUF_NUM = 1;
constexpr uint32_t DEV_NAME_STR_LEN_MAX = 64;

constexpr char CPU_LIST_PREFIX_PATH[] = "/sys/devices/system/node/";
constexpr char CPU_LIST_SUFFIX_PATH[] = "/cpulist";
constexpr char SOCKET_ID_PERFIX_PATH[] = "/sys/devices/system/cpu/";
constexpr char SOCKET_ID_SUFFIX_PATH[] = "/topology/physical_package_id";
constexpr uint16_t CPU_STR_SIZE = 3;
constexpr uint16_t NODE_STR_SIZE = 4;

static const std::string EMPTY_STR;

constexpr uint32_t UBSOCKET_TRACE_TIME_DEFAULT = 10;
constexpr uint32_t UBSOCKET_TRACE_TIME_MIN = 1;
constexpr uint32_t UBSOCKET_TRACE_TIME_MAX = 300;

constexpr uint32_t UBSOCKET_TRACE_FILE_SIZE_DEFAULT = 10;
constexpr uint32_t UBSOCKET_TRACE_FILE_SIZE_MIN = 1;
constexpr uint32_t UBSOCKET_TRACE_FILE_SIZE_MAX = 300;

constexpr uint32_t UBSOCKET_TRACE_FILE_PATH_LEN_MIN = 1;
constexpr uint32_t UBSOCKET_TRACE_FILE_PATH_LEN_MAX = 512;

constexpr uint32_t UBSOCKET_PROBE_TIME_MS_MIN = 1;
constexpr uint32_t UBSOCKET_PROBE_TIME_MS_MAX = 360000;

constexpr uint32_t UBSOCKET_PROBE_BATCH_MIN = 1;
constexpr uint32_t UBSOCKET_PROBE_BATCH_MAX = 500;

constexpr int8_t UBSOCKET_LINK_PRIORITY_NOT_SET = -1;
constexpr int8_t UBSOCKET_LINK_PRIORITY_DEFAULT = 4;

constexpr uint8_t BRPC_TRACE_FIRST_STR_SIZE = 4;
constexpr uint8_t BRPC_TRACE_SECOND_VALUE_SIZE = 4;
constexpr uint8_t BRPC_TRACE_HEADER_SIZE = 12;
constexpr uint32_t BRPC_TRACE_FIRST_MAGIC = 0x50525043U;
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_DEFINES_H
