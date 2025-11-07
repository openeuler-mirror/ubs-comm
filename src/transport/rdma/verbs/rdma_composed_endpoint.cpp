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
#ifdef RDMA_BUILD_ENABLED
#include "rdma_composed_endpoint.h"
namespace ock {
namespace hcom {
RResult RDMAAsyncEndPoint::Create(const std::string &name, RDMAWorker *worker, RDMAAsyncEndPoint *&ep)
{
    if (worker == nullptr || name.empty()) {
        return RR_PARAM_INVALID;
    }

    RDMAQp *tmpQP = nullptr;
    RResult result = worker->CreateQP(tmpQP);
    if (result != RR_OK) {
        return result;
    }

    auto tmpEP = new (std::nothrow) RDMAAsyncEndPoint(name, worker, tmpQP);
    if (tmpEP == nullptr) {
        delete tmpQP;
        NN_LOG_ERROR("Failed to create RDMAAsyncEndPoint, probably out of memory");
        return RR_NEW_OBJECT_FAILED;
    }

    tmpQP->Name(name);
    ep = tmpEP;
    return RR_OK;
}

RResult RDMASyncEndpoint::Create(const std::string &name, RDMAContext *ctx, RDMAPollingMode pollMode,
    uint32_t rdmaOpCtxPoolSize, const QpOptions &options, RDMASyncEndpoint *&ep)
{
    if (ctx == nullptr || name.empty()) {
        return RR_PARAM_INVALID;
    }

    auto tmpCQ = new (std::nothrow) RDMACq(name, ctx, pollMode == EVENT_POLLING);
    if (tmpCQ == nullptr) {
        NN_LOG_ERROR("Failed to create RDMACq, probably out of memory");
        return RR_NEW_OBJECT_FAILED;
    }

    auto tmpQP = new (std::nothrow) RDMAQp(name, RDMAQp::NewId(), ctx, tmpCQ, options);
    if (tmpQP == nullptr) {
        NN_LOG_ERROR("Failed to create RDMAQp, probably out of memory");
        delete tmpCQ;
        return RR_NEW_OBJECT_FAILED;
    }

    auto tmpEP = new (std::nothrow) RDMASyncEndpoint(name, ctx, pollMode, tmpCQ, tmpQP, rdmaOpCtxPoolSize);
    if (tmpEP == nullptr) {
        NN_LOG_ERROR("Failed to create RDMASyncClientEndPoint, probably out of memory");
        delete tmpCQ;
        delete tmpQP;
        return RR_NEW_OBJECT_FAILED;
    }

    ep = tmpEP;
    return RR_OK;
}
}
}
#endif
