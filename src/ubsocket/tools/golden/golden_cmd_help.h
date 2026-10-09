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
#ifndef UBS_COMM_GOLDEN_CMD_HELP_H
#define UBS_COMM_GOLDEN_CMD_HELP_H

#include "golden_cmd.h"

namespace golden {
class SubCommandHelp : public SubCommand {
public:
    SubCommandHelp(const std::string &name, const ParamMap &params) : SubCommand(name, params) {}

protected:
    void SetRules() noexcept override {}

    int DoPrintHelp() noexcept override
    {
        return 0;
    }

    int DoParamByRule() noexcept override
    {
        return 0;
    }

    int DoInitialize() noexcept override
    {
        return 0;
    }

    int DoExecute() noexcept override
    {
        printf("Supported sub commands:\n");
        for (auto &item : cmds) {
            std::cout << "  " << std::left << std::setw(10) << item.name.c_str() << item.help_str.c_str() << std::endl;
        }

        printf("\nSupported options:\n");
        std::cout << "  " << std::left << std::setw(10) << "-v"
                  << "print version" << std::endl;
        std::cout << "  " << std::left << std::setw(10) << "-h"
                  << "print help tips" << std::endl;

        return 0;
    }
};

static SubCommand *CreateHelp(const ParamMap &params)
{
    return new (std::nothrow) SubCommandHelp(SUB_CMD_MINUS_H, params);
}
} // namespace golden
#endif // UBS_COMM_GOLDEN_CMD_HELP_H
