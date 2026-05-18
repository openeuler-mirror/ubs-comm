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
#ifndef UBS_COMM_GOLDEN_CMD_H
#define UBS_COMM_GOLDEN_CMD_H

#include "golden_cmd_parser.h"
#include "golden_common.h"

namespace golden {
/******************************/
/* sub command base           */
/******************************/
class SubCommand {
public:
    SubCommand(const std::string &name, const ParamMap &params) : name_(name), params_(params) {}

    const std::string &Name() const noexcept
    {
        return name_;
    }

    int Execute() noexcept;

protected:
    virtual void SetRules() noexcept {}

    virtual int DoPrintHelp() noexcept;

    virtual int DoParamByRule() noexcept;

protected:
    virtual int DoInitialize() noexcept = 0;

    virtual int DoExecute() noexcept = 0;

protected:
    std::string name_;
    ParamMap params_;
    ParamRuleMap param_rules_;
    std::vector<std::string> example_;
};

inline int SubCommand::Execute() noexcept
{
    SetRules();

    if (params_.find(SUB_CMD_MINUS_H) != params_.end()) {
        return DoPrintHelp();
    }

    auto verified = DoParamByRule();
    if (verified != 0) {
        std::cout << std::endl;
        DoPrintHelp();
        return verified;
    }

    auto inited = DoInitialize();
    if (inited != 0) {
        std::cout << "Initialize sub command '" << name_ << "' failed" << std::endl;
        return inited;
    }

    return DoExecute();
}

inline int SubCommand::DoPrintHelp() noexcept
{
    std::cout << "Params for sub command '" + name_ + "':" << std::endl;
    for (auto &rule : param_rules_) {
        std::cout << "  --" << std::left << std::setw(10) << rule.first << "    " << rule.second.HelpString()
                  << std::endl;
    }
    std::cout << std::endl;

    std::cout << "Example:" << std::endl;
    for (auto &item : example_) {
        std::cout << "  " << item << std::endl;
    }

    return 0;
}

inline int SubCommand::DoParamByRule() noexcept
{
    for (auto &rule : param_rules_) {
        auto paramIter = params_.find(rule.first);

        /* if required param is not set, return -1 */
        if (paramIter == params_.end() && rule.second.required) {
            std::cout << "Execute sub command '" + name_ + "' failed, as " << rule.second.RequiredError() << std::endl;
            return -1;
        }

        /* if set, but failed to convert */
        if (!rule.second.SetValueFromString(paramIter->second)) {
            std::cout << "Execute sub command '" + name_ + "' failed, as " << rule.second.InvalidError() << std::endl;
            return -1;
        }

        /* if converted, but is not meet rule */
        if (!rule.second.IsValueValid()) {
            std::cout << "Execute sub command '" + name_ + "' failed, as " << rule.second.InvalidError() << std::endl;
            return -1;
        }

        /* all passed, then remove param from map */
        params_.erase(paramIter);
    }

    if (!params_.empty()) {
        std::cout << "Execute sub command '" + name_ + "' failed, as the following param is not for it:" << std::endl;
        for (auto &param : params_) {
            std::cout << "  --" << param.first << "=" << param.second << std::endl;
        }
        std::cout << std::endl;
        return -1;
    }

    return 0;
}

/******************************/
/* sub command registry       */
/******************************/
using SubCommandCreateFunc = SubCommand *(*)(const ParamMap &params);

class SubCommandRegistry {
public:
    static SubCommandRegistry &Instance()
    {
        static SubCommandRegistry registry;
        return registry;
    }

    /**
     * @brief Get the create command function for execution
     *
     * @param name         [in] name of sub command
     * @return func to create the sub command
     */
    SubCommandCreateFunc GetCommandCreateFunc(const std::string &name) noexcept;

    /**
     * @brief Register a create sub command func to this registry
     *
     * @param name         [in] name of the sub command
     * @param func         [in] create func of the sub command
     */
    void RegisterSubCommand(const std::string &name, SubCommandCreateFunc func) noexcept;

    /**
     * @brief Register all
     */
    void RegisterAll() noexcept;

private:
    std::map<std::string, SubCommandCreateFunc> sub_commands_;
};

inline SubCommandCreateFunc SubCommandRegistry::GetCommandCreateFunc(const std::string &name) noexcept
{
    auto iter = sub_commands_.find(name);
    if (iter != sub_commands_.end()) {
        return iter->second;
    }

    return nullptr;
}

inline void SubCommandRegistry::RegisterSubCommand(const std::string &name, golden::SubCommandCreateFunc func) noexcept
{
    if (name.empty()) {
        LOG_INFO("Invalid param, name is empty");
        return;
    }
    if (func == nullptr) {
        LOG_INFO("Invalid param, func is nullptr");
        return;
    }

    auto iter = sub_commands_.find(name);
    if (iter != sub_commands_.end()) {
        LOG_INFO("func of " << name << " already registered");
        return;
    }

    sub_commands_[name] = func;
}
} // namespace golden

#endif // UBS_COMM_GOLDEN_CMD_H
