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
#include "rdma_verbs_wrapper_ctx.h"

namespace ock {
namespace hcom {

RResult RDMAContext::Create(const std::string &name, bool useDevX, const RDMAGId &gid, RDMAContext *&ctx)
{
    auto tmpCtx = new (std::nothrow) RDMAContext(name, useDevX, gid);
    if (tmpCtx == nullptr) {
        return RR_NEW_OBJECT_FAILED;
    }

    ctx = tmpCtx;
    return RR_OK;
}

RResult RDMAContext::Initialize()
{
    if (mContext != nullptr) {
        NN_LOG_INFO("RDMAContext " << mName << " already initialized");
        return RR_OK;
    }

    HcomIbv::ForkInit();

    struct ibv_device **devList = nullptr;
    int devCount = 0;
    devList = HcomIbv::GetDevList(&devCount);
    if (devList == nullptr) {
        NN_LOG_ERROR("Failed to call get ibv device list for RDMAContext " << mName << ", errno " << errno);
        return RR_DEVICE_FAILED_OPEN;
    }

    if (mDevIndex >= devCount) {
        NN_LOG_ERROR("Invalid device index is set for RDMAContext " << mName);
        HcomIbv::FreeDevList(devList);

        return RR_DEVICE_INDEX_OVERFLOW;
    }

    ibv_context *tmpCtx = nullptr;
    if ((tmpCtx = HcomIbv::OpenDevice(devList[mDevIndex])) == nullptr) {
        NN_LOG_ERROR("Invalid device index is set for RDMAContext " << mName << ", errno " << errno);
        HcomIbv::FreeDevList(devList);
        return RR_DEVICE_OPEN_FAILED;
    }

    struct ibv_device_attr attr {};
    if (HcomIbv::QueryDevice(tmpCtx, &attr) != 0) {
        NN_LOG_ERROR("Failed to query device info");
        HcomIbv::CloseDev(tmpCtx);
        HcomIbv::FreeDevList(devList);
        return RR_DEVICE_OPEN_FAILED;
    }

    mMaxSge = attr.max_sge < mMaxSge ? attr.max_sge : mMaxSge;
    NN_LOG_INFO("Device info: fw_ver " << attr.fw_ver << " ,max_qp " << attr.max_qp << " ,max_qp_wr " <<
        attr.max_qp_wr << " ,max_sge " << attr.max_sge << " ,adapter max_cqe " << mMaxSge << " ,max_cq " <<
        attr.max_cq << " ,max_cqe " << attr.max_cqe);

    ibv_pd *tmpPD = nullptr;
    if ((tmpPD = HcomIbv::AllocPd(tmpCtx)) == nullptr) {
        NN_LOG_ERROR("Invalid device index is set for RDMAContext " << mName << ", errno " << errno);
        HcomIbv::CloseDev(tmpCtx);
        HcomIbv::FreeDevList(devList);
        return RR_DEVICE_OPEN_FAILED;
    }

    if (HcomIbv::QueryPort(tmpCtx, mPortNumber, &mPortAttr) != 0 || mPortAttr.state != IBV_PORT_ACTIVE) {
        NN_LOG_ERROR("Failed to query port for RDMAContext " << mName << ", errno " << errno <<
            " or port state invalid " << mPortAttr.state);
        HcomIbv::CloseDev(tmpCtx);
        HcomIbv::FreeDevList(devList);
        HcomIbv::DeallocPd(tmpPD);
        return RR_DEVICE_OPEN_FAILED;
    }

    HcomIbv::FreeDevList(devList);

    mProtectDomain = tmpPD;
    mContext = tmpCtx;
    return RR_OK;
}

void RDMAContext::UpdateGid(const std::string &matchIp)
{
    auto ret = RDMADeviceHelper::Update();
    if (NN_UNLIKELY(ret != RR_OK)) {
        return;
    }

    RDMAGId tmpGid {};
    if ((RDMADeviceHelper::GetDeviceByIp(matchIp, tmpGid)) != 0) {
        NN_LOG_ERROR("Failed to get device by ip " << matchIp);
        return;
    }

    NN_LOG_INFO("gid found devIndex " << tmpGid.devIndex << ", gidIndex " << tmpGid.gid << ", RoCEVersion " <<
        RDMADeviceHelper::RoCEVersionToStr(tmpGid.RoCEVersion));
    mBestGid = tmpGid;
}

RResult RDMAContext::UnInitialize()
{
    if (mContext == nullptr) {
        return RR_OK;
    }

    HcomIbv::DeallocPd(mProtectDomain);
    HcomIbv::CloseDev(mContext);
    mProtectDomain = nullptr;
    mContext = nullptr;

    return RR_OK;
}
}
}
#endif