/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli parse armument, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "cli_args_parser.h"

namespace Statistics {

const struct option CLIArgsParser::options[] = {
    {"pid", required_argument, nullptr, 'p'},
    {"help", no_argument, nullptr, 'h'},
    {"watch", no_argument, nullptr, 'w'},
    {"srceid", required_argument, nullptr, 's'},
    {"dsteid", required_argument, nullptr, 'd'},
    {nullptr, 0, nullptr, 0},
};

static bool ParseIpv6Eid(char* eidBuf, size_t bufSize, const char* arg)
{
    if (arg == nullptr) {
        CLI_LOG("Invalid eid: argument is null\n");
        return false;
    }

    if (strncpy_s(eidBuf, bufSize, arg, bufSize - 1) != 0) {
        CLI_LOG("Failed to copy eid\n");
        return false;
    }
    eidBuf[bufSize - 1] = '\0';

    struct in6_addr in6;
    if (inet_pton(AF_INET6, eidBuf, &in6) != 1) {
        CLI_LOG("Invalid eid\n");
        return false;
    }

    return true;
}

bool CLIArgsParser::Parse(int argc, char *argv[], ParsedArgs &args)
{
    optind = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "hwp:s:d:", options, nullptr)) != -1) {
        switch (opt) {
            case 'p': {
                int pid = static_cast<int>(strtol(optarg, nullptr, 10));
                if (pid < 0 || pid > INT32_MAX) {
                    CLI_LOG("Invalid pid %d\n", pid);
                    return false;
                }
                args.pid = pid;
                break;
            }
            case 'w':
                args.watch = true;
                break;
            case 's':
                if (!ParseIpv6Eid(args.srcEid, sizeof(args.srcEid), optarg)) {
                    return false;
                }
                break;
            case 'd':
                if (!ParseIpv6Eid(args.dstEid, sizeof(args.dstEid), optarg)) {
                    return false;
                }
                break;
            case 'h':
            default:
                PrintUsage(argv[0]);
                return false;
            }
    }

    if (optind >= argc) {
        CLI_LOG("Missing command (e.g., 'topo', 'stat')\n");
        PrintUsage(argv[0]);
        return false;
    }

    std::string cmd = argv[optind];
    if (!IsCommandValid(cmd)) {
        return false;
    }
    args.command = GetCmd(cmd);
    return true;
}

void CLIArgsParser::PrintUsage(const char* progName)
{
    printf("=============================================\n");
    printf("Usage: %s <command> [options]\n", progName);
    printf("=============================================\n");
    printf("Commands:\n");
    printf("  stat    Query detailed information of each socket in the specified process\n");
    printf("  topo    Query the network topology relationship of a pair of EIDs in the specified process\n");
    printf("\n");
    printf("Global Options (applicable to all commands):\n");
    printf("  -p, --pid <pid>        Required, specify the process ID to query (Range: 0~%d)\n", INT32_MAX);
    printf("  -h, --help             Show this help message and exit\n");
    printf("\n");
    printf("Command-specific Options:\n");
    printf("  [stat command only]:\n");
    printf("    -w, --watch          Optional, enable real-time monitoring (refresh socket info every second)\n");
    printf("    Note: The stat command does NOT require -s/-d parameters\n");
    printf("\n");
    printf("  [topo command only]:\n");
    printf("    -s, --srceid <eid>   Required, specify the source EID (must be a valid IPv6 address)\n");
    printf("    -d, --dsteid <eid>   Required, specify the destination EID (must be a valid IPv6 address)\n");
    printf("    Note: The topo command does NOT support the -w parameter\n");
    printf("\n");
    printf("Examples:\n");
    printf("  1. Query socket info of process 1234:\n");
    printf("     %s stat -p 1234\n", progName);
    printf("  2. Real-time monitor socket info of process 1234 (refresh every second):\n");
    printf("     %s stat -p 1234 -w\n", progName);
    printf("  3. Query topology relationship of EID pair in process 1234:\n");
    printf("     %s topo -p 1234 -s 2001:db8::1 -d 2001:db8::2\n", progName);
    printf("=============================================\n");
}

bool CLIArgsParser::IsCommandValid(std::string &cmd)
{
    static const std::vector<std::string> cmdSet = {"stat", "topo"};
    auto it = std::find(cmdSet.begin(), cmdSet.end(), cmd);
    if (it == cmdSet.end()) {
        CLI_LOG("Invalid command (e.g., 'topo', 'stat')\n");
        return false;
    }
    return true;
}

CLICommand CLIArgsParser::GetCmd(std::string &cmd)
{
    if (cmd == "topo") {
        return CLICommand::TOPO;
    } else if (cmd == "stat") {
        return CLICommand::STAT;
    }
    return CLICommand::INVALID;
}
}