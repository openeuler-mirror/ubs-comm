/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli parse armument, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#include "cli_args_parser.h"

#include <cerrno>

namespace Statistics {

const struct option CLIArgsParser::options[] = {
    {"pid", required_argument, nullptr, 'p'},
    {"help", no_argument, nullptr, 'h'},
    {"watch", no_argument, nullptr, 'w'},
    {"srceid", required_argument, nullptr, 's'},
    {"dsteid", required_argument, nullptr, 'd'},
    {"type", required_argument, nullptr, 't'},
    {"enable", no_argument, nullptr, 'e'},
    {"disable", no_argument, nullptr, 'x'},
    {"value", required_argument, nullptr, 'v'},
    {"sample-rate", required_argument, nullptr, 'r'},
    {"drain-interval", required_argument, nullptr, 'i'},
    {nullptr, 0, nullptr, 0},
};

static bool ParseIpv6Eid(char *eidBuf, size_t bufSize, const char *arg)
{
    if (arg == nullptr) {
        CLI_LOG("Invalid eid: argument is null\n");
        return false;
    }

    strncpy(eidBuf, arg, bufSize - 1);
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
    while ((opt = getopt_long(argc, argv, "hwp:s:d:t:v:r:i:", options, nullptr)) != -1) {
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
            case 't':
                args.type = optarg;
                break;
            case 'v':
                /* 保存原始字符串（mode 传 fast/ext 等非数字值）；
                 * 数字校验由具体命令使用时执行（interval 在 ProcessDelayQuery 中校验） */
                args.valueStr = optarg;
                try {
                    args.value = std::stod(optarg);
                } catch (...) {
                    args.value = 0;
                }
                break;
            case 'e':
                args.enable = "true";
                break;
            case 'x':
                args.enable = "false";
                break;
            case 'r': {
                char *end = nullptr;
                errno = 0;
                /* 用 strtol 而非 strtoul：strtoul 接受负号并按无符号回绕，
                 * 部分负数（如 -18446744073709551615）会回绕进 1~1000 而漏过校验；
                 * strtol 保持负值，由 rate < 1 拒绝 */
                long rate = strtol(optarg, &end, 10);
                if (optarg[0] == '\0' || end == nullptr || *end != '\0' || errno == ERANGE ||
                    rate < 1 || rate > 1000) {
                    CLI_LOG("Invalid sample-rate %s, must be an integer in 1~1000\n", optarg);
                    return false;
                }
                args.sampleRate = static_cast<uint32_t>(rate);
                break;
            }
            case 'i': {
                char *end = nullptr;
                errno = 0;
                /* strtol + endptr：拒绝非整型输入（如 10abc/5.5）；
                 * strtoul 会按无符号回绕负数，部分负数会漏过范围校验，strtol 保持负值被拒绝 */
                long interval = strtol(optarg, &end, 10);
                if (optarg[0] == '\0' || end == nullptr || *end != '\0' || errno == ERANGE ||
                    interval < 1 || interval > 10000) {
                    CLI_LOG("Invalid drain-interval %s, must be an integer in 1~10000\n", optarg);
                    return false;
                }
                args.drainInterval = static_cast<uint32_t>(interval);
                break;
            }
            case 'h':
            default:
                PrintUsage(argv[0]);
                return false;
        }
    }
    if (optind >= argc) {
        CLI_LOG("Missing command (e.g., 'stat', 'topo', 'fc', 'qbuf', 'umqinfo', 'io', 'umq')\n");
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
    printf("  topo      Query the network topology relationship of a pair of EIDs in the specified process\n");
    printf("  delay     Show or operate trace point delay in the specified process\n");
    printf("  fc        Query Flow Control statistics in the specified process\n");
    printf("  qbuf      Query Qbuf Pool statistics in the specified process\n");
    printf("  umqinfo   Query UMQ configuration information in the specified process\n");
    printf("  io        Query IO packet statistics in the specified process\n");
    printf("  umq       Query UMQ performance statistics in the specified process\n");
    printf("  probe     Query probe packet trace data and communication latency in the specified process\n");
    printf("            Use '-t enable' to dynamically start probing, '-t disable' to stop\n");
    printf("            Use '-t dumppath -v <path>' to set probe dump file directory path\n");
    printf("  txstat    Query per-socket TX error bucket statistics in the specified process\n");
    printf("  rxstat    Query per-socket RX error bucket statistics in the specified process\n");
    printf("  qbufstats Query global qbuf pool (normal + tiny) statistics in the specified process\n");
    printf("            Note: The qbufstats command does NOT support the -w parameter (large data volume)\n");
    printf("\n");
    printf("Global Options (applicable to all commands):\n");
    printf("  -p, --pid <pid>        Required, specify the process ID to query (Range: 0~%d)\n", INT32_MAX);
    printf("  -h, --help             Show this help message and exit\n");
    printf("\n");
    printf("Command-specific Options:\n");
    printf("  [stat, fc, qbuf, umqinfo, io, umq, probe commands]:\n");
    printf("    -w, --watch          Optional, enable real-time monitoring (refresh info every second)\n");
    printf("    Note: These commands do NOT require -s/-d parameters\n");
    printf("\n");
    printf("  [topo command only]:\n");
    printf("    -s, --srceid <eid>   Required, specify the source EID (must be a valid IPv6 address)\n");
    printf("    -d, --dsteid <eid>   Required, specify the destination EID (must be a valid IPv6 address)\n");
    printf("    Note: The topo command does NOT support the -w parameter\n");
    printf("\n");
    printf("  [delay command only]:\n");
    printf("    -t, --type <op_type>   Required, specify operation you want"
           " (must in query, enable, disable, reset, interval, path, mode)\n");
    printf("    -v, --value <interval>  Required when type is 'interval', specify dump interval in minutes (1~5)\n");
    printf("    -v, --value <path>      Required when type is 'path', specify new dump file directory path\n");
    printf("    -v, --value <mode>      Required when type is 'mode', specify prof mode (must be 'fast' or 'ext')\n");
    printf("  [strace command only]:\n");
    printf("    --enable             Enable SplitTrace in the specified process\n");
    printf("    --disable            Disable SplitTrace in the specified process\n");
    printf("    --sample-rate <N>    Set sample rate to 1/N (range: 1~1000)\n");
    printf("    --drain-interval <ms>  Set drain thread interval in ms (range: 1~10000)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  1. Query socket info of process 1234:\n");
    printf("     %s stat -p 1234\n", progName);
    printf("  2. Real-time monitor socket info of process 1234 (refresh every second):\n");
    printf("     %s stat -p 1234 -w\n", progName);
    printf("  3. Query topology relationship of EID pair in process 1234:\n");
    printf("     %s topo -p 1234 -s 2001:db8::1 -d 2001:db8::2\n", progName);
    printf("  4. Show the delay time of trace point delay in the specified process:\n");
    printf("     %s delay -p 1234 -t query\n", progName);
    printf("  5. Enable profiling trace in process 1234:\n");
    printf("     %s delay -p 1234 -t enable\n", progName);
    printf("  5b. Disable profiling trace in process 1234:\n");
    printf("     %s delay -p 1234 -t disable\n", progName);
    printf("  5c. Set dump interval to 3 minutes in process 1234:\n");
    printf("     %s delay -p 1234 -t interval -v 3\n", progName);
    printf(" 5d. Set dump file path in process 1234:\n");
    printf("     %s delay -p 1234 -t path -v /tmp/ubsocket/prof\n", progName);
    printf(" 5e. Switch prof mode to 'fast' (basic stats) in process 1234:\n");
    printf("     %s delay -p 1234 -t mode -v fast\n", progName);
    printf(" 5f. Switch prof mode to 'ext' (percentile stats) in process 1234:\n");
    printf("     %s delay -p 1234 -t mode -v ext\n", progName);
    printf("  6. Query Flow Control statistics of process 1234:\n");
    printf("     %s fc -p 1234\n", progName);
    printf("  7. Real-time monitor Flow Control statistics of process 1234:\n");
    printf("     %s fc -p 1234 -w\n", progName);
    printf("  8. Query Qbuf Pool statistics of process 1234:\n");
    printf("     %s qbuf -p 1234\n", progName);
    printf("  9. Real-time monitor Qbuf Pool statistics of process 1234:\n");
    printf("     %s qbuf -p 1234 -w\n", progName);
    printf(" 10. Query UMQ configuration information of process 1234:\n");
    printf("     %s umqinfo -p 1234\n", progName);
    printf(" 11. Real-time monitor UMQ configuration information of process 1234:\n");
    printf("     %s umqinfo -p 1234 -w\n", progName);
    printf(" 12. Query IO packet statistics of process 1234:\n");
    printf("     %s io -p 1234\n", progName);
    printf(" 13. Real-time monitor IO packet statistics of process 1234:\n");
    printf("     %s io -p 1234 -w\n", progName);
    printf(" 14. Query UMQ performance statistics of process 1234:\n");
    printf("     %s umq -p 1234\n", progName);
    printf(" 15. Real-time monitor UMQ performance statistics of process 1234:\n");
    printf("     %s umq -p 1234 -w\n", progName);
    printf(" 16. Query probe packet trace data and communication latency of process 1234:\n");
    printf("     %s probe -p 1234\n", progName);
    printf(" 17. Query per-socket TX error bucket statistics of process 1234:\n");
    printf("     %s txstat -p 1234\n", progName);
    printf(" 18. Real-time monitor per-socket TX error bucket statistics of process 1234:\n");
    printf("     %s txstat -p 1234 -w\n", progName);
    printf(" 19. Query per-socket RX error bucket statistics of process 1234:\n");
    printf("     %s rxstat -p 1234\n", progName);
    printf(" 20. Real-time monitor per-socket RX error bucket statistics of process 1234:\n");
    printf("     %s rxstat -p 1234 -w\n", progName);
    printf(" 21. Enable SplitTrace for process 1234:\n");
    printf("     %s strace -p 1234 --enable\n", progName);
    printf(" 22. Disable SplitTrace for process 1234:\n");
    printf("     %s strace -p 1234 --disable\n", progName);
    printf(" 23. Set SplitTrace sample rate to 1/1000 for process 1234:\n");
    printf("     %s strace -p 1234 --sample-rate 1000\n", progName);
    printf(" 24. Set SplitTrace drain interval to 50ms for process 1234:\n");
    printf("     %s strace -p 1234 --drain-interval 50\n", progName);
    printf(" 25. Query global qbuf pool statistics of process 1234:\n");
    printf("     %s qbufstats -p 1234\n", progName);
    printf("=============================================\n");
}

bool CLIArgsParser::IsCommandValid(std::string &cmd)
{
    static const std::vector<std::string> cmdSet = {"stat",    "topo",     "delay", "fc",     "qbuf", "umqinfo",
                                                   "io",      "umq",      "probe", "txstat", "rxstat", "strace",
                                                   "qbufstats"};
    auto it = std::find(cmdSet.begin(), cmdSet.end(), cmd);
    if (it == cmdSet.end()) {
        CLI_LOG("Invalid command (e.g., 'stat', 'topo', 'delay', 'fc', 'qbuf', 'umqinfo', 'io', 'umq', 'probe', "
                "'txstat', 'rxstat', 'strace', 'qbufstats')\n");
        return false;
    }
    return true;
}

CLICommand CLIArgsParser::GetCmd(std::string &cmd)
{
    if (cmd == "stat") {
        return CLICommand::STAT;
    } else if (cmd == "topo") {
        return CLICommand::TOPO;
    } else if (cmd == "delay") {
        return CLICommand::DELAY;
    } else if (cmd == "fc") {
        return CLICommand::FLOW_CONTROL;
    } else if (cmd == "qbuf") {
        return CLICommand::QBUF_POOL;
    } else if (cmd == "umqinfo") {
        return CLICommand::UMQ_INFO;
    } else if (cmd == "io") {
        return CLICommand::IO;
    } else if (cmd == "umq") {
        return CLICommand::UMQ;
    } else if (cmd == "probe") {
        return CLICommand::PROBE;
    } else if (cmd == "txstat") {
        return CLICommand::TX_STAT;
    } else if (cmd == "rxstat") {
        return CLICommand::RX_STAT;
    } else if (cmd == "strace") {
        return CLICommand::SPLIT_TRACE;
    } else if (cmd == "qbufstats") {
        return CLICommand::QBUF_POOL_STATS;
    }
    return CLICommand::INVALID;
}
} // namespace Statistics