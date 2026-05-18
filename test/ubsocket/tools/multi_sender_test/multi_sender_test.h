/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Multi-sender to Single-receiver Test Tool
 * Author:
 * Create: 2026-05-09
 * Note:
 * History: 2026-05-09
 */

#ifndef MULTI_SENDER_TEST_H
#define MULTI_SENDER_TEST_H

#include <cstdint>
#include <pthread.h>
#include <ctime>
#include "ubsocket_io.h"

extern volatile int g_running;

constexpr uint32_t MAX_CLIENTS = 64;
constexpr uint32_t DEFAULT_QUEUE_DEPTH = 8;
constexpr uint32_t DEFAULT_QPS = 0;
constexpr uint32_t MAX_LATENCY_SAMPLES = 100000;
constexpr int SOCKET_TIMEOUT_SEC = 2;
constexpr uint32_t PERCENTILE_50 = 50;
constexpr uint32_t PERCENTILE_90 = 90;
constexpr uint32_t PERCENTILE_99 = 99;
constexpr uint32_t PERCENTILE_999 = 999;
constexpr uint32_t US_PER_MS = 1000;
constexpr uint32_t US_PER_NS = 1000000;
constexpr uint64_t NS_PER_SEC = 1000000000ULL;
constexpr double PERCENTAGE_FACTOR = 100.0;
constexpr int LISTEN_BACKLOG = 32;
constexpr uint32_t STATS_REPORT_INTERVAL = 1000;
constexpr uint32_t PERCENT_DENOMINATOR = 100;
constexpr uint32_t PERMILL_DENOMINATOR = 1000;

struct RequestRecord {
    uint64_t msgId;
    uint64_t sendTimeNs;
    uint64_t recvTimeNs;
    uint64_t latencyNs;
};

struct LatencyStats {
    uint64_t minLatencyNs;
    uint64_t maxLatencyNs;
    uint64_t avgLatencyNs;
    uint64_t p50LatencyNs;
    uint64_t p90LatencyNs;
    uint64_t p99LatencyNs;
    uint64_t p999LatencyNs;
    uint64_t totalLatencyNs;
    uint64_t sampleCount;
    uint64_t totalSuccess;
    uint64_t totalFailure;
    uint64_t startTimeNs;
    uint64_t endTimeNs;
    struct RequestRecord *records;
};

struct SenderContext {
    int socketFd;
    uint32_t senderId;
    uint64_t msgCounter;
    uint32_t pendingReplies;
    uint32_t queueDepth;
    uint32_t expectedQps;
    uint64_t lastSendTimeNs;
    uint64_t sendIntervalNs;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct LatencyStats stats;
    uint32_t recordCount;
};

struct ReceiverContext {
    int listenFd;
    int clientFds[MAX_CLIENTS];
    uint32_t clientCount;
    uint64_t totalReceived;
    uint64_t totalReplied;
};

int SenderInit(struct SenderContext *ctx, uint32_t senderId, uint32_t queueDepth, uint32_t expectedQps);

void SenderDestroy(struct SenderContext *ctx);

int SenderSend(struct SenderContext *ctx, const struct UbsocketIovec *iov, int iovcnt);

void SenderRateLimit(struct SenderContext *ctx);

void UpdateLatencyStats(struct LatencyStats *stats, uint64_t latencyNs);

void PrintLatencyStats(const struct LatencyStats *stats);

int ReceiverInit(struct ReceiverContext *ctx, uint16_t port);

void ReceiverDestroy(struct ReceiverContext *ctx);

void ReceiverRun(struct ReceiverContext *ctx);

#endif
