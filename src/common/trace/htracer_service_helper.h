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

#ifndef HTRACE_HELPER_H
#define HTRACE_HELPER_H

#include <vector>
#include "htracer_manager.h"
#include "htracer_msg.h"

namespace ock {
namespace hcom {

class TracerServiceHelper {
public:
    static std::vector<TTraceInfo> GetTraceInfos(uint16_t serviceId, double quantile, bool enableTp)
    {
        std::vector<TTraceInfo> retTraceInfos;
        auto traceManager = TraceManager::Instance();
        if (serviceId == INVALID_SERVICE_ID) {
            for (int i = 0; i < MAX_SERVICE_NUM; ++i) {
                for (int j = 0; j < MAX_INNER_ID_NUM; ++j) {
                    auto &traceInfo = traceManager[i][j];
                    if (traceInfo.Valid()) {
                        retTraceInfos.emplace_back(TTraceInfo(traceManager[i][j], quantile, enableTp));
                    }
                }
            }
            return retTraceInfos;
        }

        if (serviceId > MAX_SERVICE_NUM) {
            return retTraceInfos;
        }

        for (int i = 0; i < MAX_INNER_ID_NUM; ++i) {
            auto &traceInfo = traceManager[serviceId][i];
            if (traceInfo.Valid()) {
                retTraceInfos.emplace_back(TTraceInfo(traceInfo, quantile, enableTp));
            }
        }
        return retTraceInfos;
    }

    static void ResetTraceInfos()
    {
        std::vector<TTraceInfo> retTraceInfos;
        auto traceManager = TraceManager::Instance();
        for (int i = 0; i < MAX_SERVICE_NUM; ++i) {
            for (int j = 0; j < MAX_INNER_ID_NUM; ++j) {
                auto &traceInfo = traceManager[i][j];
                if (traceInfo.Valid()) {
                    traceManager[i][j].Reset();
                }
            }
        }
    }
};

}
}

#endif // HTRACE_HELPER_H
