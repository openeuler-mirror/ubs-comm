/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include "common/perf_test_logger.h"

namespace hcom {
namespace perftest {

PerfTestLogger *PerfTestLogger::gLogger = nullptr;
std::mutex PerfTestLogger::gMutex;
int PerfTestLogger::logLevel = PERF_TEST_NO1;

PerfTestLogger *PerfTestLogger::Instance()
{
    if (gLogger == nullptr) {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gLogger == nullptr) {
            gLogger = new (std::nothrow) PerfTestLogger();
            if (gLogger == nullptr) {
                std::cout << "Failed to new PerfTestLogger, probably out of memory" << std::endl;
            }
            SetLogLevel();
        }
    }

    return gLogger;
}

void PerfTestLogger::SetLogLevel()
{
    /* set one of 0,1,2,3 */
    char *envSize = ::getenv("HCOM_PERF_TEST_LOG_LEVEL");
    if (envSize != nullptr) {
        long value = 0;
        if (!SetStrStol(envSize, value)) {
            std::cout << "Invalid setting 'HCOM_PERF_TEST_LOG_LEVEL', should set one of 0,1,2,3 " << std::endl;
            return;
        }
        logLevel = value;
    }
}

void PerfTestLogger::SetLogLevel(int level)
{
    if (level >= static_cast<int>(PERF_TEST_NO0) && level <= static_cast<int>(PERF_TEST_NO3)) {
        logLevel = level;
    }
}

bool PerfTestLogger::SetStrStol(const std::string &str, long &value)
{
    char *remain = nullptr;
    errno = 0;
    value = std::strtol(str.c_str(), &remain, 10); // 10 is decimal digits
    if (remain == nullptr || strlen(remain) > 0 || value < PERF_TEST_NO0 || value > PERF_TEST_NO3 || errno == ERANGE) {
        return false;
    } else if (value == 0 && str != "0") {
        return false;
    }

    return true;
}

void PerfTestLogger::Log(int level, const std::ostringstream &oss) const
{
    struct timeval tv {};
    char strTime[24];

    int ret = gettimeofday(&tv, nullptr);
    if (ret != 0) {
        std::cout << "Fail to get the current system time, " << ret << "." << std::endl;
    }
    time_t timeStamp = tv.tv_sec;
    struct tm localTime {};
    struct tm *resultTime = localtime_r(&timeStamp, &localTime);
    if ((resultTime != nullptr) &&
        (strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S.", resultTime) != PERF_TEST_NO0)) {
        std::cout << strTime << tv.tv_usec << " " << level << " " << oss.str().c_str() << std::endl;
    } else {
        std::cout << "Invalid time trace " << tv.tv_usec << " " << level << " " << oss.str().c_str() << std::endl;
    }
}

}
}
