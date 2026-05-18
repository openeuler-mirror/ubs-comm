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
#ifndef UBS_COMM_GOLDEN_LOG_H
#define UBS_COMM_GOLDEN_LOG_H

#include <sys/time.h>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sstream>

namespace golden {

static void log(const std::string &level, const std::string &msg)
{
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
        std::cout << level << strTime << tv.tv_usec << " " << msg << '\n';
    } else {
        std::cout << "failed to get date\n";
    }
}

#ifndef GOLDEN_LINE
#define GOLDEN_LINE __LINE__
#endif

#ifndef GOLDEN_LOG_FILENAME
#define GOLDEN_LOG_FILENAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define LOG(LEVEL, ARGS)                                                                                  \
    do {                                                                                                  \
        std::ostringstream oss;                                                                           \
        oss << GOLDEN_LOG_FILENAME << ":" << GOLDEN_LINE << "] [GOLDEN " << __FUNCTION__ << "] " << ARGS; \
        log(#LEVEL, oss.str());                                                                           \
    } while (0)

#define LOG_INFO(ARGS) LOG(I, ARGS)
#define LOG_DEBUG(ARGS) LOG(D, ARGS)
#define LOG_ERROR(ARGS) LOG(E, ARGS)
} // namespace golden

#endif // UBS_COMM_GOLDEN_LOG_H
