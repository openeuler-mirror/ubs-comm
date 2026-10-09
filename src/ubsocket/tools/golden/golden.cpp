/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "common/ubsocket_version.h"
#include "golden_cmd.h"

int main(int argc, char *argv[])
{
    using namespace golden;

    /* initial all sub commands */
    SubCommandRegistry::Instance().RegisterAll();

    /* parse cmd str */
    CmdParser parser(argc, argv);

    /* get create func for sub command */
    auto sub_cmd_name = parser.SumCommand();
    if (sub_cmd_name == "-v") {
        std::string version = UBS_LIB_VERSION_FULL;
        version.replace(0, strlen("library "), "");
        std::cout << version << std::endl;
        return 0;
    }

    auto func = SubCommandRegistry::Instance().GetCommandCreateFunc(sub_cmd_name);
    if (func == nullptr) {
        std::cout << "Invalid sub command\n" << std::endl;
        func = SubCommandRegistry::Instance().GetCommandCreateFunc(SUB_CMD_MINUS_H);
    }

    /* set ubsocket logger level */
    auto log_level = getenv("GOLDEN_LOG_LEVEL");
    if (log_level != nullptr) {
        ubsocket_set_log_level(atoi(log_level));
        Log::Instance().set_log_level(atoi(log_level));
    } else {
        ubsocket_set_log_level(3);
        Log::Instance().set_log_level(3);
    }

    /* create sub command */
    auto sub_cmd = func(parser.Params());
    if (sub_cmd == nullptr) {
        return 0;
    }

    /* execute sub command */
    return sub_cmd->Execute();
}