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
#include  <string>
#include "common/ubsocket_common_includes.h"
#include "profiling/impl/ubsocket_prof_tracer.h"
#include "ubsocket_prof.h"

using namespace ock::ubs::profiling;

int ubsocket_prof_enabled = 0;

int ubsocket_prof_init(uint32_t tracepoint_count, const char* dumpPath, uint16_t dumpIntervalMin)
{
    TracerOptions options{};
    options.tracepoint_count = tracepoint_count;
    options.dumpPath = std::string(dumpPath);
    options.dumpIntervalMin = dumpIntervalMin;
    int res = Tracer::Instance().Init(options);
    if (!res) {
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

int ubsocket_prof_record(uint32_t tracepoint_id, const char* tracepoint_name, uint64_t timestamp, bool good)
{
    std::string localTracepointName = std::string(tracepoint_name);
    return Tracer::Instance().Record(tracepoint_id, localTracepointName, timestamp, good);
}
