/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Multi-sender to Single-receiver Test Tool - Main Entry
 * Author:
 * Create: 2026-05-09
 * Note:
 * History: 2026-05-09
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <csignal>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>
#include <exception>
#include "multi_sender_test.h"

volatile int g_running = 1;

// 辅助函数：安全解析无符号整数，无效时返回默认值并输出警告
template<typename T>
static T ParseUnsignedWithDefault(const char *paramName,
                                  const char *str,
                                  T defaultValue,
                                  T minValue = 0,
                                  T maxValue = static_cast<T>(-1))
{
    if (str == nullptr || str[0] == '\0') {
        fprintf(stderr, "Warning: Invalid value '%s' for --%s, using default value: %u\n",
                str ? str : "null", paramName, static_cast<unsigned int>(defaultValue));
        return defaultValue;
    }
    
    // 检查是否全是数字（允许前导空格）
    const char *p = str;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    
    // 空字符串或负号
    if (*p == '\0' || *p == '-') {
        fprintf(stderr, "Warning: Invalid value '%s' for --%s, using default value: %u\n",
                str, paramName, static_cast<unsigned int>(defaultValue));
        return defaultValue;
    }
    
    // 检查是否全是数字
    const char *start = p;
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            fprintf(stderr, "Warning: Invalid value '%s' for --%s, using default value: %u\n",
                    str, paramName, static_cast<unsigned int>(defaultValue));
            return defaultValue;
        }
        p++;
    }
    
    // 使用 stoul 避免溢出
    unsigned long value = 0;
    try {
        value = std::stoul(start);
    } catch (const std::exception &) {
        (void)fprintf(stderr, "Warning: Invalid value '%s' for --%s, using default value: %u\n",
                      str, paramName, static_cast<unsigned int>(defaultValue));
        return defaultValue;
    }
    
    // 范围检查
    if (value < static_cast<unsigned long>(minValue) || value > static_cast<unsigned long>(maxValue)) {
        fprintf(stderr, "Warning: Value '%s' for --%s out of range (%u-%u), using default value: %u\n",
                str, paramName, static_cast<unsigned int>(minValue), static_cast<unsigned int>(maxValue),
                static_cast<unsigned int>(defaultValue));
        return defaultValue;
    }
    
    return static_cast<T>(value);
}

static void SignalHandler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void PrintUsage(const char *progName)
{
    printf("Usage: %s --mode <sender|receiver> [options]\n", progName);
    printf("\nCommon Options:\n");
    printf("  --mode <mode>           Running mode: sender or receiver\n");
    printf("  --port <port>           Port number (default: 8080)\n");
    printf("\nSender Options:\n");
    printf("  --server-addr <addr>    Receiver address (default: 127.0.0.1)\n");
    printf("  --msg-count <count>     Number of messages to send (default: 1000)\n");
    printf("  --msg-size <size>       Message size in bytes (default: 1024)\n");
    printf("  --qps <qps>             Expected QPS (0 = unlimited, default: 0)\n");
    printf("\nReceiver Options:\n");
    printf("  --max-clients <count>   Maximum number of clients (default: 10)\n");
    printf("\nExamples:\n");
    printf("  # Start receiver\n");
    printf("  %s --mode receiver --port 8080\n", progName);
    printf("\n");
    printf("  # Start sender\n");
    printf("  %s --mode sender --server-addr 127.0.0.1 --port 8080 \\\n", progName);
    printf("       --msg-count 10000 --msg-size 1024 --qps 1000\n");
}

static int CreateAndConnectSocket(const char *serverAddrStr, uint16_t port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return -1;
    }
    
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, serverAddrStr, &serverAddr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server address: %s\n", serverAddrStr);
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, reinterpret_cast<struct sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
        fprintf(stderr, "Failed to connect to server\n");
        close(sockfd);
        return -1;
    }
    
    const int socketTimeoutSec = 5;
    struct timeval timeout;
    timeout.tv_sec = socketTimeoutSec;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        fprintf(stderr, "Failed to set socket receive timeout\n");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

static bool SetupSignalHandlers()
{
    if (signal(SIGINT, SignalHandler) == SIG_ERR) {
        fprintf(stderr, "Failed to set SIGINT handler\n");
        return false;
    }
    if (signal(SIGTERM, SignalHandler) == SIG_ERR) {
        fprintf(stderr, "Failed to set SIGTERM handler\n");
        return false;
    }
    return true;
}

static void SendMessages(struct SenderContext *ctx, const std::string &msgBuffer,
                         uint32_t msgCount, uint32_t msgSize)
{
    uint32_t successCount = 0;
    uint32_t failureCount = 0;
    
    for (uint32_t i = 0; i < msgCount && g_running; i++) {
        struct UbsocketIovec iov;
        iov.iovBase = const_cast<char*>(msgBuffer.c_str());
        iov.iovLen = msgSize;
        
        if (SenderSend(ctx, &iov, 1) == 0) {
            successCount++;
        } else {
            failureCount++;
        }
        
        const uint32_t progressReportInterval = 1000;
        if ((i + 1) % progressReportInterval == 0) {
            printf("Progress: %u/%u messages sent\n", i + 1, msgCount);
        }
    }
}

static int RunSender(const char *serverAddrStr, uint16_t port,
                     uint32_t msgCount, uint32_t msgSize, uint32_t expectedQps)
{
    struct SenderContext ctx;
    if (SenderInit(&ctx, expectedQps) != 0) {
        fprintf(stderr, "Failed to initialize sender context\n");
        return -1;
    }
    
    int sockfd = CreateAndConnectSocket(serverAddrStr, port);
    if (sockfd < 0) {
        SenderDestroy(&ctx);
        return -1;
    }
    
    ctx.socketFd = sockfd;
    
    std::string msgBuffer(msgSize, 'A');
    
    printf("Sender started, sending %u messages of %u bytes to %s:%u\n",
           msgCount, msgSize, serverAddrStr, port);
    printf("QPS: %u\n", expectedQps);
    
    if (!SetupSignalHandlers()) {
        close(sockfd);
        SenderDestroy(&ctx);
        return -1;
    }
    
    SendMessages(&ctx, msgBuffer, msgCount, msgSize);
    
    ctx.stats.avgLatencyNs = ctx.stats.totalLatencyNs / ctx.stats.sampleCount;
    
    printf("\n");
    printf("Sender finished:\n");
    PrintLatencyStats(&ctx.stats);
    
    close(sockfd);
    SenderDestroy(&ctx);
    
    return 0;
}

static int RunReceiver(uint16_t port, uint32_t maxClients)
{
    struct ReceiverContext ctx;
    if (ReceiverInit(&ctx, port, maxClients) != 0) {
        fprintf(stderr, "Failed to initialize receiver context\n");
        return -1;
    }
    
    printf("Receiver started on port %u, max clients: %u\n", port, maxClients);
    
    if (signal(SIGINT, SignalHandler) == SIG_ERR) {
        fprintf(stderr, "Failed to set SIGINT handler\n");
        ReceiverDestroy(&ctx);
        return -1;
    }
    if (signal(SIGTERM, SignalHandler) == SIG_ERR) {
        fprintf(stderr, "Failed to set SIGTERM handler\n");
        ReceiverDestroy(&ctx);
        return -1;
    }
    
    ReceiverRun(&ctx);
    
    ReceiverDestroy(&ctx);
    
    return 0;
}

constexpr int MIN_ARGC = 2;

int main(int argc, char *argv[])
{
    if (argc < MIN_ARGC) {
        PrintUsage(argv[0]);
        return 1;
    }
    
    char mode[32] = {0};
    uint16_t port = 8080;
    char serverAddr[64] = "127.0.0.1";
    uint32_t msgCount = 1000;
    uint32_t msgSize = 1024;
    uint32_t expectedQps = DEFAULT_QPS;
    uint32_t maxClients = DEFAULT_MAX_CLIENTS;
    
    static struct option longOptions[] = {
        {"mode",        required_argument, 0, 'm'},
        {"port",        required_argument, 0, 'p'},
        {"server-addr", required_argument, 0, 'a'},
        {"msg-count",   required_argument, 0, 'c'},
        {"msg-size",    required_argument, 0, 's'},
        {"qps",         required_argument, 0, 'q'},
        {"max-clients", required_argument, 0, 'n'},
        {"help",        no_argument,       0, 'h'},
        {nullptr,       0,                 0, 0}
    };
    
    int opt;
    int optionIndex = 0;
    
    while ((opt = getopt_long(argc, argv, "m:p:a:c:s:q:n:h", longOptions, &optionIndex)) != -1) {
        switch (opt) {
            case 'm':
                snprintf(mode, sizeof(mode), "%s", optarg);
                break;
            case 'p':
                // 端口范围 1-65535，默认 8080
                port = ParseUnsignedWithDefault<uint16_t>("port", optarg, 8080, 1, 65535);
                break;
            case 'a':
                snprintf(serverAddr, sizeof(serverAddr), "%s", optarg);
                break;
            case 'c':
                // 消息数量必须 > 0，默认 1000
                msgCount = ParseUnsignedWithDefault<uint32_t>("msg-count", optarg, 1000, 1);
                break;
            case 's':
                // 消息大小范围 1-1048576，默认 1024
                msgSize = ParseUnsignedWithDefault<uint32_t>("msg-size", optarg, 1024, 1, 1048576);
                break;
            case 'q':
                // QPS 无上限，默认 0（不限速）
                expectedQps = ParseUnsignedWithDefault<uint32_t>("qps", optarg, DEFAULT_QPS, 0);
                break;
            case 'n':
                // 最大客户端数范围 1-MAX_CLIENTS，默认 10
                maxClients = ParseUnsignedWithDefault<uint32_t>(
                    "max-clients", optarg, DEFAULT_MAX_CLIENTS, 1, MAX_CLIENTS);
                break;
            case 'h':
                PrintUsage(argv[0]);
                return 0;
            default:
                PrintUsage(argv[0]);
                return 1;
        }
    }
    
    if (strcmp(mode, "sender") == 0) {
        return RunSender(serverAddr, port, msgCount, msgSize, expectedQps);
    } else if (strcmp(mode, "receiver") == 0) {
        return RunReceiver(port, maxClients);
    } else {
        fprintf(stderr, "Error: Invalid mode '%s'. Must be 'sender' or 'receiver'\n", mode);
        PrintUsage(argv[0]);
        return 1;
    }
    
    return 0;
}
