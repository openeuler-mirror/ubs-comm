/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Description: Define a hook function that allows ubsocket logs to be print in brpc.
 */

#include "ubsocket_logger.h"

namespace ock {
namespace ubs {

Logger *Logger::gLogger = nullptr;
std::mutex Logger::gMutex;
int Logger::logLevel = ubsocket::UTIL_VLOG_LEVEL_INFO;

void Logger::SetLogLevel()
{
    logLevel = ubsocket::UTIL_VLOG_LEVEL_INFO;
}

void Logger::SetLogLevel(int level)
{
    if (level >= ubsocket::UTIL_VLOG_LEVEL_EMERG && level <= ubsocket::UTIL_VLOG_LEVEL_DEBUG) {
        logLevel = level;
    } else {
        std::cout << "Invalid setting level." << std::endl;
    }
}

void Logger::SetExternalLogFunction(ExternalLog func)
{
    mLogFunc = func;
}

Logger::~Logger()
{
    mLogFunc = nullptr;
}

}
}

void UbsocketSetLogHandler(UbsocketLogHandler h)
{
    ock::ubs::Logger::Instance()->SetExternalLogFunction(h);
}
