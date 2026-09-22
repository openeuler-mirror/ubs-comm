/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the main for cli client, etc
 * Author:
 * Create: 2026-02-09
 * Note:
 * History: 2026-02-09
*/

#include "cli_args_parser.h"
#include "cli_client.h"
#include "cli_terminal_display.h"
#include "under_api/dl_libc_api.h"

int main(int argc, char *argv[])
{
    Statistics::CLIArgsParser::ParsedArgs args;
    if (!Statistics::CLIArgsParser::Parse(argc, argv, args)) {
        return -1;
    }

    Statistics::CLIClient client("ubscli-", args.pid);
    Statistics::TerminalDisplay player{};
    Statistics::CLIMessage response{};
    ock::ubs::LibcApi::Load();
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
    } else if (args.command == Statistics::CLICommand::FLOW_CONTROL) {
        client.Query(args, response);
        player.DisplayFlowControlInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplayFlowControlInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::DELAY) {
        if (args.type == "enable" || args.type == "disable" ||
            args.type == "interval" || args.type == "path") {
            if (client.Query(args, response) == 0) {
                printf("Profiling %s successfully\n", args.type.c_str());
            } else {
                printf("Failed to %s profiling\n", args.type.c_str());
            }
        } else if (args.type == "mode") {
            if (client.Query(args, response) == 0) {
                printf("Profiling mode switched to %s successfully\n", args.valueStr.c_str());
            } else {
                printf("Failed to switch profiling mode to %s\n", args.valueStr.c_str());
            }
        } else {
            if (client.Query(args, response) != 0) {
                return 0;
            }
            player.DisplayDelayTraceInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::QBUF_POOL) {
        client.Query(args, response);
        player.DisplayQbufPoolInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplayQbufPoolInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::UMQ_INFO) {
        client.Query(args, response);
        player.DisplayUmqInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplayUmqInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::IO) {
        client.Query(args, response);
        player.DisplayIoPacketInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplayIoPacketInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::UMQ) {
        client.Query(args, response);
        player.DisplayUmqPerfInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplayUmqPerfInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::PROBE) {
        if (args.type == "enable" || args.type == "disable") {
            if (client.Query(args, response) == 0) {
                printf("Probe %s successfully\n", args.type.c_str());
            } else {
                printf("Failed to %s probe\n", args.type.c_str());
            }
        } else if (args.type == "dumppath") {
            if (client.Query(args, response) == 0) {
                printf("Probe dump path set to %s successfully\n", args.valueStr.c_str());
            } else {
                printf("Failed to set probe dump path\n");
            }
        } else {
            client.Query(args, response);
            player.DisplayProbeInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
            while (args.watch) {
                sleep(1);
                client.Query(args, response);
                player.DisplayProbeInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
            }
        }
    } else if (args.command == Statistics::CLICommand::TX_STAT) {
        client.Query(args, response);
        player.DisplayTxStatInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplayTxStatInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::RX_STAT) {
        client.Query(args, response);
        player.DisplayRxStatInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        while (args.watch) {
            sleep(1);
            client.Query(args, response);
            player.DisplayRxStatInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
        }
    } else if (args.command == Statistics::CLICommand::SPLIT_TRACE) {
        client.Query(args, response);
    } else if (args.command == Statistics::CLICommand::QBUF_POOL_STATS) {
        /* qbuf 池统计数据量大, 不支持 -w 持续刷新, 仅单次查询 */
        client.Query(args, response);
        player.DisplayQbufPoolStatsInfo(reinterpret_cast<uint8_t *>(response.Data()), response.DataLen());
    } else {
        CLI_LOG("Invalid command\n");
    }
    ock::ubs::LibcApi::UnLoad();

    return 0;
}