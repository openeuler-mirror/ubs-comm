/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for cli message, etc
 * Author:
 * Create: 2026-03-02
 * Note:
 * History: 2026-03-02
*/

#ifndef UTRACER_UTILS_H
#define UTRACER_UTILS_H

#include <vector>
#include <cstring>
#include <memory>
#include <algorithm>
#include <iomanip>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ctime>
#include <sstream>
#include <linux/limits.h>
#include "securec.h"
#include "utracer_tdigest.h"
#include "utracer_def.h"

namespace Statistics {
class UTracerUtils {
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
        (void)time(&rawTime);
        struct tm tmInfo;
        struct tm* result = localtime_r(&rawTime, &tmInfo);
        if (result == nullptr) {
            return "";
        }
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(NN_NO4) << std::right << (NN_NO1900 + tmInfo.tm_year) << "-" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << (NN_NO1 + tmInfo.tm_mon) << "-" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo.tm_mday << " " <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo.tm_hour << ":" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo.tm_min << ":" <<
        std::setfill('0') << std::setw(NN_NO2) << std::right << tmInfo.tm_sec;
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

    static void SplitStr(const std::string &str, const std::string &separator, std::vector<std::string> &result)
    {
        result.clear();
        std::string::size_type pos1 = 0;
        std::string::size_type pos2 = str.find(separator);

        std::string tmpStr;
        while (pos2 != std::string::npos) {
            tmpStr = str.substr(pos1, pos2 - pos1);
            result.emplace_back(tmpStr);
            pos1 = pos2 + separator.size();
            pos2 = str.find(separator, pos1);
        }

        if (pos1 != str.length()) {
            tmpStr = str.substr(pos1);
            result.emplace_back(tmpStr);
        }
    }

    static int CreateDirectory(const std::string &name)
    {
        std::vector<std::string> paths;
        SplitStr(name, "/", paths);
        int32_t ret = 0;
        std::string pathTmp;
        for (auto &item : paths) {
            if (item.empty()) {
                continue;
            }

            pathTmp += "/" + item;
            mode_t old_mask = umask(0);
            ret = mkdir(pathTmp.c_str(), S_IRWXU | S_IRGRP | S_IXGRP);
            umask(old_mask);

            if (ret != 0 && errno != EEXIST) {
                break;
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
#endif // UTRACER_UTILS_H
