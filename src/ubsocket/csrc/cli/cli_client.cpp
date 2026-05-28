/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "cli_client.h"

#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_socket_helper.h"
#include "net_common.h"
#include "under_api/dl_libc_api.h"

#include "cli_args_parser.h"
#include "cli_terminal_display.h"
#include "net_common.h"
#include "scope_exit.h"

using ock::ubs::LibcApi;
using ock::ubs::SocketConnHelper;

namespace Statistics {

int CLIClient::ProcessStat(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::STAT;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to recv CLIControlHeader\n");
        return -1;
    }
    uint32_t payloadLen = header.mDataSize;
    if (payloadLen == 0 || payloadLen > maxResponseSize) {
        CLI_LOG("Invalid payload size: %d\n", payloadLen);
        return -1;
    }

    if (!response.AllocateIfNeed(payloadLen)) {
        CLI_LOG("Failed to alloc response memory\n");
        return -1;
    }

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::ProcessFlowControl(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::FLOW_CONTROL;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to recv CLIControlHeader\n");
        return -1;
    }
    uint32_t payloadLen = header.mDataSize;
    if (payloadLen == 0 || payloadLen > maxResponseSize) {
        CLI_LOG("Invalid payload size: %d\n", payloadLen);
        return -1;
    }

    if (!response.AllocateIfNeed(payloadLen)) {
        CLI_LOG("Failed to alloc response memory\n");
        return -1;
    }

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
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
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (!response.AllocateIfNeed(sizeof(umq_route_list_t))) {
        CLI_LOG("Failed to alloc reponsese memory\n");
        return -1;
    }

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), sizeof(umq_route_list_t), cliclientIoTimeoutMs) !=
        sizeof(umq_route_list_t)) {
        CLI_LOG("Failed to recv umq route list\n");
        return -1;
    }
    response.SetDataLen(sizeof(umq_route_list_t));

    return 0;
}

int CLIClient::ProcessProbeQuery(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::PROBE;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to recv CLIControlHeader\n");
        return -1;
    }
    uint32_t payloadLen = header.mDataSize;
    if (payloadLen == 0 || payloadLen > maxResponseSize) {
        CLI_LOG("Invalid payload size: %d\n", payloadLen);
        return -1;
    }

    if (!response.AllocateIfNeed(payloadLen)) {
        CLI_LOG("Failed to alloc response memory\n");
        return -1;
    }

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::ProcessQbufPool(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::QBUF_POOL;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
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

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::ProcessUmqInfo(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::UMQ_INFO;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
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

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::ProcessIo(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::IO;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
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

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::ProcessUmq(int sockfd, CLIMessage &response)
{
    CLIControlHeader header{};
    header.mCmdId = CLICommand::UMQ;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
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

    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, cliclientIoTimeoutMs) != payloadLen) {
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

    auto guard = MakeScopeExit([sockfd]() { ::close(sockfd); });

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    if (strncpy_s(addr.sun_path + 1, sizeof(addr.sun_path) - 1, mServerPath.c_str(), sizeof(addr.sun_path) - 1) != 0) {
        CLI_LOG("Failed to copy server path\n");
        return -1;
    }
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        CLI_LOG("Failed to connect server errno=%d, error=%s\n", errno,
                NetCommon::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return -1;
    }

    if (SetSocketTimeout(sockfd) != 0) {
        CLI_LOG("SetSocketTimeout failed\n");
        return -1;
    }

    int ret = 0;
    if (args.command == CLICommand::STAT) {
        ret = ProcessStat(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::TOPO) {
        ret = ProcessTopo(sockfd, response, args);
        return ret;
    }

    if (args.command == CLICommand::FLOW_CONTROL) {
        ret = ProcessFlowControl(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::QBUF_POOL) {
        ret = ProcessQbufPool(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::UMQ_INFO) {
        ret = ProcessUmqInfo(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::IO) {
        ret = ProcessIo(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::UMQ) {
        ret = ProcessUmq(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::PROBE) {
        ret = ProcessProbeQuery(sockfd, response);
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

    if (LibcApi::setsockopt(sockFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        CLI_LOG("set SO_RCVTIMEO fail\n");
        return -1;
    }

    if (LibcApi::setsockopt(sockFd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        CLI_LOG("set SO_SNDTIMEO fail\n");
        return -1;
    }

    return 0;
}
} // namespace Statistics
