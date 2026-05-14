/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UBSOCKET_LOGGER_H
#define UBS_COMM_UBSOCKET_LOGGER_H

#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <time.h>
#include <sys/time.h>
#include <cstring>
#include <cstdio>

#include "util_vlog.h"

namespace ock {
namespace ubs {

using ExternalLog = void (*)(int level, const char *msg, const char *filename, int line);

class Logger {
public:
    static inline Logger *Instance();
    static void SetLogLevel();
    inline static void SetLogLevel(int level);
    inline void SetExternalLogFunction(ExternalLog func);
    static inline void Print(int level, const char *msg);
    inline void Log(int level, const std::ostringstream &oss,
        const char *filename, int line) const;
    Logger(const Logger &) = delete;
    Logger &operator = (const Logger &) = delete;
    Logger(Logger &&) = delete;
    Logger &operator = (Logger &&) = delete;
    ~Logger();
    inline int GetLogLevel() const;

private:
    Logger() = default;
    static Logger *gLogger;
    static std::mutex gMutex;
    static int logLevel;
    ExternalLog mLogFunc = nullptr;
};

#ifndef NN_LOG_LINE
#define NN_LOG_LINE __LINE__
#endif

#ifndef NN_LOG_FILENAME
#define NN_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

inline Logger *Logger::Instance()
{
    if (gLogger == nullptr) {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gLogger == nullptr) {
            gLogger = new (std::nothrow) Logger();
            if (gLogger == nullptr) {
                std::cout << "Failed to new Logger, probably out of memory" << std::endl;
            }
            SetLogLevel();
        }
    }

    return gLogger;
}

inline void Logger::Print(int level, const char *msg)
{
    const char *levelStr[] = {
        "EMERG",
        "ALERT",
        "CRIT",
        "ERROR",
        "WARN",
        "NOTICE",
        "INFO",
        "DEBUG",
    };

    struct timeval tv{};
    constexpr mode_t TIME_SIZE = 24;
    char strTime[TIME_SIZE];

    int ret = gettimeofday(&tv, nullptr);
    if (ret != 0) {
        std::cout << "Fail to get the current system time, " << ret << ".\n";
    }
    time_t timeStamp = tv.tv_sec;
    struct tm localTime{};
    struct tm *resultTime = localtime_r(&timeStamp, &localTime);
    if ((resultTime != nullptr) &&
        (strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", resultTime) != 0)) {
        std::cout << strTime << tv.tv_usec << " " << levelStr[level] << " " << msg << '\n';
    } else {
        std::cout << "Invalid time trace " << tv.tv_usec << " " << levelStr[level] << " " << msg << '\n';
    }
}

inline void Logger::Log(int level, const std::ostringstream &oss,
    const char *filename, int line) const
{
    if (mLogFunc != nullptr) {
        mLogFunc(level, oss.str().c_str(), filename, line);
    } else {
        Print(level, oss.str().c_str());
    }
}

inline int Logger::GetLogLevel() const
{
    return logLevel;
}

}
}

typedef void (*UbsocketLogHandler)(int level, const char *msg, const char *filename, int line);

void UbsocketSetLogHandler(UbsocketLogHandler h);

#define NN_LOG(level, __format, ...)                                                        \
    do {                                                                                    \
        if ((level) <= (ock::ubs::Logger::Instance()->GetLogLevel())) {                     \
            std::ostringstream oss;                                                         \
            constexpr mode_t BUFFER_SIZE = 1024;                                            \
            char buffer[BUFFER_SIZE];                                                       \
            std::snprintf(buffer, sizeof(buffer), __format, ##__VA_ARGS__);                 \
            oss << buffer;                                                                  \
            ock::ubs::Logger::Instance()->Log(level, oss, NN_LOG_FILENAME, NN_LOG_LINE);    \
        }                                                                                   \
    } while (0)

#define NN_LOG_ERR(__format, ...) NN_LOG(ubsocket::UTIL_VLOG_LEVEL_ERR, __format, ##__VA_ARGS__)
#define NN_LOG_WARN(__format, ...) NN_LOG(ubsocket::UTIL_VLOG_LEVEL_WARN, __format, ##__VA_ARGS__)
#define NN_LOG_NOTICE(__format, ...) NN_LOG(ubsocket::UTIL_VLOG_LEVEL_NOTICE, __format, ##__VA_ARGS__)
#define NN_LOG_INFO(__format, ...) NN_LOG(ubsocket::UTIL_VLOG_LEVEL_INFO, __format, ##__VA_ARGS__)
#define NN_LOG_DEBUG(__format, ...) NN_LOG(ubsocket::UTIL_VLOG_LEVEL_DEBUG, __format, ##__VA_ARGS__)

#define RPC_ADPT_VLOG_ERR(__error_type, __format, ...)  \
  NN_LOG_ERR(__format, ##__VA_ARGS__)
#define RPC_ADPT_VLOG_WARN(__format, ...)  \
  NN_LOG_WARN(__format, ##__VA_ARGS__)
#define RPC_ADPT_VLOG_NOTICE(__format, ...)  \
  NN_LOG_NOTICE(__format, ##__VA_ARGS__)
#define RPC_ADPT_VLOG_INFO(__format, ...)  \
  NN_LOG_INFO(__format, ##__VA_ARGS__)
#define RPC_ADPT_VLOG_DEBUG(__format, ...)  \
  NN_LOG_DEBUG(__format, ##__VA_ARGS__)

#endif // UBS_COMM_UBSOCKET_LOGGER_H
