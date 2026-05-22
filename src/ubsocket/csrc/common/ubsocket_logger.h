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
#ifndef UBS_COMM_UBSOCKET_LOGGER_H
#define UBS_COMM_UBSOCKET_LOGGER_H

#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include "ubsocket_defines.h"

namespace ock {
namespace ubs {

using ExternalLog = void (*)(int level, const char *msg, const char *filename, int line);

enum LogLevel {
    LEVEL_DEBUG = 0,
    LEVEL_INFO,
    LEVEL_NOTICE,
    LEVEL_WARN,
    LEVEL_ERR,

    LEVEL_COUNT
};

class Logger {
public:
    static Logger &Instance()
    {
        static Logger logger;
        return logger;
    }

public:
    Logger() = default;
    ~Logger()
    {
        mLogFunc = nullptr;
    }

    void SetLogLevel(int level);
    int GetLogLevel() const;

    void SetExternalLogFunction(ExternalLog func);

    void Log(int level, const std::ostringstream &oss, const char *filename, int line) const;
    void Logv(int level, const char *filename, int line, const char *format, ...) const
    {
        std::ostringstream oss;
        char buffer[1024L];

        va_list va;
        va_start(va, format);
        const int len = std::vsnprintf(buffer, sizeof(buffer), format, va);
        va_end(va);

        if (UNLIKELY(len < 0)) {
            oss << "Invalid log format.";
        } else {
            oss << buffer;
        }

        Log(level, oss, filename, line);
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator=(Logger &&) = delete;

private:
    void LogDefault(int level, const std::string &msg, const char *filename, int line) const;

private:
    LogLevel logLevel = LEVEL_INFO;
    ExternalLog mLogFunc = nullptr;
};

#ifndef UBS_LOG_LINE
#define UBS_LOG_LINE __LINE__
#endif

#ifndef UBS_LOG_FILENAME
#define UBS_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define PID_TID " " << getpid() << ":" << syscall(SYS_gettid)

ALWAYS_INLINE int Logger::GetLogLevel() const
{
    return logLevel;
}

ALWAYS_INLINE void Logger::SetLogLevel(int level)
{
    if (level >= LEVEL_COUNT || level < LEVEL_DEBUG) {
        std::cout << "Invalid setting level." << std::endl;
        return;
    }

    logLevel = static_cast<LogLevel>(level);
}

ALWAYS_INLINE void Logger::SetExternalLogFunction(ExternalLog func)
{
    mLogFunc = func;
}

ALWAYS_INLINE void Logger::LogDefault(int level, const std::string &msg, const char *filename, int line) const
{
    static const char *levelStr[] = {"D", "I", "N", "W", "E"};

    struct timeval tv{};
    char strTime[24L];

    int ret = gettimeofday(&tv, nullptr);
    if (ret != 0) {
        std::cout << "Fail to get the current system time, " << ret << std::endl;
    }

    time_t timeStamp = tv.tv_sec;
    struct tm localTime{};
    struct tm *resultTime = localtime_r(&timeStamp, &localTime);
    if ((resultTime != nullptr) && (strftime(strTime, sizeof strTime, "%Y%m%d %H:%M:%S.", resultTime) != 0)) {
        std::cout << levelStr[level] << strTime << tv.tv_usec << PID_TID << " " << filename << ":" << line << "] "
                  << msg << '\n';
    } else {
        std::cout << "get time failed\n";
    }
}

ALWAYS_INLINE void Logger::Log(int level, const std::ostringstream &oss, const char *filename, int line) const
{
    if (mLogFunc != nullptr) {
        mLogFunc(level, oss.str().c_str(), filename, line);
    } else {
        LogDefault(level, oss.str(), filename, line);
    }
}

} // namespace ubs
} // namespace ock

#define UBS_LOG(level, __format, ...)                                                                \
    do {                                                                                             \
        if ((level) >= (ock::ubs::Logger::Instance().GetLogLevel())) {                               \
            ock::ubs::Logger::Instance().Logv(level, UBS_LOG_FILENAME, UBS_LOG_LINE, __format, ##__VA_ARGS__); \
        }                                                                                            \
    } while (0)

#define UBS_LOG_STREAM(level, ARGS)                                                       \
    do {                                                                                  \
        if ((level) >= (ock::ubs::Logger::Instance().GetLogLevel())) {                    \
            std::ostringstream oss;                                                       \
            oss << "[UBSOCKET " << __FUNCTION__ << "] " << ARGS;                          \
            ock::ubs::Logger::Instance().Log(level, oss, UBS_LOG_FILENAME, UBS_LOG_LINE); \
        }                                                                                 \
    } while (0)

#define UBS_VLOG_ERR(__format, ...) UBS_LOG(ock::ubs::LEVEL_ERR, __format, ##__VA_ARGS__)
#define UBS_VLOG_WARN(__format, ...) UBS_LOG(ock::ubs::LEVEL_WARN, __format, ##__VA_ARGS__)
#define UBS_VLOG_NOTICE(__format, ...) UBS_LOG(ock::ubs::LEVEL_NOTICE, __format, ##__VA_ARGS__)
#define UBS_VLOG_INFO(__format, ...) UBS_LOG(ock::ubs::LEVEL_INFO, __format, ##__VA_ARGS__)
#define UBS_VLOG_DEBUG(__format, ...) UBS_LOG(ock::ubs::LEVEL_DEBUG, __format, ##__VA_ARGS__)

#define UBS_SLOG_ERR(ARGS) UBS_LOG_STREAM(ock::ubs::LEVEL_ERR, ARGS)
#define UBS_SLOG_WARN(ARGS) UBS_LOG_STREAM(ock::ubs::LEVEL_WARN, ARGS)
#define UBS_SLOG_NOTICE(ARGS) UBS_LOG_STREAM(ock::ubs::LEVEL_NOTICE, ARGS)
#define UBS_SLOG_INFO(ARGS) UBS_LOG_STREAM(ock::ubs::LEVEL_INFO, ARGS)
#define UBS_SLOG_DEBUG(ARGS) UBS_LOG_STREAM(ock::ubs::LEVEL_DEBUG, ARGS)

#endif // UBS_COMM_UBSOCKET_LOGGER_H
