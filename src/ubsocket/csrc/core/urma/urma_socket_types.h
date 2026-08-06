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
#ifndef UBS_COMM_URMA_SOCKET_TYPES_H
#define UBS_COMM_URMA_SOCKET_TYPES_H

#include "common/ubsocket_common_includes.h"
#include "under_api/urma/urma_types.h"
#include "urma_setting.h"

namespace ock {
namespace ubs {
namespace urma {

constexpr uint32_t URMA_CONTROL_PLANE_MAGIC = 0x55424C49;                /* "UBLI" */
constexpr uint32_t URMA_SND_BUFF_ALLOC_UNIT = 128;                       /* 128B: inline WRITE_IN_BAND allocate unit */
constexpr uint32_t URMA_INLINE_THRESHOLD = 2 * URMA_SND_BUFF_ALLOC_UNIT; /* 256B: with head size */
constexpr uint32_t URMA_SEND_BUF_DEFAULT_KB = 128;
constexpr uint32_t URMA_RECV_BUF_DEFAULT_KB = 128;
constexpr uint32_t URMA_SEND_BUF_OFFSET_GRANULARITY = 8192;     /* 8K */
constexpr uint32_t URMA_SEND_LEN_GRANULARITY = 1024;            /* 1K */
constexpr size_t URMA_SHARED_BUF_DEFAULT_PAGE_SIZE = 16 * 1024; /* 16KB */
constexpr uint32_t kDefaultQueueDepth = 1024;
constexpr uint32_t kDefaultCqPollBatch = 32;

/*
 * OneSideSenderType encodes the single-sided operation type stored in the URMA completion record's
 * user_ctx field. The receiver uses it to dispatch the correct CQE handler upon operation completion.
 */
enum OneSideSenderType : uint8_t {
    CTRL_DATA_REQUEST = 1,  /* write-in-band: inline-data RDMA WRITE_IMM CQE */
    CTRL_DATA_RESPONSE,     /* post-write: response to a PRE_WRITE or inline-data ack */
    READ_DATA_REQUEST,      /* pre-write large IO: RDMA READ completion, data now in local blocks */
};

/*
 * SenderCRContext packs operation type, sequence number and socket fd into a 64-bit value
 * stored in the URMA completion record's user_ctx field. When a CQE is polled, the receiver
 * decodes user_ctx via this union to locate the corresponding connection and dispatch the handler.
 *
 * Bit layout:
 *   type    [ 0: 3]  4 bits  — OneSideSenderType
 *   seq_no  [ 4:33] 30 bits  — message sequence number
 *   fd      [34:53] 20 bits  — socket file descriptor
 *   reserved[54:63] 10 bits  — reserved
 */
union SenderCRContext {
    struct {
        uint64_t type : 4;     /* operation type (OneSideSenderType) */
        uint64_t seq_no : 30;  /* message sequence number */
        uint64_t fd : 20;      /* socket file descriptor */
        uint64_t reserved : 10;/* reserved */
    };
    uint64_t value;

    SenderCRContext(uint64_t val = 0) noexcept : value{val} {}
    SenderCRContext(OneSideSenderType t, uint64_t seq, int fd_in) noexcept
        : type(t),
          seq_no{seq},
          fd{static_cast<uint64_t>(fd_in)},
          reserved{0}
    {
    }
};

union WriteImmData {
    uint64_t data = 0;
    struct {
        uint64_t user_data : 20; /* bits 0-19 User-defined custom data */
        uint64_t reserved : 2;   /* bits 20-21 Reserved for URMA */
        uint64_t vjetty_id : 16; /* bits 22-37 Virtual jetty identifier (0-65535) */
        uint64_t cr_opcode : 2;  /* bits 38-39 Operation code tag (0-3) */
        uint64_t msn : 24;       /* bits 40-63 Message sequence number (0-16M) */
    } urma;
    struct {
        uint64_t opcode : 4;         /* bits 0~5 UBSocket ring buffer io op code (0~15) */
        uint64_t buffer_offset : 16; /* offset in ring buffer, 128 granularity (0 ~ 8M) */
        uint64_t urma_reserved : 44; /* bits 20~63 URMA reserved */
    } ubsocket;

    WriteImmData() noexcept {}
    WriteImmData(uint64_t data) noexcept
    {
        this->data = data;
    }
    WriteImmData(uint64_t op, uint64_t buf_off) noexcept
    {
        this->ubsocket.opcode = op;
        this->ubsocket.buffer_offset = buf_off;
    }
};

enum IOOpcode : uint8_t {
    PRE_WRITE = 0,         /* pre-write: trigger receiver to RDMA_READ */
    POST_WRITE = 1,        /* post-write: trigger sender to release send_buf and un-reference blocks */
    WRITE_IN_BAND = 2,     /* small IO inline write */
    WRITE_IN_BAND_ACK = 3, /* small IO write acknowledgment */
};

struct UrmaMessageHead {
    uint64_t request_id;
    uint64_t sequence_no;
    uint32_t flags = 0;
    uint32_t data_offset;
    uint32_t data_count; // small io: bytes count, large io: buffer count
    uint16_t message_size;
    uint16_t reserved = 0;
    uintptr_t user_ctx1 = 0;
    uintptr_t user_ctx2 = 0;
};
static_assert(sizeof(UrmaMessageHead) % 16 == 0, "UrmaMessageHead size must be 16B-aligned");

struct PageBufferInMessage {
    uint64_t addr;
    uint32_t size;
    uint32_t flags;
};

struct UrmaMessageDataBody {
    PageBufferInMessage sender_buffers[0];
};

struct UrmaMessageInlineBody {
    uint8_t data[0];
};

struct UrmaCtrlMessage {
    UrmaMessageHead head;
    union {
        UrmaMessageDataBody large_io;
        UrmaMessageInlineBody small_io;
    };
};
constexpr uint32_t URMA_INLINE_MESSAGE_CTRL_COST = sizeof(UrmaMessageHead);
constexpr uint32_t URMA_INLINE_DATA_THRESHOLD = URMA_INLINE_THRESHOLD - URMA_INLINE_MESSAGE_CTRL_COST;

enum TransportType : int8_t {
    TPT_CTP = 0,
    TPT_RTP,
    /* add here */
    TPT_COUNT
};

enum TransportMode : int8_t {
    TPM_RC = 0,
    TPM_RM,
    /* add here */
    TPM_COUNT
};

struct UrmaNegotiateReq {
    uint32_t peer_version;
    TransportType tp_type;
    TransportMode tp_mode;
    uint32_t send_buf_kb;
    uint32_t recv_buf_kb;
    urma_eid_t local_eid;
};

struct UrmaNegotiateRsp {
    int32_t ret_code;
    TransportType tp_type;
    TransportMode tp_mode;
    uint32_t send_buf_kb;
    uint32_t recv_buf_kb;
    urma_eid_t local_eid;
};

struct UrmaSockBufInfo {
    uintptr_t buf = 0; /* address of buf */
    uint32_t size = 0; /* size in bytes */
};

/**
 * Exchange info for connecting
 */
struct UrmaSockExInfo {
    TransportMode mode = TPM_RC;   /* rc or rm */
    TransportType type = TPT_CTP;  /* ctp, rtp, utp */
    uint32_t my_jetty_id = 0;      /* jetty id */
    UrmaSockBufInfo my_send_buf;   /* my address and size info for send */
    UrmaSockBufInfo my_recv_buf;   /* my address and size info for receive */
    bool is_async_connect = false; /* aysnc connect or not */
    urma_jetty_id_t raw_jetty_id{};
    uint32_t token = 0;
    // eid related
};

enum UrmaDataSizeType : uint8_t {
    DST_SMALL_DATA = 0,
    DST_LARGE_DATA,

    /* add here */
    DST_COUNT
};

enum UrmaDataState : uint8_t {
    DS_INIT = 0,            /* init state */
    DS_DURING_READ_WRITE,   /* issued read/write */
    DS_READ_WRITE_FINISHED, /* read/write finished successfully */
    DS_READ_WRITE_FAILED,   /* read/write failed */
    DS_READY,               /* ready for user to read */
    DS_READ,                /* read by user */
    DS_DELETED,             /* between */

    /* add here */
    DS_COUNT,
};

enum UrmaDataRBColour : uint8_t {
    RB_RED = 0,
    RB_BLACK,
};

/* make sure the size is 64bytes */
struct UrmaDataMeta {
    UrmaDataMeta *next = nullptr;                /* link to meta for batch read/write */
    void *io_buf = nullptr;                      /* pointer to really io buf */
    UrmaDataSizeType data_type = DST_SMALL_DATA; /* data size type */
    UrmaDataState state = DS_INIT;               /* state to store the type  */
    uint8_t reserved[5]{};                       /* reserved */
    UrmaDataRBColour rb_parent_colour = RB_RED;  /* parent node color for rb tree */
    UrmaDataMeta *rb_left_child = nullptr;       /* left child for rb tree */
    UrmaDataMeta *rb_right_child = nullptr;      /* right child for rb tree */
    uint64_t reserved2[3]{};                     /* reserved */
};

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_SOCKET_TYPES_H
