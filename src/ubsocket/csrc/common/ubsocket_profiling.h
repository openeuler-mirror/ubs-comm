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
#ifndef UBS_COMM_UBSOCKET_COMMON_PROFILING_H
#define UBS_COMM_UBSOCKET_COMMON_PROFILING_H

#include <cstdint>
#include "profiling/ubsocket_prof.h"

namespace ock {
namespace ubs {
enum ProfilingTPId : uint32_t {
    CORE_CONNECT = 0,
    CORE_ACCEPT,
    CORE_WRITE,
    CORE_READ,
    BRPC_WRITE,

    // count the number of ProfilingTPId
    UBSOCKET_PROF_COUNT,
};

class Profiling {
public:
    static int Init(uint32_t tracepoint_count, const char *dumpPath, uint16_t dumpIntervaMin)
    {
        ubsocket_prof_option_t option{};
        option.tracepoint_count = tracepoint_count;
        option.enable_dump = 1;
        option.dump_file_path = dumpPath;
        option.dump_interval_min = dumpIntervaMin;
        return ubsocket_prof_init(&option);
    }

    static int Uninit()
    {
        return ubsocket_prof_uninit();
    }

    static int Combine(std::string &out_str)
    {
        char *out_buf = nullptr;
        int len = ubsocket_prof_combind(&out_buf);
        if (len < 0 || out_buf == nullptr) {
            return UBS_ERROR;
        }
        out_str = std::string(out_buf, len);
        free(out_buf);
        return UBS_OK;
    }

    static void Reset()
    {
        ubsocket_prof_reset();
    }

public:
    /* disable constructor */
    Profiling() = delete;
};
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_COMMON_PROFILING_H
