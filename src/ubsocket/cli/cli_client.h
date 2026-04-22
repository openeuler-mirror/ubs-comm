/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli client, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#ifndef CLI_CLIENT
#define CLI_CLIENT

#include "cli_args_parser.h"
#include "cli_message.h"

namespace Statistics {
class CLIClient {
public:
    static constexpr size_t maxResponseSize = 1 * 1024 * 1024; // 1M
    static constexpr uint32_t cliclientIoTimeoutMs = 200;

    explicit CLIClient(std::string severPath, int pid)
    {
        mServerPath = severPath + std::to_string(pid);
    }

    ~CLIClient() = default;

    int Query(CLIArgsParser::ParsedArgs &args, CLIMessage &response);

    bool IsServerAvailable();

    int SetSocketTimeout(int sockFd) const;

    int ProcessStat(int sockfd, CLIMessage &response);

    int ProcessFlowControl(int sockfd, CLIMessage &response);

    int ProcessTopo(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args);

    int ProcessDelayQuery(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args);

    int ProcessUmqInfo(int sockfd, CLIMessage &response, CLIArgsParser::ParsedArgs &args);
private:
    std::string mServerPath;
};
}
#endif