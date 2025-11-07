/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#ifndef PERF_TEST_LOG_H
#define PERF_TEST_LOG_H

#include <ctime>
#include <mutex>
#include <iostream>
#include <sstream>
#include <sys/time.h>
#include <cstring>

namespace hcom {
namespace perftest {

#define PERF_TEST_NO0 0
#define PERF_TEST_NO1 1
#define PERF_TEST_NO2 2
#define PERF_TEST_NO3 3

class PerfTestLogger {
public:
    static PerfTestLogger *Instance();

    static void SetLogLevel();

    static void SetLogLevel(int level);

    static bool SetStrStol(const std::string &str, long &value);

    void Log(int level, const std::ostringstream &oss) const;

    PerfTestLogger(const PerfTestLogger &) = delete;
    PerfTestLogger &operator = (const PerfTestLogger &) = delete;
    PerfTestLogger(PerfTestLogger &&) = delete;
    PerfTestLogger &operator = (PerfTestLogger &&) = delete;

    ~PerfTestLogger() {}

    inline int GetLogLevel()
    {
        return logLevel;
    }

private:
    PerfTestLogger() = default;

private:
    static PerfTestLogger *gLogger;
    static std::mutex gMutex;
    static int logLevel;
};

// macro for log
#ifndef PERF_TEST_LOG_FILENAME
#define PERF_TEST_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define PERTTEST_LOG(level, args)                                                              \
    do {                                                                                       \
        if ((level) >= (PerfTestLogger::Instance()->GetLogLevel())) {                          \
            std::ostringstream oss;                                                            \
            oss << "[perf_test " << PERF_TEST_LOG_FILENAME << ":" << __LINE__ << "] " << args; \
            PerfTestLogger::Instance()->Log(level, oss);                                       \
        }                                                                                      \
    } while (0)

#define LOG_DEBUG(args) PERTTEST_LOG(0, args)
#define LOG_INFO(args) PERTTEST_LOG(1, args)
#define LOG_WARN(args) PERTTEST_LOG(2, args)
#define LOG_ERROR(args) PERTTEST_LOG(3, args)

}
}

#endif
