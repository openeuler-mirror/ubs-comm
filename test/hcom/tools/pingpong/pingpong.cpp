/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
  
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <argp.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "hcom/hcom.h"
#include "hcom/hcom_service.h"
#include "hcom/hcom_service_channel.h"
#include "hcom/hcom_service_context.h"

// Constants
constexpr uint32_t HCOM_HEADER_SIZE = 1024;
constexpr uint16_t OP_CODE_PINGPONG = 100;

// Global variables
static ock::hcom::UBSHcomService *g_service = nullptr;
static ock::hcom::UBSHcomChannelPtr g_channel = nullptr;
static std::string g_serverUrl;
static std::string g_ipMask = "127.0.0.1/24";
static ock::hcom::UBSHcomConnectOptions g_connectOpt;
static std::atomic<bool> g_connected{false};
static std::mutex g_connectMutex;

static bool g_isServer = false;
static uint32_t g_intervalMs = 1000;
static uint32_t g_size = 1024;
static ock::hcom::UBSHcomNetDriverProtocol g_protocol = ock::hcom::UBSHcomNetDriverProtocol::TCP;

static std::string g_ip = "127.0.0.1";
static uint16_t g_port = 9981;
static std::string g_ubcUrl = "";

// Forward declarations
static int NewChannelHandler(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
                             const std::string &payload);
static int RequestReceivedHandler(ock::hcom::UBSHcomServiceContext &ctx);
static int ClientRecvHandler(ock::hcom::UBSHcomServiceContext &ctx);
static int SendHandler(const ock::hcom::UBSHcomServiceContext &ctx);
static int OneSideHandler(const ock::hcom::UBSHcomServiceContext &ctx);
static void ClientChannelBrokenHandler(const ock::hcom::UBSHcomChannelPtr &ch);
static void ServerChannelBrokenHandler(const ock::hcom::UBSHcomChannelPtr &ch);
static void Reconnect();
static bool ParseArgs(int argc, char *argv[]);

static int NewChannelHandler(const std::string &ipPort, const ock::hcom::UBSHcomChannelPtr &ch,
                             const std::string &payload)
{
    static_cast<void>(ch);
    static_cast<void>(payload);
    std::cout << "[Server] New client connection from " << ipPort << std::endl;
    return 0;
}

static int RequestReceivedHandler(ock::hcom::UBSHcomServiceContext &ctx)
{
    if (ctx.OpType() == ock::hcom::UBSHcomServiceContext::Operation::SER_RECEIVED) {
        ock::hcom::UBSHcomRequest req(ctx.MessageData(), ctx.MessageDataLen(), ctx.OpCode());
        ock::hcom::UBSHcomReplyContext replyCtx(ctx.RspCtx(), 0);

        ock::hcom::Callback *newCallback =
            ock::hcom::UBSHcomNewCallback([](ock::hcom::UBSHcomServiceContext &context) {}, std::placeholders::_1);
        if (newCallback == nullptr) {
            std::cerr << "Create callback failed" << std::endl;
            return -1;
        }
        int32_t ret = ctx.Channel()->Reply(replyCtx, req, newCallback);
        if (ret != 0) {
            std::cerr << "[Server] Reply failed, error: " << ret << std::endl;
        } else {
            std::cout << "[Server] Received ping and replied with pong (size: " << ctx.MessageDataLen() << " bytes)"
                      << std::endl;
        }
    }
    return 0;
}

static int ClientRecvHandler(ock::hcom::UBSHcomServiceContext &ctx)
{
    static_cast<void>(ctx);
    return 0;
}

static int SendHandler(const ock::hcom::UBSHcomServiceContext &ctx)
{
    static_cast<void>(ctx);
    return 0;
}

static int OneSideHandler(const ock::hcom::UBSHcomServiceContext &ctx)
{
    static_cast<void>(ctx);
    return 0;
}

static void ClientChannelBrokenHandler(const ock::hcom::UBSHcomChannelPtr &ch)
{
    static_cast<void>(ch);
    std::cout << "[Client] Channel broken callback triggered!" << std::endl;
    g_connected.store(false);

    // Launch a detached thread to reconnect
    std::thread([]() { Reconnect(); }).detach();
}

static void ServerChannelBrokenHandler(const ock::hcom::UBSHcomChannelPtr &ch)
{
    static_cast<void>(ch);
    std::cout << "[Server] Client connection broken" << std::endl;
}

static void Reconnect()
{
    std::lock_guard<std::mutex> lock(g_connectMutex);
    if (g_connected.load()) {
        return;
    }

    std::cout << "[Client] Reconnect thread started." << std::endl;
    while (!g_connected.load()) {
        std::cout << "[Client] Attempting to connect to " << g_serverUrl << "..." << std::endl;
        ock::hcom::UBSHcomChannelPtr newChannel;
        int32_t ret = g_service->Connect(g_serverUrl, newChannel, g_connectOpt);
        if (ret == 0) {
            std::cout << "[Client] Connection successful!" << std::endl;
            g_channel = newChannel;
            g_connected.store(true);
            break;
        }
        std::cout << "[Client] Connection failed (error: " << ret << "). Retrying in 1 second..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

struct Arguments {
    bool hasRole = false;
};

static struct argp_option g_options[] = {{"role", 'r', "ROLE", 0, "Role to run: client or server (required)", 0},
                                         {"protocol", 'p', "PROTO", 0, "Protocol: tcp, ubc (default: tcp)", 0},
                                         {"ip", 'i', "IP", 0, "OOB IP address (default: 127.0.0.1)", 0},
                                         {"port", 'o', "PORT", 0, "OOB port (default: 9981)", 0},
                                         {"ubc", 'e', "UBC_URL", 0, "UBC URL (default constructed from ip/port)", 0},
                                         {"interval", 'n', "MS", 0, "Ping interval in milliseconds (default: 1000)", 0},
                                         {"size", 's', "SIZE", 0, "Message payload size in bytes (default: 1024)", 0},
                                         {"mask", 'm', "MASK", 0, "IP mask (default constructed from IP)", 0},
                                         {nullptr, 0, nullptr, 0, nullptr, 0}};

static error_t ParseOpt(int key, char *arg, struct argp_state *state)
{
    struct Arguments *arguments = static_cast<struct Arguments *>(state->input);

    switch (key) {
        case 'r': {
            std::string role = arg;
            if (role == "server") {
                g_isServer = true;
                arguments->hasRole = true;
            } else if (role == "client") {
                g_isServer = false;
                arguments->hasRole = true;
            } else {
                argp_error(state, "Invalid role: %s. Must be 'client' or 'server'.", arg);
            }
            break;
        }
        case 'p': {
            std::string proto = arg;
            if (proto == "tcp") {
                g_protocol = ock::hcom::UBSHcomNetDriverProtocol::TCP;
            } else if (proto == "ubc" || proto == "ub") {
                g_protocol = ock::hcom::UBSHcomNetDriverProtocol::UBC;
            } else {
                argp_error(state, "Invalid protocol: %s. Must be tcp or ubc.", arg);
            }
            break;
        }
        case 'i':
            g_ip = arg;
            break;
        case 'o':
            g_port = static_cast<uint16_t>(strtoul(arg, nullptr, 0));
            break;
        case 'e':
            g_ubcUrl = arg;
            break;
        case 'n':
            g_intervalMs = static_cast<uint32_t>(strtoul(arg, nullptr, 0));
            break;
        case 's':
            g_size = static_cast<uint32_t>(strtoul(arg, nullptr, 0));
            break;
        case 'm':
            g_ipMask = arg;
            break;
        case ARGP_KEY_END:
            if (!arguments->hasRole) {
                argp_usage(state);
            }
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp g_argp = {
    g_options, ParseOpt, nullptr, "HCOM PingPong Tool -- pingpong client and server with automatic reconnection",
    nullptr,   nullptr,  nullptr};

static bool ParseArgs(int argc, char *argv[])
{
    struct Arguments arguments;
    error_t ret = argp_parse(&g_argp, argc, argv, 0, nullptr, &arguments);
    if (ret != 0) {
        return false;
    }

    // Set default IP mask if not provided
    if (g_ipMask == "127.0.0.1/24" && g_ip != "127.0.0.1") {
        size_t lastDot = g_ip.rfind('.');
        if (lastDot != std::string::npos) {
            g_ipMask = g_ip.substr(0, lastDot) + ".0/24";
        }
    }

    // Construct server URL
    if (g_protocol == ock::hcom::UBSHcomNetDriverProtocol::UBC) {
        if (!g_ip.empty() && g_port != 0) {
            g_serverUrl = "tcp://" + g_ip + ":" + std::to_string(g_port);
        } else if (!g_ubcUrl.empty()) {
            g_serverUrl = "ubc://" + g_ubcUrl + ":0";
        } else {
            g_serverUrl = "tcp://" + g_ip + ":" + std::to_string(g_port);
        }
    } else {
        // TCP uses tcp:// URL for control plane (OOB)
        g_serverUrl = "tcp://" + g_ip + ":" + std::to_string(g_port);
    }

    return true;
}

int main(int argc, char *argv[])
{
    if (!ParseArgs(argc, argv)) {
        return -1;
    }

    std::cout << "Starting pingpong tool with options:" << std::endl;
    std::cout << "  Role: " << (g_isServer ? "server" : "client") << std::endl;
    std::cout << "  Protocol: " << ock::hcom::UBSHcomNetDriverProtocolToString(g_protocol) << std::endl;
    std::cout << "  Server URL: " << g_serverUrl << std::endl;
    std::cout << "  Ping Interval: " << g_intervalMs << " ms" << std::endl;
    std::cout << "  Message Size: " << g_size << " bytes" << std::endl;
    std::cout << "  IP Mask: " << g_ipMask << std::endl;

    ock::hcom::UBSHcomServiceOptions options;
    options.maxSendRecvDataSize = g_size + HCOM_HEADER_SIZE;
    options.workerGroupMode = ock::hcom::NET_EVENT_POLLING;

    std::string serviceName = g_isServer ? "pingpong_server" : "pingpong_client";
    g_service = ock::hcom::UBSHcomService::Create(g_protocol, serviceName, options);
    if (g_service == nullptr) {
        std::cerr << "Failed to create UBSHcomService!" << std::endl;
        return -1;
    }

    g_service->SetDeviceIpMask({g_ipMask});

    ock::hcom::UBSHcomTlsOptions tlsOptions;
    tlsOptions.enableTls = false;
    g_service->SetTlsOptions(tlsOptions);

    ock::hcom::UBSHcomHeartBeatOptions hbOptions;
    hbOptions.heartBeatIdleSec = 2;
    g_service->SetHeartBeatOptions(hbOptions);

    if (g_isServer) {
        g_service->RegisterRecvHandler(RequestReceivedHandler);
        g_service->RegisterChannelBrokenHandler(ServerChannelBrokenHandler,
                                                ock::hcom::UBSHcomChannelBrokenPolicy::BROKEN_ALL);
        g_service->RegisterSendHandler(SendHandler);
        g_service->RegisterOneSideHandler(OneSideHandler);

        int32_t ret = g_service->Bind(g_serverUrl, NewChannelHandler);
        if (ret != 0) {
            std::cerr << "Failed to bind to URL: " << g_serverUrl << ", error: " << ret << std::endl;
            ock::hcom::UBSHcomService::Destroy(serviceName);
            return -1;
        }
        ret = g_service->Start();
        if (ret != 0) {
            std::cerr << "Failed to start HCOM service, error: " << ret << std::endl;
            ock::hcom::UBSHcomService::Destroy(serviceName);
            return -1;
        }
        std::cout << "[Server] HCOM service started. Press Ctrl+C to exit." << std::endl;
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } else {
        g_service->RegisterRecvHandler(ClientRecvHandler);
        g_service->RegisterChannelBrokenHandler(ClientChannelBrokenHandler,
                                                ock::hcom::UBSHcomChannelBrokenPolicy::RECONNECT);
        g_service->RegisterSendHandler(SendHandler);
        g_service->RegisterOneSideHandler(OneSideHandler);

        int32_t ret = g_service->Start();
        if (ret != 0) {
            std::cerr << "Failed to start HCOM service, error: " << ret << std::endl;
            ock::hcom::UBSHcomService::Destroy(serviceName);
            return -1;
        }

        // Perform initial connection
        Reconnect();

        char *pingBuf = new char[g_size];
        memset(pingBuf, 'A', g_size);
        char *pongBuf = new char[g_size];

        std::cout << "[Client] Starting ping-pong loop." << std::endl;
        while (true) {
            if (g_connected.load() && g_channel != nullptr) {
                ock::hcom::UBSHcomRequest req(pingBuf, g_size, OP_CODE_PINGPONG);
                ock::hcom::UBSHcomResponse rsp(pongBuf, g_size);

                auto start = std::chrono::high_resolution_clock::now();
                int32_t callRet = g_channel->Call(req, rsp);
                auto end = std::chrono::high_resolution_clock::now();

                if (callRet == 0) {
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                    std::cout << "[Client] PingPong success! Size: " << g_size << " bytes, RTT: " << duration << " us"
                              << std::endl;
                } else {
                    std::cerr << "[Client] Call failed, error: " << callRet << std::endl;
                    // Note: If connection is broken, ClientChannelBrokenHandler will trigger soon.
                }
            } else {
                std::cout << "[Client] Waiting for connection..." << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(g_intervalMs));
        }

        delete[] pingBuf;
        delete[] pongBuf;
    }

    ock::hcom::UBSHcomService::Destroy(serviceName);
    return 0;
}
