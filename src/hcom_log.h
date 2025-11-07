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
#ifndef OCK_COMM_LOG_12456845341233_H
#define OCK_COMM_LOG_12456845341233_H

#include <ctime>
#include <mutex>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <cstring>
#include "hcom_def.h"

namespace ock {
namespace hcom {
using ExternalLog = void (*)(int level, const char *msg);

class UBSHcomNetOutLogger {
public:
    static UBSHcomNetOutLogger *Instance()
    {
        if (NN_UNLIKELY(gLogger == nullptr)) {
            std::lock_guard<std::mutex> lock(gMutex);
            if (gLogger == nullptr) {
                gLogger = new (std::nothrow) UBSHcomNetOutLogger();
                if (gLogger == nullptr) {
                    std::cout << "Failed to new UBSHcomNetOutLogger, probably out of memory" << std::endl;
                }
                SetLogLevel();
            }
        }

        return gLogger;
    }

    static void SetLogLevel()
    {
        /* set one of 0,1,2,3 */
        char *envSize = ::getenv("HCOM_SET_LOG_LEVEL");
        if (envSize != nullptr) {
            long value = 0;
            if (!SetStrStol(envSize, value)) {
                std::cout << "Invalid setting 'HCOM_SET_LOG_LEVEL', should set one of 0,1,2,3 " << std::endl;
                return;
            }
            logLevel = value;
        }
    }

    inline static void SetLogLevel(int level)
    {
        if (level >= static_cast<int>(NN_NO0) && level <= static_cast<int>(NN_NO3)) {
            logLevel = level;
        }
    }

    static bool SetStrStol(const std::string &str, long &value)
    {
        char *remain = nullptr;
        errno = 0;
        value = std::strtol(str.c_str(), &remain, 10); // 10 is decimal digits
        if (remain == nullptr || strlen(remain) > 0 || value < NN_NO0 || value > NN_NO3 || errno == ERANGE) {
            return false;
        } else if (value == 0 && str != "0") {
            return false;
        }

        return true;
    }

    inline void SetExternalLogFunction(ExternalLog func)
    {
        mLogFunc = func;
    }

    static inline void Print(int level, const char *msg)
    {
        // See NN_LOG_DEBUG, NN_LOG_INFO, NN_LOG_WARN and NN_LOG_ERROR
        const char *levelStr[] = {"DEBUG", "INFO", "WARN", "ERROR"};

        struct timeval tv{};
        char strTime[24];

        int ret = gettimeofday(&tv, nullptr);
        if (ret != 0) {
            std::cout << "Fail to get the current system time, " << ret << ".\n";
        }
        time_t timeStamp = tv.tv_sec;
        struct tm localTime{};
        struct tm *resultTime = localtime_r(&timeStamp, &localTime);
        if ((resultTime != nullptr) &&
            (strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", resultTime) != NN_NO0)) {
            std::cout << strTime << tv.tv_usec << " " << levelStr[level] << " " << msg << '\n';
        } else {
            std::cout << "Invalid time trace " << tv.tv_usec << " " << levelStr[level] << " " << msg << '\n';
        }
    }

    inline void Log(int level, const std::ostringstream &oss) const
    {
        if (NN_LIKELY(mLogFunc != nullptr)) {
            mLogFunc(level, oss.str().c_str());
        } else {
            Print(level, oss.str().c_str());
        }
    }

    UBSHcomNetOutLogger(const UBSHcomNetOutLogger &) = delete;
    UBSHcomNetOutLogger &operator = (const UBSHcomNetOutLogger &) = delete;
    UBSHcomNetOutLogger(UBSHcomNetOutLogger &&) = delete;
    UBSHcomNetOutLogger &operator = (UBSHcomNetOutLogger &&) = delete;

    ~UBSHcomNetOutLogger()
    {
        mLogFunc = nullptr;
    }

    inline int GetLogLevel() const
    {
        return logLevel;
    }

private:
    UBSHcomNetOutLogger() = default;

private:
    static UBSHcomNetOutLogger *gLogger;
    static std::mutex gMutex;
    static int logLevel;

    ExternalLog mLogFunc = nullptr;
};

// macro for log
#ifndef NN_LOG_FILENAME
#define NN_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define NN_LOG(level, args)                                                        \
    do {                                                                           \
        if ((level) >= (UBSHcomNetOutLogger::Instance()->GetLogLevel())) {                \
            std::ostringstream oss;                                                \
            oss << "[HCOM " << NN_LOG_FILENAME << ":" << __LINE__ << "] " << args; \
            UBSHcomNetOutLogger::Instance()->Log(level, oss);                             \
        }                                                                          \
    } while (0)

#define NN_LOG_PRINT(level, args)                                                  \
    do {                                                                             \
        if ((level) >= (UBSHcomNetOutLogger::Instance()->GetLogLevel())) {                  \
            std::ostringstream oss;                                                  \
            oss << "[HCOM " << NN_LOG_FILENAME << ":" << __LINE__ << "] " << (args); \
            UBSHcomNetOutLogger::Instance()->Print(level, oss.str().c_str());               \
        }                                                                            \
    } while (0)

#define NN_LOG_DEBUG(args) NN_LOG(0, args)
#define NN_LOG_INFO(args) NN_LOG(1, args)
#define NN_LOG_WARN(args) NN_LOG(2, args)
#define NN_LOG_ERROR(args) NN_LOG(3, args)

#define NN_ASSERT_LOG_RETURN(args, RET)   \
    if (NN_UNLIKELY(!(args))) {           \
        NN_LOG_ERROR("Assert " << #args); \
        return RET;                       \
    }

#define NN_ASSERT_LOG_RETURN_VOID(args)   \
    if (NN_UNLIKELY(!(args))) {           \
        NN_LOG_ERROR("Assert " << #args); \
        return;                           \
    }

#ifdef NN_LOG_TRACE_INFO_ENABLED
#define NN_LOG_TRACE_INFO(args) NN_LOG_INFO(args)
#else
#define NN_LOG_TRACE_INFO(x)
#endif
}
}

#endif // OCK_COMM_LOG_12456845341233_H
