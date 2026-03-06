/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the main for cli client, etc
 * Author:
 * Create: 2026-02-09
 * Note:
 * History: 2026-02-09
*/

#include "file_descriptor.h"
#include "cli_args_parser.h"
#include "cli_terminal_display.h"
#include "cli_client.h"

int main(int argc, char *argv[])
{
    Statistics::CLIArgsParser::ParsedArgs args;
    if (!Statistics::CLIArgsParser::Parse(argc, argv, args)) {
        return -1;
    }

    Statistics::CLIClient client("ubscli-", args.pid);
    Statistics::TerminalDisplay player{};
    Statistics::CLIMessage response{};
    if (args.command == Statistics::CLICommand::STAT) {
        client.Query(args, response);
        player.DisplaySocketInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplaySocketInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::TOPO) {
        client.Query(args, response);
        player.DisplayTopoInfo(reinterpret_cast<umq_route_list_t *>(response.Data()), response.DataLen());
    } else if (args.command == Statistics::CLICommand::DELAY) {
        if (client.Query(args, response) != 0) {
            return 0;
        }
        player.DisplayDelayTraceInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
    } else {
        CLI_LOG("Invalid command\n");
    }

    return 0;
}