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
#ifndef UBS_COMM_GOLDEN_CMD_DATA_H
#define UBS_COMM_GOLDEN_CMD_DATA_H

#include "golden_cmd.h"

namespace golden {
class SubCommandData : public SubCommand {
public:
    SubCommandData(const std::string &name, const ParamMap &params) : SubCommand(name, params) {}

protected:
    void SetRules() noexcept override {}

    int DoInitialize() noexcept override
    {
        return 0;
    }

    int DoExecute() noexcept override
    {
        std::cout << "execute pp" << std::endl;
        return 0;
    }
};

static SubCommand *CreateData(const ParamMap &params)
{
    return new (std::nothrow) SubCommandData(SUB_CMD_DATA, params);
}
} // namespace golden

#endif //UBS_COMM_GOLDEN_CMD_DATA_H
