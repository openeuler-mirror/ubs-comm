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

UResult UBContext::Initialize(uint8_t &bandWidth)
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
    ret = UBDeviceHelper::Initialize(mDevAttr, bandWidth);
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

    mUrmaContext = tmpCtx;
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

void UBContext::UpdateGid(const std::string &matchIp)
{
    auto ret = UBDeviceHelper::Update();
    if (NN_UNLIKELY(ret != UB_OK)) {
        NN_LOG_ERROR("Failed to do update");
        return;
    }

    UBEId tmpEid{};
    if ((UBDeviceHelper::GetDeviceByIp(matchIp, tmpEid)) != 0) {
        NN_LOG_ERROR("Failed to get device by ip " << matchIp);
        return;
    }

    NN_LOG_INFO("gid found devIndex " << tmpEid.devIndex << ", gidIndex " << tmpEid.eidIndex);
    mBestEid = tmpEid;
}
} // namespace hcom
}
#endif