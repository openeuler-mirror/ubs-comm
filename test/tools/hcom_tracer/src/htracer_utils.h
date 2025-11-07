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

#ifndef HTRACE_UTILS_H
#define HTRACE_UTILS_H

#include "securec.h"
#include "hcom/hcom_num_def.h"
#include <vector>
#include <cstring>
#include <memory>
#include <algorithm>
#include <iomanip>
#include <unistd.h>
#include <sys/stat.h>
#include <sstream>
#include <linux/limits.h>

namespace ock {
namespace hcom {
class HTracerUtils {
public:
    static std::vector<std::string> StrSplit(const std::string &str, char delim)
    {
        std::vector<std::string> res;
        std::string::size_type start = 0;
        while (true) {
            auto pos = str.find(delim, start);
            if (pos == std::string::npos) {
                res.push_back(str.substr(start));
                break;
            }
            res.push_back(str.substr(start, pos - start));
            start = pos + 1;
        }
        return res;
    }

    static std::string GetCmdOption(char **begin, char **end, const std::string &option)
    {
        char **itr = std::find(begin, end, option);
        if (itr != end && ++itr != end) {
            return *itr;
        }
        return "";
    }

    static bool ExistCmdOption(std::vector<std::string> cmds, const std::string &option)
    {
        auto itr = std::find(cmds.begin(), cmds.end(), option);
        return itr != cmds.end();
    }

    static std::string GetCmdOption(std::vector<std::string> cmds, const std::string &option)
    {
        auto itr = std::find(cmds.begin(), cmds.end(), option);
        if (itr != cmds.end() && ++itr != cmds.end()) {
            return *itr;
        }
        return "";
    }

    static std::string CurrentTime()
    {
        time_t rawTime;
        time(&rawTime);
        auto tmInfo = localtime(&rawTime);
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(NN_NO4) << std::right << (NN_NO1900 + tmInfo->tm_year) << "-" <<
            std::setfill('0') << std::setw(NN_NO2) << std::right << (NN_NO1 + tmInfo->tm_mon) << "-" <<
            std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo->tm_mday << " " << std::setfill('0') <<
            std::setw(NN_NO2) << std::right << tmInfo->tm_hour << ":" << std::setfill('0') << std::setw(NN_NO2) <<
            std::right << tmInfo->tm_min << ":" << std::setfill('0') << std::setw(NN_NO2) << std::right <<
            tmInfo->tm_sec;
        return ss.str();
    }
};
}
}
#endif // HTRACE_UTILS_H