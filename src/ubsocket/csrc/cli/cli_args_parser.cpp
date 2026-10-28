/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli parse armument, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "cli_args_parser.h"

#include <cstdint>

namespace Statistics {

const struct option CLIArgsParser::options[] = {
    {"pid", required_argument, nullptr, 'p'},
    {"help", no_argument, nullptr, 'h'},
    {"watch", no_argument, nullptr, 'w'},
    {"type", required_argument, nullptr, 't'},
    {"enable", required_argument, nullptr, 'e'},
    {"value", required_argument, nullptr, 'v'},
    {nullptr, 0, nullptr, 0},
};

bool CLIArgsParser::Parse(int argc, char *argv[], ParsedArgs &args)
{
    optind = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "hwp:t:e:v:", options, nullptr)) != -1) {
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
            case 't':
                args.type = optarg;
                break;
            case 'v':
                try {
                    args.value = std::stod(optarg);
                } catch (...) {
                    return false;
                }
                break;
            case 'e':
                args.enable = optarg;
                break;
            case 'h':
            default:
                PrintUsage(argv[0]);
                return false;
        }
    }
    if (optind >= argc) {
        CLI_LOG("Missing command (e.g., 'stat', 'delay')\n");
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

void CLIArgsParser::PrintUsage(const char *progName)
{
    printf("=============================================\n");
    printf("Usage: %s <command> [options]\n", progName);
    printf("=============================================\n");
    printf("Commands:\n");
    printf("  stat      Query detailed information of each socket in the specified process\n");
    printf("  delay     Show or operate trace point delay in the specified process\n");
    printf("\n");
    printf("Global Options (applicable to all commands):\n");
    printf("  -p, --pid <pid>        Required, specify the process ID to query (Range: 0~%d)\n", INT32_MAX);
    printf("  -h, --help             Show this help message and exit\n");
    printf("\n");
    printf("Command-specific Options:\n");
    printf("  [stat command only]:\n");
    printf("    -w, --watch          Optional, enable real-time monitoring (refresh info every second)\n");
    printf("\n");
    printf("  [delay command only]:\n");
    printf("    -t, --type <op_type>   Required, specify operation you want (must in query, enable, reset)\n");
    printf("    -v, --value <quantile>   Optional, when type is 'query' could be used\n");
    printf("    -e, --enable <enable_switches>   Required when type is 'enable', specify trace switches you want"
           " (could be a combination of (trace,quantile,log), which not use will be closed)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  1. Query socket info of process 1234:\n");
    printf("     %s stat -p 1234\n", progName);
    printf("  2. Real-time monitor socket info of process 1234 (refresh every second):\n");
    printf("     %s stat -p 1234 -w\n", progName);
    printf("  3. Show the delay time of trace point delay in the specified process:\n");
    printf("     %s delay -p 1234 -t query\n", progName);
    printf("  4. Change the switch status of trace:\n");
    printf("     %s delay -p 1234 -t enable\n", progName);
    printf("=============================================\n");
}

bool CLIArgsParser::IsCommandValid(std::string &cmd)
{
    static const std::vector<std::string> cmdSet = {"stat", "delay"};
    auto it = std::find(cmdSet.begin(), cmdSet.end(), cmd);
    if (it == cmdSet.end()) {
        CLI_LOG("Invalid command (e.g., 'stat', 'delay')\n");
        return false;
    }
    return true;
}

CLICommand CLIArgsParser::GetCmd(std::string &cmd)
{
    if (cmd == "stat") {
        return CLICommand::STAT;
    } else if (cmd == "delay") {
        return CLICommand::DELAY;
    }
    return CLICommand::INVALID;
}
} // namespace Statistics
