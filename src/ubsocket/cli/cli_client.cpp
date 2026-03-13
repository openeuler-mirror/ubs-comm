/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "file_descriptor.h"
#include "cli_args_parser.h"
#include "cli_terminal_display.h"
#include "utracer_info.h"
#include "cli_client.h"

namespace Statistics {

int CLIClient::ProcessStat(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::STAT;
    if (SocketFd::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketFd::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to recv CLIControlHeader\n");
        return -1;
    }
    uint32_t payloadLen = header.mDataSize;
    if (payloadLen == 0 || payloadLen > maxResponseSize) {
        CLI_LOG("Invalid paylaod size: %d\n", payloadLen);
        return -1;
    }

    if (!response.AllocateIfNeed(payloadLen)) {
        CLI_LOG("Failed to alloc reponsese memory\n");
        return -1;
    }

    if (SocketFd::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::ProcessTopo(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::TOPO;
    if (inet_pton(AF_INET6, args.srcEid, &(header.srcEid)) != 1) {
        CLI_LOG("Invalid source eid: %s\n", args.srcEid);
        return -1;
    }
    if (inet_pton(AF_INET6, args.dstEid, &(header.dstEid)) != 1) {
        CLI_LOG("Invalid source eid: %s\n", args.dstEid);
        return -1;
    }
    if (SocketFd::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (!response.AllocateIfNeed(sizeof(umq_route_list_t))) {
        CLI_LOG("Failed to alloc reponsese memory\n");
        return -1;
    }

    if (SocketFd::RecvSocketData(sockfd, response.Data(), sizeof(umq_route_list_t), cliclientIoTimeoutMs) !=
        sizeof(umq_route_list_t)) {
        CLI_LOG("Failed to recv umq route list\n");
        return -1;
    }
    response.SetDataLen(sizeof(umq_route_list_t));

    return 0;
}

static std::unordered_map<std::string, CLITypeParam> delayTypeNameToType = {
    {"query", CLITypeParam::TRACE_OP_QUERY},
    {"enable", CLITypeParam::TRACE_OP_ENABLE_TRACE},
    {"reset", CLITypeParam::TRACE_OP_RESET},
};

CLITypeParam tranCliTypeParam(const std::string &name, CLITypeParam *type,
                              const std::unordered_map<std::string, CLITypeParam> &nameToType)
{
    auto it = nameToType.find(name);
    if (it != nameToType.end()) {
        *type = it->second;
        return it->second;
    }
    return CLITypeParam::INVALID;
}

static std::unordered_map<std::string, CLISwitchPosition> delaySwitchNameToPosition = {
    {"trace", CLISwitchPosition::IS_TRACE_ENABLE},
    {"quantile", CLISwitchPosition::IS_LATENCY_QUANTILE_ENABLE},
    {"log", CLISwitchPosition::IS_TRACE_LOG_ENABLE},
};

bool dealDelayEnableParse(CLIControlHeader& header, const std::string& enable)
{
    std::vector<std::string> result = {};
    UTracerUtils::SplitStr(enable, ",", result);
    for (const auto& name : result) {
        auto it = delaySwitchNameToPosition.find(name);
        if (it == delaySwitchNameToPosition.end()) {
            return false;
        }
        header.SetSwitch(it->second, true);
    }
    return true;
}

int CLIClient::ProcessDelayQuery(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args)
{
    CLIControlHeader header{};
    header.Reset();
    header.mCmdId = CLICommand::DELAY;
    header.mValue = args.value;
    if (tranCliTypeParam(args.type, &header.mType, delayTypeNameToType) == CLITypeParam::INVALID) {
        CLI_LOG("Invalid type param for delay\n");
        return -1;
    }
    if (header.mType == CLITypeParam::TRACE_OP_ENABLE_TRACE && !dealDelayEnableParse(header, args.enable)) {
        CLI_LOG("Invalid enable param for delay\n");
        return -1;
    }
    if (SocketFd::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketFd::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to recv CLIControlHeader\n");
        return -1;
    }
    uint32_t payloadLen = header.mDataSize;
    if (payloadLen == 0 || payloadLen > maxResponseSize) {
        CLI_LOG("Invalid paylaod size: %d\n", payloadLen);
        return -1;
    }

    if (!response.AllocateIfNeed(payloadLen)) {
        CLI_LOG("Failed to alloc reponsese memory\n");
        return -1;
    }

    if (SocketFd::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::Query(CLIArgsParser::ParsedArgs &args, CLIMessage &response)
{
    if (!IsServerAvailable()) {
        CLI_LOG("server is not available\n");
        return -1;
    }

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        CLI_LOG("failed to create socket\n");
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    if (strncpy_s(addr.sun_path + 1, sizeof(addr.sun_path) - 1, mServerPath.c_str(), sizeof(addr.sun_path) - 1) != 0) {
        close(sockfd);
        sockfd = -1;
        CLI_LOG("Failed to copy server path\n");
        return -1;
    }
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sockfd);
        sockfd = -1;
        CLI_LOG("Failed to connect server errno=%d, error=%s\n", errno, strerror(errno));
        return -1;
    }

    if (SetSocketTimeout(sockfd) != 0) {
        close(sockfd);
        sockfd = -1;
        CLI_LOG("SetSocketTimeout failed\n");
        return -1;
    }

    int ret = 0;
    if (args.command == CLICommand::STAT) {
        ret = ProcessStat(sockfd, response);
        if (ret != 0) {
            close(sockfd);
            sockfd = -1;
        }

        return ret;
    }

    if (args.command == CLICommand::TOPO) {
        ret = ProcessTopo(sockfd, response, args);
        if (ret != 0) {
            close(sockfd);
            sockfd = -1;
        }

        return ret;
    }

    if (args.command == CLICommand::DELAY) {
        ret =  ProcessDelayQuery(sockfd, response, args);
        if (ret != 0) {
            close(sockfd);
            sockfd = -1;
        }

        return ret;
    }
    return 0;
}

bool CLIClient::IsServerAvailable()
{
    return access(mServerPath.c_str(), F_OK);
}

int CLIClient::SetSocketTimeout(int sockFd) const
{
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    if (setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        CLI_LOG("set SO_RCVTIMEO fail\n");
        return -1;
    }

    if (setsockopt(sockFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        CLI_LOG("set SO_SNDTIMEO fail\n");
        return -1;
    }

    return 0;
}
}