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
#include "ubsocket_prof.h"
#include <string>
#include "common/ubsocket_common_includes.h"
#include "profiling/impl/ubsocket_prof_tracer.h"

using namespace ock::ubs::profiling;

int ubsocket_prof_enabled = 0;

int ubsocket_prof_init(ubsocket_prof_option_t *option)
{
    if (option == nullptr) {
        UBS_VLOG_ERR("Profiling init failed, as param 'option' is null");
        return -1;
    }

    if (option->tracepoint_count == 0) {
        UBS_VLOG_ERR("Profiling init failed, as param 'option.tracepoint_count' is 0");
        return -1;
    }

    if (option->enable_dump != 0) {
        if (option->dump_file_path == nullptr) {
            UBS_VLOG_ERR("Profiling init failed, as param 'option.dump_file_path' is null");
            return -1;
        }
    }

    TracerOptions options{};
    options.tracepoint_count = option->tracepoint_count;
    options.enable_dump = (option->enable_dump != 0);
    options.dumpPath = option->dump_file_path != nullptr ? std::string(option->dump_file_path) : "";
    options.dumpIntervalMin = option->dump_interval_min;
    auto res = Tracer::Instance().Init(options);
    if (res == 0) {
        ubsocket_prof_enabled = 1;
    }
    return res;
}

int ubsocket_prof_uninit()
{
    ubsocket_prof_enabled = 0;
    Tracer::Instance().UnInit();
    return 0;
}

int ubsocket_prof_record(uint32_t tracepoint_id, const char *tracepoint_name, uint64_t timestamp, bool good)
{
    return Tracer::Instance().Record(tracepoint_id, tracepoint_name, timestamp, good);
}
