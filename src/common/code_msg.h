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
#ifndef HCOM_CODE_MSG_H
#define HCOM_CODE_MSG_H

namespace ock {
namespace hcom {
static const char *NNCodeArray[] = {
    "net error",
    "net invalid ip",
    "net new object generate failed",
    "net invalid parameter",
    "net message is too large for two side",
    "net invalid opcode",
    "net endpoint is not established",
    "net endpoint is not initialized",
    "net semaphore initialize failed for block queue",
    "net timeout",
    "net invalid operation",
    "net malloc failed",
    "net seqNo is not matched",
    "net not initialized",
    "net get buffer failed",
    "net message timeout",
    "net message canceled",
    "net message error",
    "net connection is refused",
    "net connection protocol is mismatched",
    "net invalid lKey",
    "net endpoint is broken",
    "net endpoint is closed",
    "net invalid param",
    "net oob listen socket error",
    "net send error in oob connection",
    "net receive error in oob connection",
    "net oob connection callback is not set",
    "net oob client socket error",
    "net oob ssl initialize error",
    "net oob ssl write error",
    "net oob ssl read error",
    "net create epoll failed in heartbeat manager",
    "net set socket option failed in heartbeat manager",
    "net ip is already existed in heartbeat manager",
    "net failed to add ip into heartbeat manager",
    "net failed to add ip into heartbeat manager as epoll add failed",
    "net failed to remove ip from epoll handle",
    "net ip is not found in heartbeat manager",
    "net encrypt failed",
    "net decrypt failed",
    "net oob secure process error",
    "net not support to exchange fd",
    "net validate header failed",
};

static const char *RRCodeArray[] = {
    "rdma invalid param",
    "rdma memory allocate failed",
    "rdma new object generate failed",
    "rdma open file failed",
    "rdma read file failed",
    "rdma device open failed",
    "rdma device index overflow",
    "rdma device open failed",
    "rdma device get interface address failed",
    "rdma device interface address is mismatched",
    "rdma device get gid failed by address",
    "rdma device invalid ip mask",
    "rdma memory region register failed",
    "rdma completion queue is not initialized",
    "rdma completion queue is polling failed",
    "rdma completion queue is polling timeout",
    "rdma completion queue is polling error result",
    "rdma completion queue is polling unmatched opcode",
    "rdma completion queue get event failed",
    "rdma completion queue notify event failed",
    "rdma completion queue is polled failed",
    "rdma completion queue get event timeout",
    "rdma create queue pair failed",
    "rdma queue pair is not initialized",
    "rdma queue pair state change failed",
    "rdma queue pair post receive failed",
    "rdma queue pair post send failed",
    "rdma queue pair post read failed",
    "rdma queue pair post write failed",
    "rdma queue pair receive configuration error",
    "rdma queue pair work request of post send is full",
    "rdma queue pair one side work request is full",
    "rdma queue pair context is full",
    "rdma queue pair change error",
    "rdma oob listen socket error",
    "rdma send error in oob connection",
    "rdma receive error in oob connection",
    "rdma oob connection callback is not set",
    "rdma oob client socket error",
    "rdma oob ssl initialize error",
    "rdma oob ssl write error",
    "rdma oob ssl read error",
    "rdma endpoint is no initialized",
    "rdma worker is no initialized",
    "rdma worker binds cpu failed",
    "rdma request handler is not set in worker",
    "rdma send request posted handler is not set in worker",
    "rdma one side done handler not set in worker",
    "rdma worker adds queue pair failed",
    "rdma create epoll failed in heartbeat manager",
    "rdma set socket option failed in heartbeat manager",
    "rdma ip is already existed in heartbeat manager",
    "rdma failed to add ip into heartbeat manager",
    "rdma failed to add ip into heartbeat manager as epoll add failed",
    "rdma failed to remove ip from epoll handle",
    "rdma ip is not found in heartbeat manager",
};

static const char *ShCodeArray[] = {
    "shm error",
    "shm invalid parameter",
    "shm memory allocate failed",
    "shm new object generate failed",
    "shm file operation failed",
    "shm not initialized",
    "shm timeout",
    "shm context pool is used up",
    "shm channel broken",
    "shm create epoll failed for channel keeper",
    "shm duplicated channel in channel keeper",
    "shm add channel into channel keeper failed",
    "shm remove channel from channel keeper failed",
    "shm request queue space failed",
    "shm send completion callback is failed",
    "shm fd queue is full",
    "shm peer fd is destroyed",
    "shm op context is failed to remove",
};

static const char *SCodeArray[] = {
    "socket general error",
    "socket invalid parameter",
    "socket memory allocate failed",
    "socket new object generate failed",
    "socket listen failed",
    "socket create failed",
    "socket data size is unmatched",
    "socket epoll operation failed",
    "socket send failed",
    "socket connect failed",
    "socket set option failed",
    "socket get option failed",
    "socket create epoll failed in worker",
    "socket retry",
    "socket eagain in nonblocking mode",
    "socket send queue is full",
    "socket context pool is used up",
    "socket ssl write is failed",
    "socket ssl read is failed",
    "socket reset by peer",
    "socket ssl read failed",
    "socket timeout",
};

static const char *SevCodeArray[] = {
    "service general error",
    "service invalid parameter",
    "service new object generate failed",
    "service create timeout thread is failed",
    "service malloc data memory failed",
    "service channel is not established",
    "service store seq no duplicated",
    "service seq no is not found",
    "service response size is small than data length",
    "service timeout",
    "service failed to start periodic manager",
    "service is not configure enable RNDV, failed to start RNDV",
    "service RNDV operate failed by peer",
    "service store channel id duplicated",
    "service reconnect find ep not broken",
    "service find channel not exist",
    "service reconnect over user set window",
    "service connect failed by some ep broken",
    "service do not support server invoke reconnect",
    "service stop by user",
};

static int32_t NNCodeArrayLength = sizeof(NNCodeArray) / sizeof(NNCodeArray[0]);
static int32_t RRCodeArrayLength = sizeof(RRCodeArray) / sizeof(RRCodeArray[0]);
static int32_t ShCodeArrayLength = sizeof(ShCodeArray) / sizeof(ShCodeArray[0]);
static int32_t SCodeArrayLength = sizeof(SCodeArray) / sizeof(SCodeArray[0]);
static int32_t SevCodeArrayLength = sizeof(SevCodeArray) / sizeof(SevCodeArray[0]);
}
}
#endif // HCOM_CODE_MSG_H
