/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "cli_client.h"
#include <cstring>
#include <vector>

#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_socket_helper.h"
#include "under_api/dl_libc_api.h"

#include "cli_args_parser.h"
#include "cli_terminal_display.h"

using namespace ock::ubs;
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

static std::unordered_map<std::string, CLITypeParam> probeTypeNameToType = {
    {"query", CLITypeParam::PROBE_OP_QUERY},
    {"enable", CLITypeParam::PROBE_OP_ENABLE},
    {"disable", CLITypeParam::PROBE_OP_DISABLE},
    {"dumppath", CLITypeParam::PROBE_OP_SET_DUMP_PATH},
};

static std::unordered_map<std::string, CLITypeParam> delayTypeNameToType = {
    {"query", CLITypeParam::PROF_OP_QUERY},
    {"enable", CLITypeParam::PROF_OP_ENABLE},
    {"disable", CLITypeParam::PROF_OP_DISABLE},
    {"reset", CLITypeParam::PROF_OP_RESET},
    {"interval", CLITypeParam::PROF_OP_INTERVAL},
    {"path", CLITypeParam::PROF_OP_PATH},
    {"mode", CLITypeParam::PROF_OP_MODE},
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

int CLIClient::ProcessProbe(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args)
{
    CLIControlHeader header{};
    header.Reset();
    header.mCmdId = CLICommand::PROBE;

    if (args.type.empty()) {
        header.mType = CLITypeParam::PROBE_OP_QUERY;
    } else if (tranCliTypeParam(args.type, &header.mType, probeTypeNameToType) == CLITypeParam::INVALID) {
        CLI_LOG("Invalid type param for probe (expected: query, enable, disable, dumppath)\n");
        return -1;
    }

    std::string payloadStr;
    if (header.mType == CLITypeParam::PROBE_OP_SET_DUMP_PATH) {
        if (args.valueStr.empty()) {
            CLI_LOG("dumppath requires -v <path> argument\n");
            return -1;
        }
        payloadStr = args.valueStr;
        header.mDataSize = static_cast<uint32_t>(payloadStr.size());
    }

    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }

    if (!payloadStr.empty() && header.mDataSize > 0) {
        if (SocketConnHelper::SendSocketData(sockfd, payloadStr.c_str(), header.mDataSize,
                                              cliclientIoTimeoutMs) != static_cast<ssize_t>(header.mDataSize)) {
            CLI_LOG("Failed to send payload\n");
            return -1;
        }
    }

    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to recv CLIControlHeader\n");
        return -1;
    }

    if (header.mType == CLITypeParam::PROBE_OP_ENABLE || header.mType == CLITypeParam::PROBE_OP_DISABLE ||
        header.mType == CLITypeParam::PROBE_OP_SET_DUMP_PATH) {
        response.SetDataLen(0);
        return (header.mErrorCode == CLIErrorCode::OK) ? 0 : -1;
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

static int SendSimpleCmd(int sockfd, CLIMessage &response, CLICommand cmd, uint32_t maxResponseSize,
                         uint32_t timeoutMs)
{
    CLIControlHeader header{};
    header.mCmdId = cmd;
    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), timeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), timeoutMs) !=
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
    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), payloadLen, timeoutMs) != payloadLen) {
        CLI_LOG("Failed to recv server msg\n");
        return -1;
    }
    response.SetDataLen(payloadLen);
    return 0;
}

int CLIClient::ProcessTxStat(int sockfd, CLIMessage &response)
{
    return SendSimpleCmd(sockfd, response, CLICommand::TX_STAT, maxResponseSize, cliclientIoTimeoutMs);
}

int CLIClient::ProcessRxStat(int sockfd, CLIMessage &response)
{
    return SendSimpleCmd(sockfd, response, CLICommand::RX_STAT, maxResponseSize, cliclientIoTimeoutMs);
}

int CLIClient::ProcessQbufPoolStats(int sockfd, CLIMessage &response)
{
    return SendSimpleCmd(sockfd, response, CLICommand::QBUF_POOL_STATS, maxResponseSize, cliclientIoTimeoutMs);
}

int CLIClient::ProcessSplitTrace(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args)
{
    (void)response;
    CLIControlHeader header{};
    header.Reset();
    header.mCmdId = CLICommand::SPLIT_TRACE;

    if (args.sampleRate > 0) {
        header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE;
        header.mValue = static_cast<double>(args.sampleRate);
    } else if (args.drainInterval > 0) {
        header.mType = CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL;
        header.mValue = static_cast<double>(args.drainInterval);
    } else {
        bool enable = (args.enable == "true" || args.enable == "1" || args.enable == "on");
        header.SetSwitch(CLISwitchPosition::IS_TRACE_ENABLE, enable);
    }

    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send SplitTrace CLIControlHeader\n");
        return -1;
    }

    CLIControlHeader respHeader{};
    if (SocketConnHelper::RecvSocketData(sockfd, &respHeader, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to recv SplitTrace response\n");
        return -1;
    }

    if (header.mType == CLITypeParam::SPLIT_TRACE_OP_SET_SAMPLE_RATE) {
        CLI_LOG("SplitTrace sample-rate set to %u\n", args.sampleRate);
    } else if (header.mType == CLITypeParam::SPLIT_TRACE_OP_SET_DRAIN_INTERVAL) {
        CLI_LOG("SplitTrace drain-interval set to %u ms\n", args.drainInterval);
    } else {
        bool enable = (args.enable == "true" || args.enable == "1" || args.enable == "on");
        CLI_LOG("SplitTrace %s\n", enable ? "enabled" : "disabled");
    }
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
    if (payloadLen < sizeof(CLIDataHeader)) {
        CLI_LOG("Invalid payload size: %d\n", payloadLen);
        return -1;
    }

    uint32_t oneSockSize = sizeof(CLIDataHeader) + sizeof(CLIQbufPoolData);
    if (payloadLen < oneSockSize) {
        if (!response.AllocateIfNeed(sizeof(CLIDataHeader))) {
            CLI_LOG("Failed to alloc response memory\n");
            return -1;
        }
        if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), sizeof(CLIDataHeader),
              cliclientIoTimeoutMs) != static_cast<ssize_t>(sizeof(CLIDataHeader))) {
            CLI_LOG("Failed to recv qbuf pool header\n");
            return -1;
        }
        response.SetDataLen(sizeof(CLIDataHeader));
        return 0;
    }
    if (!response.AllocateIfNeed(oneSockSize)) {
        CLI_LOG("Failed to alloc reponsese memory\n");
        return -1;
    }
    if (SocketConnHelper::RecvSocketData(sockfd, response.Data(), oneSockSize, cliclientIoTimeoutMs) !=
        static_cast<ssize_t>(oneSockSize)) {
        CLI_LOG("Failed to recv first socket qbuf pool data\n");
        return -1;
    }
    response.SetDataLen(oneSockSize);
    uint32_t remainingLen = payloadLen - oneSockSize;
    constexpr size_t discardBufSize = 4096;
    std::vector<uint8_t> discardBuf(discardBufSize);
    while (remainingLen > 0) {
        uint32_t chunkSize = (remainingLen > discardBuf.size()) ? static_cast<uint32_t>(discardBuf.size()) :
                                                                  remainingLen;
        if (SocketConnHelper::RecvSocketData(sockfd, discardBuf.data(), chunkSize, cliclientIoTimeoutMs) !=
            static_cast<ssize_t>(chunkSize)) {
            CLI_LOG("Failed to discard remaining qbuf pool data\n");
            return -1;
        }
        remainingLen -= chunkSize;
    }
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

int CLIClient::ProcessDelayQuery(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args)
{
    CLIControlHeader header{};
    header.Reset();
    header.mCmdId = CLICommand::DELAY;
    if (tranCliTypeParam(args.type, &header.mType, delayTypeNameToType) == CLITypeParam::INVALID) {
        CLI_LOG("Invalid type param for delay\n");
        return -1;
    }

    /* 各操作取参并校验（-e 是无参布尔开关，供 probe enable/disable 使用，
     * 无法携带取值，path 与 mode/interval 统一从 -v 取参） */
    std::string payloadStr;
    if (header.mType == CLITypeParam::PROF_OP_PATH) {
        if (args.valueStr.empty()) {
            CLI_LOG("set_path requires -v <path> argument\n");
            return -1;
        }
        payloadStr = args.valueStr;
    } else if (header.mType == CLITypeParam::PROF_OP_MODE) {
        if (args.valueStr.empty()) {
            CLI_LOG("set_mode requires -v <fast|ext> argument\n");
            return -1;
        }
        payloadStr = args.valueStr;
    } else if (header.mType == CLITypeParam::PROF_OP_INTERVAL) {
        /* -v 现接受字符串参数，interval 需数字及范围校验（解析阶段不再提前失败） */
        try {
            double intervalVal = std::stod(args.valueStr);
            if (intervalVal < 1 || intervalVal > 5) {
                CLI_LOG("Invalid interval value: %s, must be 1~5 minutes\n", args.valueStr.c_str());
                return -1;
            }
            header.mValue = intervalVal;
        } catch (...) {
            CLI_LOG("Invalid interval value: %s, must be a number (1~5)\n", args.valueStr.c_str());
            return -1;
        }
    }
    if (!payloadStr.empty()) {
        header.mDataSize = static_cast<uint32_t>(payloadStr.size());
    }

    if (SocketConnHelper::SendSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
        sizeof(CLIControlHeader)) {
        CLI_LOG("Failed to send CLIControlHeader\n");
        return -1;
    }

    if (!payloadStr.empty() && header.mDataSize > 0) {
        if (SocketConnHelper::SendSocketData(sockfd, payloadStr.c_str(), header.mDataSize,
                                              cliclientIoTimeoutMs) != static_cast<ssize_t>(header.mDataSize)) {
            CLI_LOG("Failed to send payload\n");
            return -1;
        }
    }

    if (header.mType == CLITypeParam::PROF_OP_INTERVAL || header.mType == CLITypeParam::PROF_OP_PATH ||
        header.mType == CLITypeParam::PROF_OP_MODE) {
        if (SocketConnHelper::RecvSocketData(sockfd, &header, sizeof(CLIControlHeader), cliclientIoTimeoutMs) !=
            sizeof(CLIControlHeader)) {
            CLI_LOG("Failed to recv CLIControlHeader\n");
            return -1;
        }
        response.SetDataLen(0);
        return (header.mErrorCode == CLIErrorCode::OK) ? 0 : -1;
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

    struct sockaddr_un addr {
    };
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    strncpy(addr.sun_path + 1, mServerPath.c_str(), sizeof(addr.sun_path) - 1);

    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CLI_LOG("Failed to connect server errno=%d, error=%s\n", errno, ock::ubs::Func::Error2Str(errno));
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

    if (args.command == CLICommand::DELAY) {
        ret = ProcessDelayQuery(sockfd, response, args);
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
        ret = ProcessProbe(sockfd, response, args);
        return ret;
    }

    if (args.command == CLICommand::TX_STAT) {
        ret = ProcessTxStat(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::RX_STAT) {
        ret = ProcessRxStat(sockfd, response);
        return ret;
    }

    if (args.command == CLICommand::SPLIT_TRACE) {
        ret = ProcessSplitTrace(sockfd, response, args);
        return ret;
    }

    if (args.command == CLICommand::QBUF_POOL_STATS) {
        ret = ProcessQbufPoolStats(sockfd, response);
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