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
#ifndef UBS_COMM_GOLDEN_CMD_PARSER_H
#define UBS_COMM_GOLDEN_CMD_PARSER_H

#include "golden_common.h"

namespace golden {

class CmdParser {
public:
    CmdParser(int argc, char *argv[]) noexcept
    {
        Parse(argc, argv);
    }

    std::string Get(const std::string &key, const std::string &default_val = "") const
    {
        auto it = params_.find(key);
        if (it != params_.end()) {
            return it->second;
        }
        return default_val;
    }

    bool Has(const std::string &key) const
    {
        return params_.count(key) > 0;
    }

    const std::string &SumCommand() const noexcept
    {
        return sub_cmd_;
    }

    const ParamMap &Params() const noexcept
    {
        return params_;
    }

private:
    void Parse(int argc, char *argv[])
    {
        if (argc <= 1) {
            sub_cmd_ = "help";
            return;
        }

        if (argc >= 2) {
            sub_cmd_ = argv[1];
        }

        if (argc == 3 && std::string(argv[2]) == SUB_CMD_MINUS_H) {
            params_[SUB_CMD_MINUS_H] = "";
            return;
        }

        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg.substr(0, 2) != "--") {
                continue;
            }

            size_t eq_pos = arg.find('=');
            if (eq_pos == std::string::npos) {
                continue;
            }

            std::string key = arg.substr(2, eq_pos - 2);
            std::string val = arg.substr(eq_pos + 1);

            params_[key] = val;
        }
    }

private:
    std::string sub_cmd_;
    ParamMap params_;
};
} // namespace golden

#endif // UBS_COMM_GOLDEN_CMD_PARSER_H
