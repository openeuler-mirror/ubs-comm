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

#include "ub_urma_wrapper_ctx.h"

namespace ock {
namespace hcom {

UResult UBContext::Create(const std::string &name, UBContext *&ctx)
{
    auto tmpCtx = new (std::nothrow) UBContext(name);
    if (tmpCtx == nullptr) {
        return UB_NEW_OBJECT_FAILED;
    }

    ctx = tmpCtx;
    return UB_OK;
}

UResult UBContext::Initialize(uint8_t &bandWidth, uint32_t ubPriority, UBSHcomUbcMode ubcMode)
{
    if (mUrmaContext != nullptr) {
        NN_LOG_INFO("UBContext " << mName << " already initialized");
        return UB_OK;
    }

    UResult ret = UB_OK;
    mDevAttr = reinterpret_cast<urma_device_attr_t *>(malloc(sizeof(urma_device_attr_t)));
    if (mDevAttr == nullptr) {
        NN_LOG_ERROR("Failed to malloc for urma device attr");
        return UB_MEMORY_ALLOCATE_FAILED;
    }
    ret = UBDeviceHelper::Initialize(mDevAttr, mUrmaContext, mBestEid);
    if (ret != 0) {
        NN_LOG_ERROR("Failed to initialize urma device");
        free(mDevAttr);
        mDevAttr = nullptr;
        return ret;
    }
    int tmpMaxSge = std::min(mDevAttr->dev_cap.max_jfs_sge, mDevAttr->dev_cap.max_jfr_sge);
    mMaxSge = tmpMaxSge < mMaxSge ? tmpMaxSge : mMaxSge;

    NN_LOG_INFO("Device info: max_qp " << mDevAttr->dev_cap.max_jetty << " ,max_qp_wr " <<
        mDevAttr->dev_cap.max_jfs_depth << " ,max_sge " << tmpMaxSge << " ,adapter max_cqe " << mMaxSge <<
        " ,max_cq " << mDevAttr->dev_cap.max_jfc << " ,max_cqe " << mDevAttr->dev_cap.max_jfc_depth);

    mMaxJfr = mDevAttr->dev_cap.max_jfr_depth;
    mMaxJfs = mDevAttr->dev_cap.max_jfs_depth;

    // get ctp and rtp default SL priority
    union urma_tp_type_en tp_type_ctp {};
    union urma_tp_type_en tp_type_rtp {};
    tp_type_ctp.bs.ctp = 1;
    tp_type_rtp.bs.rtp = 1;

    mCtpPri = GetPriByTpType(tp_type_ctp);
    if (mCtpPri == -1) {
        NN_LOG_ERROR("Failed to get priority by ctp type");
        return UB_ERROR;
    }
    mRtpPri = GetPriByTpType(tp_type_rtp);
    if (mRtpPri == -1) {
        NN_LOG_ERROR("Failed to get priority by rtp type");
        return UB_ERROR;
    }

    if (ubcMode == UBSHcomUbcMode::HighBandwidth) {
        if (!CheckPriByTpType(ubPriority, tp_type_ctp)) {
            NN_LOG_ERROR("UbPriority " << ubPriority <<
                " is invalid, please set priority with ctp type when ubc mode is high bandwidth");
            return UB_ERROR;
        }
        // if user set priority, use user set priority, else use ctp default priority
        if (ubPriority != UINT32_MAX) {
            mCtpPri = ubPriority;
        }
    }
    if (ubcMode == UBSHcomUbcMode::LowLatency) {
        if (!CheckPriByTpType(ubPriority, tp_type_rtp)) {
            NN_LOG_ERROR("UbPriority " << ubPriority <<
                " is invalid, please set priority with rtp type when ubc mode is low latency");
            return UB_ERROR;
        }
        // if user set priority, use user set priority, else use rtp default priority
        if (ubPriority != UINT32_MAX) {
            mRtpPri = ubPriority;
        }
    }

    bandWidth = mBestEid.bandWidth;
    return UB_OK;
}

UResult UBContext::UnInitialize()
{
    if (mUrmaContext != nullptr) {
        int res = 0;
        if ((res = HcomUrma::DeleteContext(mUrmaContext)) != 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_WARN("Unable to delete UB Context " << res << ", as errno " <<
                NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
        }
        mUrmaContext = nullptr;
    }

    if (mDevAttr != nullptr) {
        free(mDevAttr);
        mDevAttr = nullptr;
    }
    UBDeviceHelper::UnInitialize();
    return UB_OK;
}
} // namespace hcom
}
#endif