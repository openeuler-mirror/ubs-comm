/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <iostream>
#include "hcom/hcom_num_def.h"
#include "hcom/hcom_err.h"
#include "rpc_client.h"
#include "htracer_utils.h"
#include "cmd_handler.h"

using namespace ock::hcom;

/* !
 * support
 * 1. config service trace information support
 * 1.1 trace level
 * 1.2 trace on/off
 * 2. show trace information by service id support
 * 3. cross-node query support
 */

void InvalidParamPrint()
{
    std::cout << "Invalid parameters!"<<std::endl <<
           "\t -s server name." << std::endl <<
           "\t The server name is registerd when init" <<std::endl;
}

int main(int argc, char **argv)
{
    if (argc != NN_NO3) {
        InvalidParamPrint();
        return SER_ERROR;
    }

    auto serverName = HTracerUtils::GetCmdOption(argv, argv + argc, "-s");
    if (serverName.empty()) {
        InvalidParamPrint();
        return SER_ERROR;
    }
    RpcClient::serverName = serverName;
    HTracerCliHelper cliHelper;
    cliHelper.Initialize();

    while (true) {
        std::string cmd;
        std::cout << ">> ";
        getline(std::cin, cmd);
        if (cmd == "quit") {
            return SER_OK;
        }
        cliHelper.HandleCmd(cmd);
        std::cout << "Execution Done." << std::endl;
    }
    return SER_OK;
}