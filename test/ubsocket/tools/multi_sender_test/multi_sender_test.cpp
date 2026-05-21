/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Multi-sender to Single-receiver Test Tool
 * Author:
 * Create: 2026-05-09
 * Note:
 * History: 2026-05-09
 */

#include "multi_sender_test.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cerrno>
#include <sys/time.h>
#include <algorithm>
#include <string>

static uint64_t GetCurrentTimeNs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

int SenderInit(struct SenderContext *ctx, uint32_t expectedQps)
{
    if (ctx == nullptr) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(struct SenderContext));
    ctx->expectedQps = expectedQps;
    
    if (expectedQps > 0) {
        ctx->sendIntervalNs = 1000000000ULL / expectedQps;
    } else {
        ctx->sendIntervalNs = 0;
    }
    
    memset(&ctx->stats, 0, sizeof(struct LatencyStats));
    ctx->stats.minLatencyNs = UINT64_MAX;
    ctx->stats.startTimeNs = GetCurrentTimeNs();
    
    ctx->stats.records = static_cast<struct RequestRecord *>(
        malloc(MAX_LATENCY_SAMPLES * sizeof(struct RequestRecord)));
    if (ctx->stats.records == nullptr) {
        return -1;
    }
    ctx->recordCount = 0;
    
    return 0;
}

void SenderDestroy(struct SenderContext *ctx)
{
    if (ctx == nullptr) {
        return;
    }
    
    if (ctx->stats.records != nullptr) {
        free(ctx->stats.records);
        ctx->stats.records = nullptr;
    }
}

void SenderRateLimit(struct SenderContext *ctx)
{
    if (ctx->expectedQps == 0) {
        return;
    }
    
    uint64_t now = GetCurrentTimeNs();
    uint64_t elapsed = now - ctx->lastSendTimeNs;
    
    if (elapsed < ctx->sendIntervalNs) {
        uint64_t waitNs = ctx->sendIntervalNs - elapsed;
        struct timespec ts;
        ts.tv_sec = waitNs / 1000000000ULL;
        ts.tv_nsec = waitNs % 1000000000ULL;
        nanosleep(&ts, nullptr);
    }
    
    ctx->lastSendTimeNs = GetCurrentTimeNs();
}

int SenderSend(struct SenderContext *ctx, const struct UbsocketIovec *iov, int iovcnt)
{
    if (ctx == nullptr || iov == nullptr || iovcnt <= 0) {
        return -1;
    }
    
    SenderRateLimit(ctx);
    
    uint64_t sendTime = GetCurrentTimeNs();
    uint64_t msgId = ctx->msgCounter++;
    
    ssize_t sent = UbsocketWritev(ctx->socketFd, iov, iovcnt, msgId, UBSOCKET_MSG_TYPE_REQUEST);
    if (sent < 0) {
        ctx->stats.totalFailure++;
        ctx->stats.endTimeNs = GetCurrentTimeNs();
        return -1;
    }
    
    uint64_t recvMsgId;
    uint8_t recvMsgType;
    ssize_t received = UbsocketReadv(ctx->socketFd, nullptr, 0, &recvMsgId, &recvMsgType);
    if (received < 0) {
        ctx->stats.totalFailure++;
        ctx->stats.endTimeNs = GetCurrentTimeNs();
        return -1;
    }
    
    if (recvMsgType != UBSOCKET_MSG_TYPE_RESPONSE) {
        printf("Invalid response message type: expected=%u, received=%u\n",
               UBSOCKET_MSG_TYPE_RESPONSE, recvMsgType);
        ctx->stats.totalFailure++;
        ctx->stats.endTimeNs = GetCurrentTimeNs();
        return -1;
    }
    
    uint64_t recvTime = GetCurrentTimeNs();
    uint64_t latencyNs = recvTime - sendTime;
    
    UpdateLatencyStats(&ctx->stats, latencyNs);
    ctx->stats.totalSuccess++;
    ctx->stats.endTimeNs = recvTime;
    
    return 0;
}

void UpdateLatencyStats(struct LatencyStats *stats, uint64_t latencyNs)
{
    if (stats == nullptr) {
        return;
    }
    
    if (latencyNs < stats->minLatencyNs) {
        stats->minLatencyNs = latencyNs;
    }
    
    if (latencyNs > stats->maxLatencyNs) {
        stats->maxLatencyNs = latencyNs;
    }
    
    stats->totalLatencyNs += latencyNs;
    stats->sampleCount++;
    
    if (stats->sampleCount <= MAX_LATENCY_SAMPLES && stats->records != nullptr) {
        uint32_t idx = static_cast<uint32_t>(stats->sampleCount - 1);
        stats->records[idx].latencyNs = latencyNs;
    }
}

static void CalculatePercentiles(struct LatencyStats *stats, uint64_t *latencies, uint32_t count)
{
    if (count == 0) {
        return;
    }
    
    std::sort(latencies, latencies + count);
    
    stats->p50LatencyNs = latencies[count * PERCENTILE_50 / PERCENT_DENOMINATOR];
    stats->p90LatencyNs = latencies[count * PERCENTILE_90 / PERCENT_DENOMINATOR];
    stats->p99LatencyNs = latencies[count * PERCENTILE_99 / PERCENT_DENOMINATOR];
    stats->p999LatencyNs = latencies[count * PERCENTILE_999 / PERMILL_DENOMINATOR];
}

void PrintLatencyStats(const struct LatencyStats *stats)
{
    if (stats == nullptr || stats->sampleCount == 0) {
        return;
    }
    
    uint64_t durationNs = stats->endTimeNs - stats->startTimeNs;
    double durationSec = durationNs / static_cast<double>(NS_PER_SEC);
    double actualQps = durationSec > 0 ? stats->sampleCount / durationSec : 0.0;
    
    uint64_t *latencies = static_cast<uint64_t *>(malloc(stats->sampleCount * sizeof(uint64_t)));
    if (latencies != nullptr && stats->records != nullptr) {
        for (uint64_t i = 0; i < stats->sampleCount; i++) {
            latencies[i] = stats->records[i].latencyNs;
        }
        CalculatePercentiles(
            const_cast<struct LatencyStats *>(stats),
            latencies,
            static_cast<uint32_t>(stats->sampleCount));
        free(latencies);
    }
    
    printf("==================================================\n");
    printf("                   Test Results Summary\n");
    printf("==================================================\n");
    uint64_t totalMessages = stats->totalSuccess + stats->totalFailure;
    printf("Total Messages:      %lu\n", totalMessages);
    printf("Success Count:       %lu\n", stats->totalSuccess);
    printf("Failure Count:       %lu\n", stats->totalFailure);
    printf("Success Rate:        %.2f%%\n", totalMessages > 0 ?
           static_cast<double>(stats->totalSuccess) / totalMessages * PERCENTAGE_FACTOR : 0.0);
    printf("Duration:            %.2f seconds\n", durationSec);
    printf("Actual QPS:          %.2f\n", actualQps);
    printf("--------------------------------------------------\n");
    printf("Latency Statistics (microseconds):\n");
    printf("  Min:               %lu us\n", stats->minLatencyNs / US_PER_MS);
    printf("  Max:               %lu us\n", stats->maxLatencyNs / US_PER_MS);
    printf("  Avg:               %lu us\n", stats->avgLatencyNs / US_PER_MS);
    printf("  P50:               %lu us\n", stats->p50LatencyNs / US_PER_MS);
    printf("  P90:               %lu us\n", stats->p90LatencyNs / US_PER_MS);
    printf("  P99:               %lu us\n", stats->p99LatencyNs / US_PER_MS);
    printf("  P99.9:             %lu us\n", stats->p999LatencyNs / US_PER_MS);
    printf("==================================================\n");
}

int ReceiverInit(struct ReceiverContext *ctx, uint16_t port, uint32_t maxClients)
{
    if (ctx == nullptr) {
        return -1;
    }
    
    memset(ctx, 0, sizeof(struct ReceiverContext));
    
    ctx->listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->listenFd < 0) {
        return -1;
    }
    
    int opt = 1;
    setsockopt(ctx->listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(ctx->listenFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(ctx->listenFd);
        return -1;
    }
    
    if (listen(ctx->listenFd, LISTEN_BACKLOG) < 0) {
        close(ctx->listenFd);
        return -1;
    }
    
    ctx->maxClients = maxClients > 0 ? maxClients : DEFAULT_MAX_CLIENTS;
    if (ctx->maxClients > MAX_CLIENTS) {
        ctx->maxClients = MAX_CLIENTS;
    }
    
    return 0;
}

void ReceiverDestroy(struct ReceiverContext *ctx)
{
    if (ctx == nullptr) {
        return;
    }
    
    for (uint32_t i = 0; i < ctx->clientCount; i++) {
        if (ctx->clientFds[i] >= 0) {
            close(ctx->clientFds[i]);
        }
    }
    
    if (ctx->listenFd >= 0) {
        close(ctx->listenFd);
    }
}

void ReceiverRun(struct ReceiverContext *ctx)
{
    if (ctx == nullptr) {
        return;
    }
    
    fd_set readFds;
    struct timeval tv;
    int maxFd = ctx->listenFd;
    
    while (g_running) {
        FD_ZERO(&readFds);
        FD_SET(ctx->listenFd, &readFds);
        
        for (uint32_t i = 0; i < ctx->clientCount; i++) {
            if (ctx->clientFds[i] >= 0) {
                FD_SET(ctx->clientFds[i], &readFds);
                if (ctx->clientFds[i] > maxFd) {
                    maxFd = ctx->clientFds[i];
                }
            }
        }
        
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int ret = select(maxFd + 1, &readFds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        
        if (ret == 0) {
            continue;
        }
        
        if (FD_ISSET(ctx->listenFd, &readFds)) {
            struct sockaddr_in clientAddr;
            socklen_t clientLen = sizeof(clientAddr);
            int clientFd = accept(ctx->listenFd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);
            if (clientFd < 0) {
                continue;
            }
            if (ctx->clientCount >= ctx->maxClients) {
                printf("Max clients reached (%u), rejecting connection from %s:%d\n",
                       ctx->maxClients, inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));
                fflush(stdout);
                close(clientFd);
                continue;
            }
            
            struct timeval timeout;
            timeout.tv_sec = SOCKET_TIMEOUT_SEC;
            timeout.tv_usec = 0;
            setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            
            ctx->clientFds[ctx->clientCount++] = clientFd;
            printf("Client connected: %s:%d, total clients: %u\n",
                   inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), ctx->clientCount);
            fflush(stdout);
        }
        
        uint32_t i = 0;
        while (i < ctx->clientCount) {
            if (ctx->clientFds[i] >= 0 && FD_ISSET(ctx->clientFds[i], &readFds)) {
                struct UbsocketMsgHeader reqHeader;
                ssize_t n = recv(ctx->clientFds[i], &reqHeader, sizeof(reqHeader), 0);
                if (n <= 0) {
                    printf("Client disconnected or timeout: fd=%d, total clients: %u\n",
                           ctx->clientFds[i], ctx->clientCount - 1);
                    fflush(stdout);
                    close(ctx->clientFds[i]);
                    ctx->clientFds[i] = ctx->clientFds[--ctx->clientCount];
                    continue;
                }
                
                size_t headerReceived = static_cast<size_t>(n);
                bool clientDisconnected = false;
                while (headerReceived < sizeof(reqHeader)) {
                    n = recv(ctx->clientFds[i], reinterpret_cast<char*>(&reqHeader) + headerReceived,
                             sizeof(reqHeader) - headerReceived, 0);
                    if (n <= 0) {
                        printf("Client disconnected during header read: fd=%d\n", ctx->clientFds[i]);
                        fflush(stdout);
                        close(ctx->clientFds[i]);
                        ctx->clientFds[i] = ctx->clientFds[--ctx->clientCount];
                        clientDisconnected = true;
                        break;
                    }
                    headerReceived += n;
                }
                if (clientDisconnected) {
                    continue;
                }
                
                std::string payload;
                if (reqHeader.payloadLen > 0) {
                    payload.resize(reqHeader.payloadLen);
                    ssize_t payloadN = recv(ctx->clientFds[i], &payload[0], reqHeader.payloadLen, 0);
                    if (payloadN <= 0) {
                        printf("Client disconnected during payload read: fd=%d\n", ctx->clientFds[i]);
                        fflush(stdout);
                        close(ctx->clientFds[i]);
                        ctx->clientFds[i] = ctx->clientFds[--ctx->clientCount];
                        continue;
                    }
                    
                    ssize_t payloadReceived = payloadN;
                    while (payloadReceived < reqHeader.payloadLen) {
                        payloadN = recv(ctx->clientFds[i], 
                                        &payload[0] + payloadReceived,
                                        reqHeader.payloadLen - payloadReceived, 0);
                        if (payloadN <= 0) {
                            printf("Client disconnected during payload read: fd=%d\n", ctx->clientFds[i]);
                            fflush(stdout);
                            close(ctx->clientFds[i]);
                            ctx->clientFds[i] = ctx->clientFds[--ctx->clientCount];
                            clientDisconnected = true;
                            break;
                        }
                        payloadReceived += payloadN;
                    }
                    if (clientDisconnected) {
                        continue;
                    }
                }
                
                struct UbsocketIovec iov;
                iov.iovBase = const_cast<char*>(payload.data());
                iov.iovLen = reqHeader.payloadLen;
                uint32_t calcChecksum = CalculateIovecChecksum(&iov, 1);
                if (calcChecksum != reqHeader.checksum) {
                    printf("Checksum mismatch from client %d\n", ctx->clientFds[i]);
                    i++;
                    continue;
                }
                
                struct UbsocketMsgHeader respHeader;
                respHeader.msgId = reqHeader.msgId;
                respHeader.payloadLen = 0;
                respHeader.checksum = 0;
                respHeader.msgType = UBSOCKET_MSG_TYPE_RESPONSE;
                
                send(ctx->clientFds[i], &respHeader, sizeof(respHeader), 0);
                ctx->totalReceived++;
                ctx->totalReplied++;
                
                if (ctx->totalReceived % STATS_REPORT_INTERVAL == 0) {
                    printf("Received: %lu, Replied: %lu\n", ctx->totalReceived, ctx->totalReplied);
                    fflush(stdout);
                }
            }
            i++;
        }
    }
}