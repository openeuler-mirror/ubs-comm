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

#include <functional>
#include <fstream>
#include <memory>
#include <unistd.h>
#include <utility>
#include "htracer_service.h"
#include "htracer_msg.h"
#include "htracer_service_helper.h"
#include "htracer_utils.h"
#include "htracer_manager.h"

namespace ock {
namespace hcom {

// todo 支持命令修改
#ifdef HTRACER_DUMP_ENABLED
#define TRACE_DUMP_PERIOD 5
bool HTracerService::mDumpEnable = true;
#else
#define TRACE_DUMP_PERIOD 60
bool HTracerService::mDumpEnable = false;
#endif
int HTracerService::mDumpPeriod = TRACE_DUMP_PERIOD;
std::string HTracerService::dumpDir = "/tmp/htrace";

int32_t HTracerService::StartUp(const std::string &serverName)
{
    if (mRpcServer != nullptr) {
        return SER_OK;
    }

    std::unique_ptr<RpcServer> mRpcServerTmp(new RpcServer());      // compatible with c++11
    mRpcServer = std::move(mRpcServerTmp);
    if (mRpcServer == nullptr) {
        NN_LOG_WARN("[HTRACER] failed to create rpc server");
        return SER_ERROR;
    }

    mRpcServer->RegisterRequestHandler(
        std::bind(&HTracerService::HandleRequest, this, std::placeholders::_1, std::placeholders::_2));

    mRpcServer->RegisterSentResponse(
        std::bind(&HTracerService::SentResponse, this, std::placeholders::_1, std::placeholders::_2));

    if (mRpcServer->Start(serverName) != SER_OK) {
        NN_LOG_WARN("[HTRACER] failed to start rpc server");
        return SER_ERROR;
    }

    int32_t ret = HTracerUtils::CreateDirectory(dumpDir);
    if (ret != SER_OK) {
        NN_LOG_WARN("[HTRACER] prepare dump dir failed, disable dump feature!");
        mDumpEnable = false;
    }

    mIsRunning = true;
    return SER_OK;
}

void HTracerService::ShutDown()
{
    mIsRunning = false;
    if (mRpcServer != nullptr) {
        mRpcServer->Stop();
    }
}

SerCode HTracerService::HandleRequest(const Message &request, Message &response)
{
    auto header = request.GetHeader();
    if (header == nullptr) {
        NN_LOG_WARN("[HTRACER] header is nullptr");
        return SER_ERROR;
    }

    switch (header->opcode) {
        case TRACE_OP_QUERY: {
            auto queryRequest = reinterpret_cast<const QueryTraceInfoRequest *>(request.GetData());
            auto tTranceInfos = TracerServiceHelper::GetTraceInfos(queryRequest->serviceId, queryRequest->quantile,
                TraceManager::IsLatencyQuantileEnable());
            SerCode ret = QueryTraceInfoResponse::BuildMessage(tTranceInfos, response);
            if (ret != SER_OK) {
                NN_LOG_WARN("[HTRACER] failed to build response message");
                return ret;
            }
            break;
        }
        case TRACE_OP_RESET: {
            TracerServiceHelper::ResetTraceInfos();
            SerCode ret = ResetTraceInfoResponse::BuildMessage(response);
            if (ret != SER_OK) {
                NN_LOG_WARN("[HTRACER] failed to build response message");
                return ret;
            }
            break;
        }
        case TRACE_OP_ENABLE_TRACE: {
            auto enableRequest = reinterpret_cast<const EnableTraceRequest *>(request.GetData());
            TraceManager::SetEnable(enableRequest->enable);
            TraceManager::SetLatencyQuantileEnable(enableRequest->enableTp);
            std::string logPath(enableRequest->logPath);
            TraceManager::SetEnableLog(enableRequest->enableLog, logPath);
            SerCode ret = EnableTraceResponse::BuildMessage(response);
            if (ret != SER_OK) {
                NN_LOG_WARN("[HTRACER] failed to build response message");
                return ret;
            }
            break;
        }
        default:
            break;
    }
    return SER_OK;
}

void HTracerService::SentResponse(SerCode result, Message &response)
{
    void *data = response.GetData();
    if (data != nullptr) {
        free(data);
        data = nullptr;
    }
}
}
}