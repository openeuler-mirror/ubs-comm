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

#ifndef CMD_HANDLER
#define CMD_HANDLER

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>
#include "hcom/hcom_err.h"

using namespace ock::hcom;

class CmdHandler {
public:
    virtual SerCode Handle(std::vector<std::string> cmds) = 0;
    virtual std::string HelpInfo() = 0;
};

class ShowCmdHandler : public CmdHandler {
public:
    SerCode Handle(std::vector<std::string> cmds) override;

    std::string HelpInfo() override;

    uint32_t ParseUintOption(const std::vector<std::string> &cmds, const std::string &opt, uint32_t defaultValue);

    double ParseDoubleOption(const std::vector<std::string> &cmds, const std::string &opt, double defaultValue,
        double min, double max);

    void ProcessTraceData(std::ostream &out, double quantile);
};

class ResetCmdHandler : public CmdHandler {
public:
    SerCode Handle(std::vector<std::string> cmds) override;

    std::string HelpInfo() override;
};

class ConfCmdHandler : public CmdHandler {
public:
    SerCode Handle(std::vector<std::string> cmds) override;

    std::string HelpInfo() override;
};

class HTracerCliHelper {
public:
    void Initialize();

    SerCode HandleCmd(std::string cmd);

private:
    std::map<std::string, std::shared_ptr<CmdHandler>> mCmdHandlers;
};
#endif // CMD_HANDLER