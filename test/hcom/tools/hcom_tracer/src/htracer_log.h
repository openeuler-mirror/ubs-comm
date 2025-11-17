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

#ifndef HTRACER_LOG_H
#define HTRACER_LOG_H

#include <iostream>
#include <cstring>
#include <sstream>
#include <memory>
#include <sys/time.h>

using ExternalLog = void (*)(int level, const char *msg);

class Logger {
public:
    Logger() {}

    static std::shared_ptr<Logger> Instance()
    {
        static auto logger = std::make_shared<Logger>();
        return logger;
    }

    inline void SetExternalLogFunction(ExternalLog func)
    {
        mLogFunc = func;
    }

    inline void Log(int32_t level, const std::ostringstream &oss)
    {
        if (mLogFunc != nullptr) {
            mLogFunc(level, oss.str().c_str());
            return;
        }
        static const char *levelName[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        struct timeval tv {};
        char strTime[24];

        gettimeofday(&tv, nullptr);
        strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", localtime(&tv.tv_sec));

        std::cout << "[" << strTime << tv.tv_usec << "]" <<
            "[" << levelName[level] << "]" << oss.str() << std::endl;
    }

private:
    ExternalLog mLogFunc = nullptr;
};

#ifndef __LOG_FILENAME__
#define __LOG_FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#ifndef LOG
#define LOG(level, message)                                                                              \
    do {                                                                                                 \
        std::ostringstream oss;                                                                          \
        oss << "[ " << __LOG_FILENAME__ << ":" << __LINE__ << " ][" << __FUNCTION__ << "]" << (message); \
        Logger::Instance()->Log(level, oss);                                                             \
    } while (0)
#endif

#ifdef LOG_ENABLED
#ifndef LOG_ERR
#define LOG_ERR(message) LOG(3, message)
#endif

#ifndef LOG_WARN
#define LOG_WARN(message) LOG(2, message)
#endif

#ifndef LOG_INFO
#define LOG_INFO(message) LOG(1, message)
#endif

#ifndef LOG_DEBUG
#define LOG_DEBUG(message) LOG(0, message)
#endif
#else
#define LOG_ERR(message)
#define LOG_WARN(message)
#define LOG_INFO(message)
#define LOG_DEBUG(message)
#endif
#endif // HTRACER_LOG_H