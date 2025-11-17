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
#include <string>
#include <thread>
#include <fstream>
#include "cmd_helper.h"
#include "htracer_client.h"
#include "htracer_utils.h"
#include "hcom/hcom_num_def.h"
#include "cmd_handler.h"

using namespace ock::hcom;

static CmdHelper g_cmdHelper;

void HTracerCliHelper::Initialize()
{
    std::map<std::string, std::shared_ptr<CmdHandler>> cmdHandlers = { { "show", std::make_shared<ShowCmdHandler>() },
                                                                       { "reset", std::make_shared<ResetCmdHandler>() },
                                                                       { "conf", std::make_shared<ConfCmdHandler>() } };
    g_cmdHelper.UpdateHost();
    cmdHandlers.swap(mCmdHandlers);
}

SerCode ShowCmdHandler::Handle(std::vector<std::string> cmds)
{
    /* !
     * -s 1 -i 1 -c 10000 -d /tmp/local
     */
    if (HTracerUtils::ExistCmdOption(cmds, "-h")) {
        std::cout << "Help info:" << std::endl;
        std::cout << HelpInfo() << std::endl;
        return SER_OK;
    }

    /*
     * interval time
     */
    uint32_t interval = ParseUintOption(cmds, "-i", 1);

    /*
     * latency quantile
     */
    double quantile = ParseDoubleOption(cmds, "-tp", -1.0, 0, NN_NO100);

    /*
     * round count
     */
    uint32_t count = ParseUintOption(cmds, "-n", 1);

    /*
     * dump result to file
     */
    std::ofstream dump;
    auto dumpPath = HTracerUtils::GetCmdOption(cmds, "-d");
    if (!dumpPath.empty()) {
        dump.open(dumpPath, std::ios::out | std::ios::app);
    }

    std::ostream &out = dump.is_open() ? dump : std::cout;
    for (uint32_t i = 0; i < count; ++i) {
        out << "Round:" << (i + 1) << " " << HTracerUtils::CurrentTime() << std::endl;
        ProcessTraceData(out, quantile);
        if (count != 1) {
            sleep(interval);
        }
    }

    return SER_OK;
}

uint32_t ShowCmdHandler::ParseUintOption(const std::vector<std::string> &cmds, const std::string &opt,
    uint32_t defaultValue)
{
    auto param = HTracerUtils::GetCmdOption(cmds, opt);
    return param.empty() ? defaultValue : std::atol(param.c_str());
}

double ShowCmdHandler::ParseDoubleOption(const std::vector<std::string> &cmds, const std::string &opt,
    double defaultValue, double min, double max)
{
    auto param = HTracerUtils::GetCmdOption(cmds, opt);
    if (param.empty()) {
        return defaultValue;
    }
    double val = std::atof(param.c_str());
    return (val > min && val < max) ? val : defaultValue;
}

void ShowCmdHandler::ProcessTraceData(std::ostream &out, double quantile)
{
    std::map<std::string, TTraceInfo> sumTraceInfoMap;
    g_cmdHelper.UpdateHost(quantile);
    auto hostInfo = g_cmdHelper.GetHostInfo();
    auto &processes = hostInfo.GetAllProcesses();
    for (const auto &process : processes) {
        auto &traceInfos = process.second->GetAllTraceInfos();
        for (const auto &traceInfo : traceInfos) {
            auto iter = sumTraceInfoMap.find(traceInfo.name);
            if (iter == sumTraceInfoMap.end()) {
                sumTraceInfoMap.insert(std::make_pair(traceInfo.name, traceInfo));
            } else {
                iter->second += traceInfo;
            }
        }
    }

    out << TTraceInfo::HeaderString() << std::endl;
    for (const auto &traceInfo : sumTraceInfoMap) {
        out << "\t" << traceInfo.second.ToString() << std::endl;
    }
}

std::string ShowCmdHandler::HelpInfo()
{
    std::stringstream ss;
    ss << "\t -i print interval. "<< std::endl <<
      "\t -n number of times. "<< std::endl <<
      "\t -d dump trace point information. -d /opt/dump.text " << std::endl <<
      "\t -tp show percentile of latency, need to use \'conf -p\' to enable it first!" << std::endl;
    return ss.str();
}

SerCode HTracerCliHelper::HandleCmd(std::string cmd)
{
    auto cmds = HTracerUtils::StrSplit(cmd, ' ');
    if (cmds.empty()) {
        std::cout << "Invalid command!" << std::endl <<
               "\tshow : show trace information" << std::endl <<
               "\treset : clear invalid host and reset trace" << std::endl <<
               "\tconf : config trace" << std::endl <<
               "\tquit : quit trace" << std::endl <<
               "\tcommand -h : show help information for command. e.g. show -h" << std::endl;
        return SER_ERROR;
    }

    std::string cmdType = cmds[0];
    cmds.erase(cmds.begin());
    auto cmdHandlerIt = mCmdHandlers.find(cmdType);
    if (cmdHandlerIt == mCmdHandlers.end()) {
        std::cout << "Invalid command!" << std::endl <<
               "\tshow : show trace information" << std::endl <<
               "\treset : clear invalid host and reset trace" << std::endl <<
               "\tconf : config trace" << std::endl <<
               "\tquit : quit trace" << std::endl <<
               "\tcommand -h : show help information for command. e.g. show -h" << std::endl;
        return SER_ERROR;
    }
    return cmdHandlerIt->second->Handle(cmds);
}

SerCode ResetCmdHandler::Handle(std::vector<std::string> cmds)
{
    if (!cmds.empty()) {
        std::cout << "Invalid param" << std::endl;
        return SER_ERROR;
    }

    g_cmdHelper.ResetTraceInfo();

    return SER_OK;
}

std::string ResetCmdHandler::HelpInfo()
{
    return std::string("");
}

SerCode ConfCmdHandler::Handle(std::vector<std::string> cmds)
{
    if (HTracerUtils::ExistCmdOption(cmds, "-h")) {
        std::cout << "Help info:" << std::endl;
        std::cout << HelpInfo() << std::endl;
        return SER_OK;
    }
    /* enable trace */
    bool enable = true;
    auto enableParam = HTracerUtils::GetCmdOption(cmds, "-t");
    if (!enableParam.empty()) {
        enable = std::stoi(enableParam);
    }

    /* enable tp */
    bool enableTp = false;
    auto enableTpParam = HTracerUtils::GetCmdOption(cmds, "-p");
    if (!enableTpParam.empty()) {
        enableTp = std::atoi(enableTpParam.c_str());
    }

    /* enable log */
    bool enableLog = false;
    auto enableLogParam = HTracerUtils::GetCmdOption(cmds, "-o");
    if (!enableLogParam.empty()) {
        enableLog = std::atoi(enableLogParam.c_str());
    }

    /* log path */
    auto logPath = HTracerUtils::GetCmdOption(cmds, "-d");
    if (logPath.size() > NN_NO260) {
        std::cout << "Invalid log path param" << std::endl;
        logPath = "";
    }

    HandlerConfPara confPara(enable, enableTp, enableLog, logPath);
    g_cmdHelper.EnableTrace(confPara);
    return SER_OK;
}

std::string ConfCmdHandler::HelpInfo()
{
    std::stringstream ss;
    ss << "\t -t 1:enable or 0:disable trace, default 1. " << std::endl;
    ss << "\t -p 1:enable tp or 0:disable tp, default 0. " << std::endl;
    ss << "\t -o 1:enable log or 0:disable log, default 0. " << std::endl;
    ss << "\t -d set dump log path, default path /tmp/htrace/log. " << std::endl;
    ss << "\t To successfully change the log path, set \"conf -o 0\" first. " << std::endl;
    return ss.str();
}