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
#ifndef HCOM_GOLDEN_CMD_SHOW_H
#define HCOM_GOLDEN_CMD_SHOW_H

#include "golden_cmd.h"

namespace golden {
class SubCommandShow : public SubCommand {
public:
    SubCommandShow(const std::string &name, const ParamMap &params) : SubCommand(name, params) {}

protected:
    void SetRules() noexcept override;

    int DoInitialize() noexcept override;

    int DoExecute() noexcept override;

    friend std::ostream &operator<<(std::ostream &os, const SubCommandShow &o)
    {
        os << "device type: " << o.device_type_ << ", device name filter: " << o.device_name_
           << ", show details: " << o.device_details_;
        return os;
    }

private:
    std::string device_type_;    /* type of device, e.g. ub | roce etc*/
    std::string device_name_;    /* sub part of device name */
    std::string device_details_; /* how many details to show */
};

static SubCommand *CreateShow(const ParamMap &params)
{
    return new (std::nothrow) SubCommandShow(SUB_CMD_SHOW, params);
}
} // namespace golden

#endif //HCOM_GOLDEN_CMD_SHOW_H
