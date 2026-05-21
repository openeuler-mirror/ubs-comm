/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide ubsocket IO interfaces (readv/writev)
 * Author:
 * Create: 2026-05-09
 * Note:
 * History: 2026-05-09
 */

#include "ubsocket_io.h"
#include <sys/uio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <arpa/inet.h>
#include <cerrno>
#include "rpc_adpt_vlog.h"

static uint32_t CalculateChecksum(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}

uint32_t CalculateIovecChecksum(const struct UbsocketIovec *iov, int iovcnt)
{
    uint32_t totalChecksum = 0;
    
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iovBase != nullptr && iov[i].iovLen > 0) {
            totalChecksum ^= CalculateChecksum(reinterpret_cast<const uint8_t*>(iov[i].iovBase), iov[i].iovLen);
        }
    }
    
    return totalChecksum;
}

ssize_t UbsocketWritev(int fd, const struct UbsocketIovec *iov, int iovcnt,
                       uint64_t msgId, uint8_t msgType)
{
    if (fd < 0 || iov == nullptr || iovcnt <= 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid parameters: fd=%d, iov=%p, iovcnt=%d", fd, iov, iovcnt);
        return -1;
    }
    
    struct UbsocketMsgHeader header;
    header.msgId = msgId;
    header.payloadLen = 0;
    header.checksum = 0;
    header.msgType = msgType;
    
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iovBase != nullptr) {
            header.payloadLen += iov[i].iovLen;
        }
    }
    
    header.checksum = CalculateIovecChecksum(iov, iovcnt);
    
    struct iovec writeIov[MAX_IOVEC_COUNT];
    if (iovcnt + 1 > MAX_IOVEC_COUNT) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Too many iovec: %d", iovcnt);
        return -1;
    }
    
    writeIov[0].iov_base = &header;
    writeIov[0].iov_len = sizeof(header);
    
    for (int i = 0; i < iovcnt; i++) {
        writeIov[i + 1].iov_base = iov[i].iovBase;
        writeIov[i + 1].iov_len = iov[i].iovLen;
    }
    
    ssize_t totalSent = 0;
    ssize_t sent = writev(fd, writeIov, iovcnt + 1);
    if (sent < 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "writev failed: %s", strerror(errno));
        return -1;
    }
    
    totalSent = sent - sizeof(header);
    if (totalSent < 0) {
        totalSent = 0;
    }
    
    return totalSent;
}

static ssize_t RecvAll(int fd, char *buf, size_t len)
{
    size_t received = 0;
    while (received < len) {
        ssize_t n = recv(fd, buf + received, len - received, 0);
        if (n <= 0) {
            return n;
        }
        received += n;
    }
    return received;
}

static ssize_t ReadvAll(int fd, struct iovec *iov, int iovcnt)
{
    size_t totalLen = 0;
    for (int i = 0; i < iovcnt; i++) {
        totalLen += iov[i].iov_len;
    }
    
    size_t totalReceived = 0;
    while (totalReceived < totalLen) {
        ssize_t n = readv(fd, iov, iovcnt);
        if (n <= 0) {
            return n;
        }
        totalReceived += n;
    }
    return totalReceived;
}

ssize_t UbsocketReadv(int fd, struct UbsocketIovec *iov, int iovcnt,
                      uint64_t *msgId, uint8_t *msgType)
{
    if (fd < 0 || (iov == nullptr && iovcnt != 0)) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Invalid parameters: fd=%d, iov=%p, iovcnt=%d", fd, iov, iovcnt);
        return -1;
    }
    
    struct UbsocketMsgHeader header;
    ssize_t headerReceived = RecvAll(fd, reinterpret_cast<char*>(&header), sizeof(header));
    if (headerReceived != sizeof(header)) {
        if (headerReceived < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Receive timeout: fd=%d", fd);
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                          "Failed to receive header: received=%zd, expected=%zu, errno=%d",
                          headerReceived, sizeof(header), errno);
        }
        return -1;
    }
    
    if (msgId != nullptr) {
        *msgId = header.msgId;
    }
    
    if (msgType != nullptr) {
        *msgType = header.msgType;
    }
    
    // 如果不需要读取payload或者没有payload，直接返回
    if (iovcnt == 0 || header.payloadLen == 0) {
        return 0;
    }
    
    struct iovec readIov[MAX_IOVEC_COUNT];
    if (iovcnt > MAX_IOVEC_COUNT) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Too many iovec: %d", iovcnt);
        return -1;
    }
    
    for (int i = 0; i < iovcnt; i++) {
        readIov[i].iov_base = iov[i].iovBase;
        readIov[i].iov_len = iov[i].iovLen;
    }
    
    ssize_t totalReceived = ReadvAll(fd, readIov, iovcnt);
    if (totalReceived < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "readv timeout: fd=%d", fd);
        } else {
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "readv failed: %s", strerror(errno));
        }
        return -1;
    }
    
    uint32_t calculatedChecksum = CalculateIovecChecksum(iov, iovcnt);
    if (calculatedChecksum != header.checksum) {
        RPC_ADPT_VLOG_ERR(ubsocket::UBSocket,
                          "Checksum mismatch: expected=%u, calculated=%u",
                          header.checksum, calculatedChecksum);
        return -1;
    }
    
    return totalReceived;
}
