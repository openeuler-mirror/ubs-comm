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

#include "htracer_client.h"
#include "rpc_client.h"
#include "htracer_msg.h"

HTracerClient::HTracerClient()
{
    mQueryRecvBuffer = new char[mQueryRecvBufferSize];
    if (mQueryRecvBuffer == nullptr) {
        LOG_ERR("failed to malloc query recv buffer");
        return;
    }
}

HTracerClient::~HTracerClient()
{
    if (mQueryRecvBuffer != nullptr) {
        delete[] mQueryRecvBuffer;
    }
}

SerCode HTracerClient::Query(uint16_t serviceId, double quantile, std::vector<TTraceInfo> &traceInfos, pid_t &pid)
{
    QueryTraceInfoRequest queryRequest;
    queryRequest.serviceId = serviceId;
    queryRequest.quantile = quantile;
    Message request(&queryRequest, sizeof(queryRequest));
    Message response(mQueryRecvBuffer, mQueryRecvBufferSize);
    if (mClient.SyncCall(request, response) != SER_OK) {
        return SER_ERROR;
    }

    auto queryResponse = reinterpret_cast<QueryTraceInfoResponse *>(mQueryRecvBuffer);
    for (uint32_t i = 0; i < queryResponse->traceInfoNum; ++i) {
        auto &traceInfo = queryResponse->traceInfo[i];
        traceInfos.push_back(traceInfo);
    }
    pid = queryResponse->pid;
    return SER_OK;
}

SerCode HTracerClient::Reset()
{
    ResetTraceInfoRequest resetRequest;
    ResetTraceInfoResponse resetResponse;
    Message request(&resetRequest, sizeof(resetRequest));
    Message response(&resetResponse, sizeof(resetResponse));
    if (mClient.SyncCall(request, response) != SER_OK) {
        return SER_ERROR;
    }
    return SER_OK;
}

SerCode HTracerClient::EnableTrace(const HandlerConfPara &confPara)
{
    EnableTraceRequest enableRequest;
    enableRequest.enable = confPara.enable;
    enableRequest.enableTp = confPara.enableTp;
    enableRequest.enableLog = confPara.enableLog;
    // HandlerConfPara and EnableTraceRequest logPath is same length.
    if (strcpy_s(enableRequest.logPath, sizeof(enableRequest.logPath), confPara.logPath) != 0) {
        return SER_ERROR;
    }
    EnableTraceResponse enableResponse;
    Message request(&enableRequest, sizeof(enableRequest));
    Message response(&enableResponse, sizeof(enableResponse));
    if (mClient.SyncCall(request, response) != SER_OK) {
        return SER_ERROR;
    }
    return SER_OK;
}