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
#include "ubsocket_prof_tracepoint_dumper_ext.h"
#include "ubsocket_prof_tracer_ext.h"

namespace ock {
namespace ubs {
namespace profiling {
void DumpThreadExt::DumpDataExt() noexcept
{
    std::ostringstream oss;
    WriteDumpTitleExt(oss);
    TracerExt::Instance().CombineExt(oss);
    if (WriteDumpDataExt(oss) != 0) {
        UBS_VLOG_WARN("Profiling data write skipped, please check path and permissions.\n");
        return;
    }
    UBS_VLOG_DEBUG("Dump thread ext success combiner and dump data");
}
} // namespace profiling
} // namespace ubs
} // namespace ock
