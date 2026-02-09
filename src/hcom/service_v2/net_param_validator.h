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
#ifndef HCOM_PARAM_VAL_H
#define HCOM_PARAM_VAL_H

#include <cstdint>
#include <string>
#include <vector>
#include "hcom.h"
#include "hcom_def.h"
#include "hcom_log.h"
#include "hcom_num_def.h"
#include "hcom_service.h"
#include "net_common.h"
#include "service_common.h"

namespace ock {
namespace hcom {

#define VALIDATE_PARAM_RET(funcName, ...)                       \
    do {                                                        \
        if (NN_UNLIKELY(!funcName##Check(__VA_ARGS__))) {       \
            NN_LOG_ERROR("Invalid parameter!");                 \
            return SER_INVALID_PARAM;                           \
        }                                                       \
    } while (0)                                                 \

#define VALIDATE_PARAM(funcName, ...) (funcName##Check(__VA_ARGS__))

inline bool BindCheck(const std::string &url, const UBSHcomServiceNewChannelHandler &handler)
{
    if (NN_UNLIKELY(url.empty())) {
        NN_LOG_ERROR("Invalid url: " << url);
        return false;
    }
    if (NN_UNLIKELY(handler == nullptr)) {
        NN_LOG_ERROR("UBSHcomServiceNewChannelHandler is nullptr");
        return false;
    }
    return true;
}

inline bool TlsOptionsCheck(const UBSHcomTlsOptions &opt)
{
    if (NN_UNLIKELY(!opt.enableTls)) {
        return true;
    }

    if (NN_UNLIKELY(opt.caCb == nullptr)) {
        NN_LOG_ERROR("UBSHcomTLSCaCallback of UBSHcomTlsOptions is nullptr");
        return false;
    }

    if (NN_UNLIKELY(opt.cfCb == nullptr)) {
        NN_LOG_ERROR("UBSHcomTLSCertificationCallback of UBSHcomTlsOptions is nullptr");
        return false;
    }

    if (NN_UNLIKELY(opt.pkCb == nullptr)) {
        NN_LOG_ERROR("UBSHcomTLSPrivateKeyCallback of UBSHcomTlsOptions is nullptr");
        return false;
    }

    return true;
}

inline bool SerConnInfoCheck(const SerConnInfo &connInfo)
{
    if (NN_UNLIKELY(connInfo.totalLinkCount == NN_NO0 || connInfo.totalLinkCount > NN_NO64)) {
        NN_LOG_ERROR("Invalid total link count " << static_cast<int32_t>(connInfo.totalLinkCount) <<
            " for connect, make sure range in [1, 64]");
        return false;
    }

    if (NN_UNLIKELY(connInfo.options.linkCount == NN_NO0 || connInfo.options.linkCount > NN_NO16)) {
        NN_LOG_ERROR("Invalid link count " << connInfo.options.linkCount <<
            " for connect, make sure range in [1, 16]");
        return false;
    }

    if (NN_UNLIKELY(connInfo.index >= connInfo.totalLinkCount)) {
        NN_LOG_ERROR("Invalid conn index " << connInfo.index << ", total ep size " << connInfo.totalLinkCount <<
            " for connecting");
        return false;
    }

    return true;
}

inline bool ConnectOptionsCheck(const UBSHcomConnectOptions &opt)
{
    if (NN_UNLIKELY(opt.linkCount == NN_NO0 || opt.linkCount > NN_NO16)) {
        NN_LOG_ERROR("Invalid link count " << static_cast<int32_t>(opt.linkCount) <<
            " for connect, make sure range in [1, 16]");
        return false;
    }
    if (NN_UNLIKELY(opt.mode != UBSHcomClientPollingMode::WORKER_POLL &&
                    opt.mode != UBSHcomClientPollingMode::SELF_POLL_BUSY &&
                    opt.mode != UBSHcomClientPollingMode::SELF_POLL_EVENT)) {
        NN_LOG_ERROR("Invalid polling mode " << static_cast<int32_t>(opt.mode));
        return false;
    }
    return true;
}

inline bool RequestCheck(const UBSHcomRequest &req)
{
    if (NN_UNLIKELY(req.address == nullptr)) {
        NN_LOG_ERROR("Invalid request as address of request is nullptr");
        return false;
    }
    if (NN_UNLIKELY(req.size == NN_NO0)) {
        NN_LOG_ERROR("Invalid request as size of request is zeri");
        return false;
    }
    return true;
}

inline bool OneSideRequestCheck(const UBSHcomOneSideRequest &req)
{
    if (NN_UNLIKELY(req.size == NN_NO0)) {
        NN_LOG_ERROR("NetServiceRequest.size is invalid");
        return false;
    }
    if (NN_UNLIKELY(req.lAddress == 0)) {
        NN_LOG_ERROR("NetServiceRequest.lAddress is invalid");
        return false;
    }
    return true;
}

inline bool OneSideSglRequestCheck(const UBSHcomOneSideSglRequest &req)
{
    if (NN_UNLIKELY(req.iov == nullptr || req.iovCount == 0 || req.iovCount > NET_SGE_MAX_IOV)) {
        NN_LOG_ERROR("NetServiceRequest.iov or iovCount " << req.iovCount << " is invalid");
        return false;
    }
    
    for (uint32_t i = 0; i < req.iovCount; i++) {
        if (!OneSideRequestCheck(req.iov[i])) {
            return false;
        }
    }
    return true;
}

inline bool ReplyCheck(const UBSHcomReplyContext &ctx, const UBSHcomRequest &req, bool selfPoll)
{
    if (NN_UNLIKELY(selfPoll)) {
        NN_LOG_ERROR("Self poll is not support reply");
        return false;
    }

    if (NN_UNLIKELY(ctx.rspCtx == 0)) {
        NN_LOG_ERROR("Invalid reply param as rspCtx is 0");
        return false;
    }
    if (NN_UNLIKELY(req.address == nullptr)) {
        NN_LOG_ERROR("Invalid reply param as address of req is nullptr");
        return false;
    }
    if (NN_UNLIKELY(req.size <= 0)) {
        NN_LOG_ERROR("Invalid reply param as size of req is negative");
        return false;
    }
    return true;
}
}
}
#endif // HCOM_PARAM_VAL_H