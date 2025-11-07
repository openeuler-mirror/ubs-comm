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
#include "hcom_num_def.h"
#include "htracer_tdigest.h"
#include "net_common.h"
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
    static std::string &StrTrim(std::string &str)
    {
        if (str.empty()) {
            return str;
        }
        str.erase(0, str.find_first_not_of(" "));
        str.erase(str.find_last_not_of(" ") + 1);
        return str;
    }

    static std::string CurrentTime()
    {
        time_t rawTime;
        time(&rawTime);
        auto tmInfo = localtime(&rawTime);
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(NN_NO4) << std::right << (NN_NO1900 + tmInfo->tm_year) << "-" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << (NN_NO1 + tmInfo->tm_mon) << "-" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo->tm_mday << " " <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo->tm_hour << ":" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo->tm_min << ":" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo->tm_sec;
        return ss.str();
    }

    static std::string FormatString(std::string &name, uint64_t begin, uint64_t goodEnd, uint64_t badEnd, uint64_t min,
        uint64_t max, uint64_t total)
    {
        std::string str;
        std::ostringstream os(str);
        os.flags(std::ios::fixed);
        os.precision(NN_NO3);
        auto unitStep = NN_NO1000;
        os << std::left << std::setw(NN_NO50) << name
           << "\t" << std::left << std::setw(NN_NO15) << begin
           << "\t" << std::left << std::setw(NN_NO15) << goodEnd
           << "\t" << std::left << std::setw(NN_NO15) << badEnd
           << "\t" << std::left << std::setw(NN_NO15) << ((begin > goodEnd - badEnd) ? (begin - goodEnd - badEnd) : 0)
           << "\t" << std::left << std::setw(NN_NO15) << (min == UINT64_MAX ? 0 : ((double)min / unitStep))
           << "\t" << std::left << std::setw(NN_NO15) << (double)max / unitStep
           << "\t" << std::left << std::setw(NN_NO15) << (goodEnd == 0 ? 0 : (double)total / goodEnd / unitStep)
           << "\t" << std::left << std::setw(NN_NO15) << (double)total / unitStep;
        return os.str();
    }

    static int CreateDirectory(const std::string &name)
    {
        std::vector<std::string> paths;
        NetFunc::NN_SplitStr(name, "/", paths);
        int32_t ret = 0;
        std::string pathTmp;
        for (auto &item : paths) {
            if (item.empty()) {
                continue;
            }

            pathTmp += "/" + item;
            if (access(pathTmp.c_str(), F_OK) != 0) {
                mode_t old_mask = umask(0);
                ret = mkdir(pathTmp.c_str(), S_IRWXU | S_IRGRP | S_IXGRP);
                umask(old_mask);
                if (ret != 0 && errno != EEXIST) {
                    break;
                }
            }
        }
        return ret;
    }

    /* *
     * @brief Check whether the path is canonical, and canonical it.
     */
    static bool CanonicalPath(std::string &path)
    {
        if (path.empty() || path.size() > PATH_MAX) {
            return false;
        }

        /* It will allocate memory to store path */
        char *realPath = realpath(path.c_str(), nullptr);
        if (realPath == nullptr) {
            return false;
        }

        path = realPath;
        free(realPath);
        realPath = nullptr;
        return true;
    }
};
}
}
#endif // HTRACE_UTILS_H
