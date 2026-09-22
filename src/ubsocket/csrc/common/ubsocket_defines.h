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

enum dev_schedule_policy {
    ROUND_ROBIN = 1,
    CPU_AFFINITY = 2,
    CPU_AFFINITY_PRIORITY = 3,
};

enum ub_trans_mode {
    RC_TP,
    RM_TP,
    RM_CTP,
    RC_CTP
};

// 描述在 Connect/Accept 间的握手状态
enum class UBHandshakeState : uint32_t {
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

enum ops_error_code {
    OK,
    NORMAL_ERROR,
    FATAL_ERROR
};

enum class UBHandshakeMode : uint32_t {
    TFO,
    UB_SOCK_OPT
};

typedef enum pool_type : uint8_t {
    SINGLE,
    POOL
} pool_type_t;

/// | 选项             | 对应场景                                    | 说明                                                  |
/// |------------------|---------------------------------------------|-------------------------------------------------------|
/// | `BONDING_BACKUP` | 指定 bonding 设备，且 `backup_link=true`    | 通过 bonding 设备通信，bonding 本身提供主备冗余       |
/// | `BONDING_ROUTE`  | 指定 bonding 设备，但是 `backup_link=false` | 通过 bonding 设备获取裸设备路由信息，实际数据走裸设备 |
/// | `RAW_DEVICE`     | 指定裸设备                                  | 完全不依赖 bonding 设备，直接通过裸设备通信           |
enum class LinkSelectionPolicy : uint8_t {
    BONDING_BACKUP = 0,
    BONDING_ROUTE,
    RAW_DEVICE,
};

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
/* 方案B：NegotiateReq 尾部可携带客户端 bind_info（≤ UMQ_BIND_INFO_SIZE_MAX），
 * 发送缓冲随之扩大：magic+version+len(16) + req(≤64) + size(8) + bind_info(8192)。 */
constexpr uint32_t NEGOTIATE_REQ_BUFFER_SIZE = 8320;
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
constexpr uint32_t NEGOTIATE_TIMEOUT_MS = 20000;
constexpr uint32_t SEND_RECV_POLL_SLICE_MS = 10;
constexpr uint32_t FLUSH_SOCKET_MSG_BUFFER_LEN = 1024;
constexpr uint32_t FLUSH_TIMEOUT_MS = 200;
constexpr uint32_t CONTROL_PLANE_TIMEOUT_MS = 20000;
// 析构路径 flush 超时: 覆盖硬件 RNR 重试周期 (rnr_retry=6, err_timeout=2s => 12s) + 3s 余量
constexpr uint32_t UMQ_DESTROY_FLUSH_TIMEOUT_MS = 15000;
// umq_destroy 重试: jetty 仍 BUSY 时 (in-flight WR 未完成) 间隔重试
constexpr int UMQ_DESTROY_MAX_RETRIES = 3;
constexpr int UMQ_DESTROY_RETRY_INTERVAL_US = 500000;
constexpr uint64_t UMQ_MEM_MIN_EXPAND_SIZE_MB = 64;
constexpr uint32_t UBS_RX_PORT_NUM = 4;

/* 环境变量 UBSOCKET_TX_DEPTH / UBSOCKET_RX_DEPTH 校验上限。
 * 65536 (2^16) 对齐硬件 JFS/JFR 深度典型上限；UMQ 层会按设备实际能力
 * (max_jfs_depth / max_jfr_depth / max_jfc_depth) 与流控开关做二次校验，
 * validator 仅在 ubsocket 入口拦截明显非法值。 */
constexpr uint32_t UBSOCKET_TX_DEPTH_MAX = 65536;
constexpr uint32_t UBSOCKET_RX_DEPTH_MAX = 65536;

/* 环境变量 UBSOCKET_ASYNC_ACCEPT_THREAD_NUM 校验上限：async accept 线程池
 * 工作线程数，128 已远超实际需要，防止误配过大导致线程爆炸。 */
constexpr uint32_t UBSOCKET_THREAD_POOL_SIZE_MAX = 128;

constexpr uint64_t SIZE_4K = 4096;
constexpr uint64_t SIZE_8K = 8192;
constexpr uint64_t SIZE_16K = 16384;
constexpr uint64_t SIZE_32K = 32768;
constexpr uint64_t SIZE_64K = 65536;
constexpr uint64_t SIZE_128K = 131072;
constexpr uint64_t SIZE_256K = 262144;
constexpr uint64_t SIZE_512K = 524288;
constexpr uint64_t SIZE_1M = 1048576;
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

constexpr const uint32_t NET_STR_ERROR_BUF_SIZE = 128;

constexpr uint32_t BLOCK_TYPE_STR_LEN_MAX = 64;
constexpr const char *DEFAULT_QBUF_BLOCK_TYPE = "default"; // CTP:4k TP:8k
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

/* CPU ID 校验上限：覆盖所有现网架构（arm64 ≤4096、x86_64 ≤8192），
 * 65535 用于拦截明显非法值（负数、垃圾值），真实上限由 sysfs 文件是否存在决定 */
constexpr int UBSOCKET_CPU_ID_MAX = 65535;

static const std::string EMPTY_STR;

constexpr uint32_t UBSOCKET_TRACE_TIME_DEFAULT = 10;
constexpr uint32_t UBSOCKET_TRACE_TIME_MIN = 1;
constexpr uint32_t UBSOCKET_TRACE_TIME_MAX = 300;

/* KPI 落盘：UBSOCKET_MONITOR_FILE_SIZE 现为磁盘总占用上限（覆盖式轮转，单位 MB），
   环境变量可配，默认 40MB，范围 [10, 600]。落盘最多 2 个文件（1 活动 + 1 轮转槽位），
   单文件轮转大小 = 上限 / 2，超阈值后循环覆写最旧的槽位文件。 */
constexpr uint32_t UBSOCKET_TRACE_FILE_SIZE_DEFAULT = 40;
constexpr uint32_t UBSOCKET_TRACE_FILE_SIZE_MIN = 10;
constexpr uint32_t UBSOCKET_TRACE_FILE_SIZE_MAX = 600;

constexpr uint32_t UBSOCKET_TRACE_FILE_PATH_LEN_MIN = 1;
constexpr uint32_t UBSOCKET_TRACE_FILE_PATH_LEN_MAX = 512;

constexpr uint32_t UBSOCKET_PROBE_TIME_MS_MIN = 1;
constexpr uint32_t UBSOCKET_PROBE_TIME_MS_MAX = 360000;

constexpr uint32_t UBSOCKET_PROBE_BATCH_MIN = 1;
constexpr uint32_t UBSOCKET_PROBE_BATCH_MAX = 500;

/* TX-STAT: 发送侧流控统计探测（默认开启；采样间隔/落盘文件可配置） */
constexpr bool UBSOCKET_TX_STAT_ENABLE_DEFAULT = true;
constexpr uint32_t UBSOCKET_TX_STAT_INTERVAL_MS_DEFAULT = 1000; /* 1s */
constexpr uint32_t UBSOCKET_TX_STAT_INTERVAL_MS_MIN = 1000;     /* 下限 1s 硬约束 */
constexpr uint32_t UBSOCKET_TX_STAT_INTERVAL_MS_MAX = 3600000;  /* 1h */
constexpr uint32_t UBSOCKET_TX_STAT_MAX_MB_DEFAULT = 64;
constexpr uint32_t UBSOCKET_TX_STAT_MAX_MB_MIN = 1;
constexpr uint32_t UBSOCKET_TX_STAT_MAX_MB_MAX = 4096;
constexpr uint32_t UBSOCKET_TX_STAT_HEARTBEAT_SEC_DEFAULT = 0; /* 0=关闭心跳 */
constexpr uint32_t UBSOCKET_TX_STAT_HEARTBEAT_SEC_MAX = 3600;
constexpr uint32_t UBSOCKET_TX_STAT_FILE_LEN_MAX = 256;
constexpr const char *UBSOCKET_TX_STAT_FILE_DEFAULT = "/tmp/ubsocket/stat/tx_stat_<pid>.log";

constexpr int8_t UBSOCKET_LINK_PRIORITY_NOT_SET = -1;
constexpr int8_t UBSOCKET_LINK_PRIORITY_DEFAULT = 4;

constexpr uint8_t BRPC_TRACE_FIRST_STR_SIZE = 4;
constexpr uint8_t BRPC_TRACE_SECOND_VALUE_SIZE = 4;
constexpr uint8_t BRPC_TRACE_HEADER_SIZE = 12;
constexpr uint32_t BRPC_TRACE_FIRST_MAGIC = 0x50525043U;
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_DEFINES_H
