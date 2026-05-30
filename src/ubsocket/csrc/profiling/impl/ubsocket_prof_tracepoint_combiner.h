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
#ifndef UBS_COMM_UBSOCKET_PROF_TRACEPOINT_COMBINER_H
#define UBS_COMM_UBSOCKET_PROF_TRACEPOINT_COMBINER_H

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "ubsocket_prof_tracepoint.h"

namespace ock {
namespace ubs {
namespace profiling {
class TraceCombiner {
public:
    int CombinerTracePoint(Tracepoint &outTracePoint, const Tracepoint &pointB);

    void OutputTracePointStats(std::ostringstream &oss, const Tracepoint &totalTracePoint);

    DEFINE_REF_OPERATION_FUNC
private:
    std::mutex mutex_; /* mutex for compute tracePoint data */
    DECLARE_REF_COUNT_VARIABLE;
};
using TraceCombinerPtr = Ref<TraceCombiner>;
} // namespace profiling
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_PROF_TRACEPOINT_COMBINER_H
