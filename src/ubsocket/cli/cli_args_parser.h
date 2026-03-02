/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 *Description: Provide the utility for cli parse armument, etc
 *Author:
 *Create: 2026-02-09
 *Note:
 *History: 2026-02-09
*/

#ifndef CLI_ARGS_PARSER
#define CLI_ARGS_PARSER

#include <arpa/inet.h>
#include <algorithm>
#include <getopt.h>

#include "cli_message.h"
#include "securec.h"

namespace Statistics {

class CLIArgsParser {
public:
    struct ParsedArgs {
        int pid = -1;
        CLICommand command;
        bool watch = false;
        char srcEid[INET6_ADDRSTRLEN];
        char dstEid[INET6_ADDRSTRLEN];
    };

    static bool Parse(int argc, char *argv[], ParsedArgs &args);
    static void PrintUsage(const char* name);
    static bool IsCommandValid(std::string &cmd);
    static CLICommand GetCmd(std::string &cmd);

    static const struct option options[];
};
}
#endif