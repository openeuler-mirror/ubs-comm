/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide ubsocket IO interfaces (readv/writev)
 * Author:
 * Create: 2026-05-09
 * Note:
 * History: 2026-05-09
 */

#ifndef UBSOCKET_IO_H
#define UBSOCKET_IO_H

#include <cstdint>
#include <cstddef>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UBSOCKET_MSG_TYPE_REQUEST  1
#define UBSOCKET_MSG_TYPE_RESPONSE 2
#define MAX_IOVEC_COUNT 32

struct UbsocketIovec {
    void*       iovBase;
    size_t      iovLen;
};

struct UbsocketMsgHeader {
    uint64_t    msgId;
    uint32_t    payloadLen;
    uint32_t    checksum;
    uint8_t     msgType;
};

uint32_t CalculateIovecChecksum(const struct UbsocketIovec *iov, int iovcnt);

ssize_t UbsocketWritev(int fd, const struct UbsocketIovec *iov, int iovcnt,
                       uint64_t msgId, uint8_t msgType);

ssize_t UbsocketReadv(int fd, struct UbsocketIovec *iov, int iovcnt,
                      uint64_t *msgId, uint8_t *msgType);

#ifdef __cplusplus
}
#endif

#endif
