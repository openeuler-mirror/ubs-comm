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

#ifndef HTRACE_CLIENT_H
#define HTRACE_CLIENT_H

#include <string>
#include <cstring>
#include <vector>
#include "hcom/hcom_err.h"
#include "htracer_msg.h"
#include "rpc_client.h"

#define MAX_SERVICE_NUM (256)
#define MAX_INNER_ID_NUM (2)

class HTracerClient {
public:
    HTracerClient();

    ~HTracerClient();

    SerCode Query(uint16_t serviceId, double quantile, std::vector<TTraceInfo> &traceInfos, pid_t &pid);

    SerCode Reset();

    SerCode EnableTrace(const HandlerConfPara &confPara);

private:
    RpcClient mClient;
    char *mQueryRecvBuffer = nullptr;
    const static uint32_t mQueryRecvBufferSize =
        sizeof(QueryTraceInfoResponse) + sizeof(TTraceInfo) * MAX_INNER_ID_NUM * MAX_SERVICE_NUM;
};


#endif // HTRACE_CLIENT_H