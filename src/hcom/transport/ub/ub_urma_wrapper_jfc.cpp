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
#ifdef UB_BUILD_ENABLED

#include "ub_urma_wrapper_jfc.h"

namespace ock {
namespace hcom {

UResult UBJfc::CreatePollingCq()
{
    urma_jfc_cfg_t jfc_cfg{};
    jfc_cfg.depth = mJfcCount;
    jfc_cfg.flag.value = 0;
    jfc_cfg.jfce = nullptr;
    jfc_cfg.user_ctx = mWork;

    urma_jfc_t *tmpJfc = HcomUrma::CreateJfc(mUBContext->mUrmaContext, &jfc_cfg);
    if (tmpJfc == nullptr) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create completion queue for UBJfc " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return UB_NEW_OBJECT_FAILED;
    }

    mUrmaJfc = tmpJfc;
    return UB_OK;
}

UResult UBJfc::CreateEventCq()
{
    // create jfce
    urma_jfce_t *tmpJfce = HcomUrma::CreateJfce(mUBContext->mUrmaContext);
    if (tmpJfce == nullptr) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create JFCE for UBJfc " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return UB_NEW_OBJECT_FAILED;
    }

    // create jfc
    urma_jfc_cfg_t jfc_cfg{};
    jfc_cfg.depth = mJfcCount;
    jfc_cfg.flag.value = 0;
    jfc_cfg.jfce = tmpJfce;
    jfc_cfg.user_ctx = mWork;

    urma_jfc_t *tmpJfc = HcomUrma::CreateJfc(mUBContext->mUrmaContext, &jfc_cfg);
    if (tmpJfc == nullptr) {
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create completion queue for UBJfc " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        HcomUrma::DeleteJfce(tmpJfce);
        return UB_NEW_OBJECT_FAILED;
    }

    if (HcomUrma::RearmJfc(tmpJfc, 0) != 0) {
        HcomUrma::DeleteJfc(tmpJfc);
        HcomUrma::DeleteJfce(tmpJfce);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to create completion queue for UBJfc " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return UB_NEW_OBJECT_FAILED;
    }

    int flags = fcntl(tmpJfce->fd, F_GETFL);
    if (fcntl(tmpJfce->fd, F_SETFL, static_cast<uint32_t>(flags) | O_NONBLOCK) < 0) {
        HcomUrma::DeleteJfc(tmpJfc);
        HcomUrma::DeleteJfce(tmpJfce);
        char buf[NET_STR_ERROR_BUF_SIZE] = {0};
        NN_LOG_ERROR("Failed to set no blocking for UBJfc " << mName << ", error " <<
            NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        return UB_NEW_OBJECT_FAILED;
    }

    mUrmaJfcEvent = tmpJfce;
    mUrmaJfc = tmpJfc;
    return UB_OK;
}

UResult UBJfc::Initialize()
{
    NN_LOG_TRACE_INFO("UBJfc::Initialize");
    if (mUrmaJfc != nullptr) {
        return UB_OK;
    }

    if (mUBContext == nullptr || mUBContext->mUrmaContext == nullptr) {
        NN_LOG_ERROR("Failed to initialize UBJfc as ub context is null");
        return UB_PARAM_INVALID;
    }
    if (mCreateCompletionChannel) {
        return CreateEventCq();
    } else {
        return CreatePollingCq();
    }

    NN_LOG_TRACE_INFO("UBJfc::Initialized");
    return UB_OK;
}

UResult UBJfc::UnInitialize()
{
    int res = 0;
    if (mUrmaJfc != nullptr) {
        if ((res = HcomUrma::DeleteJfc(mUrmaJfc)) != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_WARN("Unable to delete jfc " << res << ", as " <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
                return UB_OK;
        }
        mUrmaJfc = nullptr;
    }

    if (mUrmaJfcEvent != nullptr) {
        HcomUrma::DeleteJfce(mUrmaJfcEvent);
        mUrmaJfcEvent = nullptr;
    }

    if (mUBContext != nullptr) {
        mUBContext->DecreaseRef();
        mUBContext = nullptr;
    }
    return UB_OK;
}

UResult UBJfc::ProgressV(urma_cr_t *cr, uint32_t &countInOut)
{
    if (NN_UNLIKELY(mUrmaJfc == nullptr || cr == nullptr)) {
        return UB_CQ_NOT_INITIALIZED;
    }

    uint16_t times = 0;

    while (true) {
        auto n = HcomUrma::PollJfc(mUrmaJfc, countInOut, cr);
        if (NN_UNLIKELY(n < 0)) {
            NN_LOG_ERROR("Poll jfc failed in UBJfc " << mName << ", errno " << errno << " n = " << n);
            return UB_CQ_POLLING_FAILED;
        }
        if (n == 0) {
            times++;
            if (times < NN_NO10) {
                continue;
            }
        }

        countInOut = static_cast<uint32_t>(n);
        break;
    }

    return UB_OK;
}

UResult UBJfc::EventProgressV(urma_cr_t *cr, uint32_t &countInOut, int32_t timeoutInMs)
{
    if (NN_UNLIKELY(mUrmaJfc == nullptr || mUrmaJfcEvent == nullptr || cr == nullptr)) {
        return UB_CQ_NOT_INITIALIZED;
    }

    // wait request if n == 0
    urma_jfc_t *jfc = nullptr;

    // Wait for the completion event
    int result = HcomUrma::WaitJfc(mUrmaJfcEvent, 1, timeoutInMs, &jfc);
    if (result < 0) {
        NN_LOG_ERROR("urma_wait_jfc failed, jfc id: " << mUrmaJfc->jfc_id.id << ", errno " << errno);
        return UB_CQ_EVENT_GET_FAILED;
    }

    int cqeCnt = HcomUrma::PollJfc(mUrmaJfc, countInOut, cr);
    if (cqeCnt < 0) {
        NN_LOG_ERROR("Poll jfc failed in UBJfc " << mName << ", errno " << errno);
        return UB_CQ_POLLING_FAILED;
    }
    countInOut = static_cast<uint32_t>(cqeCnt);

    if (jfc != nullptr) {
        // Ack the event
        uint32_t ackCnt = 1;
        HcomUrma::AckJfc(&jfc, &ackCnt, 1);
    }

    // Request notification upon the next completion event
    if (HcomUrma::RearmJfc(mUrmaJfc, false) != 0) {
        NN_LOG_ERROR("Notify cq event failed in UBJfc " << mName << ", errno " << errno);
        return UB_CQ_EVENT_NOTIFY_FAILED;
    }

    return UB_OK;
}
} // namespace hcom
}
#endif