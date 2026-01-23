/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */
#ifndef HCOM_MULTICAST_DEF_H
#define HCOM_MULTICAST_DEF_H

#include "hcom.h"

namespace ock {
namespace hcom {
using WorkerMode = UBSHcomNetDriverWorkingMode;

struct MulticastServiceOptions {
    uint32_t maxSendRecvDataSize = 1024;           // 发送数据块最大值
    uint32_t maxSendRecvDataCount = 8192;          // 同时最大发送的数据个数
    uint16_t workerGroupId = 0;                    // worker组的id ,需要从0开始， 并且保持唯一
    uint16_t workerGroupThreadCount = 1;           // worker线程数, 如果设置为0的话，不启动worker
    WorkerMode workerGroupMode = NET_BUSY_POLLING; // worker线程工作模式，默认busy_polling
    int8_t workerThreadPriority = 0;               // 线程优先级[-20,19]，19优先级最低，-20优先级最高，同nice值
    std::pair<uint32_t, uint32_t> workerGroupCpuIdsRange = { UINT32_MAX, UINT32_MAX }; // worker绑定的CPU核，默认不绑定

    uint32_t qpSendQueueSize = 1024;                // qp发送队列大小
    uint32_t qpRecvQueueSize = 1024;                // qp接收队列大小
    uint32_t qpPrePostSize = 256;                   // qp队列预申请大小
    uint32_t qpBatchRePostSize = 10;                // qp批量还wr的大小
    uint16_t completionQueueDepth = 2048;           // cq队列的大小
    uint32_t maxSubscriberNum = 7;                  // 一个发布者最大的订阅者数量
    uint8_t publisherWrkGroupNo = 0;                // subscriber订阅时对应publisher的groupNum
};

struct MulticastHeartBeatOptions {
    uint16_t heartBeatIdleSec = 60;                 // 发送心跳保活消息间隔时间
    uint16_t heartBeatProbeTimes = 7;               // 发送心跳探测失败/没收到回复重试次数，超了认为连接已经断开
    uint16_t heartBeatProbeIntervalSec = 2;         // 发送心跳后再次发送时间
};
}
}

#endif
