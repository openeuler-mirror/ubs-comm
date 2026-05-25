/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "umq_backend.h"
#include "umq_setting.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
std::mutex UmqBackend::UMQ_MUTEX;
bool UmqBackend::UMQ_INITED = false;

Result UmqBackend::Init() noexcept
{
    //UBS_VLOG_DEBUG("enter");
    Result ret = UBS_OK;
    std::lock_guard<std::mutex> guard(UMQ_MUTEX);
    if (UMQ_INITED) {
        //UBS_VLOG_DEBUG("umq already initialized");
        return UBS_OK;
    }

    /* initialize umq settting */
    ret = UmqSetting::Init();
    if (ret != UBS_OK) {
        UBS_VLOG_ERR("[UMQ_API] UmqSetting::Init() failed, ret: %d\n", ret);
        return ret;
    }

    /* init umq init config */
    umq_init_cfg_t umq_config;
    bzero(&umq_config, sizeof(umq_config));
    umq_config.feature = UMQ_FEATURE_API_PRO | UMQ_FEATURE_ENABLE_FLOW_CONTROL;
    umq_config.buf_mode = UMQ_BUF_SPLIT;
    umq_config.io_lock_free = true;
    umq_config.trans_info_num = 1;
    umq_config.flow_control.use_atomic_window = true;
    umq_config.flow_control.initial_credit = UmqSetting::UMQ_FC_DEFAULT_CREDIT;
    umq_config.flow_control.max_credits_request = UmqSetting::UMQ_FC_MAX_CREDIT;
    umq_config.flow_control.min_reserved_credit = UmqSetting::UMQ_FC_MIN_CREDIT;
    umq_config.block_cfg.small_block_size = UmqSetting::IO_BLOCK_TYPE;
    umq_config.trans_info[0].dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DUMMY;
    umq_config.trans_info[0].mem_cfg.total_size = UmqSetting::UMQ_MEM_POOL_INIT_SIZE_MB * IO_SIZE_MB;
    umq_config.trans_info[0].trans_mode = UmqSetting::UMQ_TRANS_MODE;
    umq_config.buf_pool_cfg.umq_buf_pool_max_size = UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB * IO_SIZE_MB;
    umq_config.buf_pool_cfg.tls_qbuf_pool_depth = UmqSetting::UMQ_BUF_POOL_DEPTH;
    umq_config.io_lock_free = false;

    /* init umq */
    ret = UmqApi::umq_init(&umq_config);
    if (ret != 0) {
        UBS_VLOG_ERR("[UMQ_API] umq_init() failed, ret: %d\n", ret);
        return UBS_ERROR;
    }

    switch (UmqSetting::UMQ_TRANS_MODE) {
        case UMQ_TRANS_MODE_IB:
            // ret = AddIbDev(umq_config.trans_info[0]);
            UBS_VLOG_ERR("Un-supported IB protocol.\n");
            break;
        case UMQ_TRANS_MODE_UB:
            ret = AddUbDev(umq_config.trans_info[0]);
            break;
        default:
            UBS_VLOG_ERR("Un-supported protocol.\n");
            return UBS_ERROR;
    }
    if (ret != 0) {
        UBS_VLOG_ERR("[UMQ_API] AddIbDev()/AddUbDev() failed, ret: %d\n", ret);
        return UBS_ERROR;
    }

    if (UmqSetting::UMQ_DEV_SCHEDULE_POLICY == dev_schedule_policy::CPU_AFFINITY ||
        UmqSetting::UMQ_DEV_SCHEDULE_POLICY == dev_schedule_policy::CPU_AFFINITY_PRIORITY) {
        UmqSetting::UMQ_PROCESS_SOCKET_ID = SocketConnHelper::GetCurrentProcessSocketId();
        UmqSetting::UMQ_ALL_SOCKET_IDS = SocketConnHelper::GetSocketIdsViaNumaSysfs();
        if (UmqSetting::UMQ_ALL_SOCKET_IDS.empty() || UmqSetting::UMQ_PROCESS_SOCKET_ID == -1) {
            UBS_VLOG_ERR("Failed get socket id in cpu affinity policy.\n");
            return UBS_ERROR;
        }
    }

    UMQ_INITED = true;

    //UBS_VLOG_DEBUG("leave, inited = %d", UMQ_INITED);
    return UBS_OK;
}

void UmqBackend::UnInit() noexcept
{
    UBS_VLOG_DEBUG("enter");

    std::lock_guard<std::mutex> guard(UMQ_MUTEX);
    if (!UMQ_INITED) {
        UBS_VLOG_DEBUG("umq not initialized");
        return;
    }

    UmqApi::umq_uninit();
    UMQ_INITED = false;

    UBS_VLOG_DEBUG("leave, inited = %d", UMQ_INITED);
}

Result UmqBackend::AddUbDev(umq_trans_info_t &trans_info)
{
    if (UmqSetting::UMQ_DEV_NAME.empty()) {
        if (FindDevName() != UBS_OK) {
            UBS_VLOG_ERR("[UMQ_API] Failed to find bonding dev, need active input.\n");
            return UBS_ERROR;
        }
    }
    if (UmqSetting::UMQ_DEV_NAME.length() >= DEV_NAME_STR_LEN_MAX) {
        UBS_VLOG_ERR("Device name too long.\n");
        return UBS_ERROR;
    }

    char dev_info[DEV_NAME_STR_LEN_MAX];
    strncpy(dev_info, UmqSetting::UMQ_DEV_NAME.c_str(), DEV_NAME_STR_LEN_MAX - 1);
    dev_info[DEV_NAME_STR_LEN_MAX - 1] = '\0';

    trans_info.mem_cfg.total_size = UmqSetting::UMQ_IO_TOTAL_SIZE_MB * IO_SIZE_MB;
    trans_info.trans_mode = UMQ_TRANS_MODE_UB;
    int ret = sprintf(trans_info.dev_info.dev.dev_name, "%s", dev_info);
    if (ret < 0 || ret >= UMQ_DEV_NAME_SIZE) {
        UBS_VLOG_ERR("Failed to sprintf_s device name\n");
        return UBS_ERROR;
    }

    if (strstr(trans_info.dev_info.dev.dev_name, "bonding_dev") == nullptr) {
        trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_DEV;
        trans_info.dev_info.dev.eid_idx = UmqSetting::UMQ_EID_INDEX;
    } else {
        trans_info.dev_info.assign_mode = UMQ_DEV_ASSIGN_MODE_EID;
        trans_info.dev_info.eid.eid = UmqSetting::UMQ_LOCAL_EID;
        UmqSetting::UMQ_IS_BONDING = true;
    }

    ret = UmqApi::umq_dev_add(&trans_info);
    if (ret != 0 && ret != -UMQ_ERR_EEXIST) {
        UBS_VLOG_ERR("[UMQ_API] umq_dev_add() failed, ret: %d\n", ret);
        return -1;
    }

    // TODO RegisterAsyncEvent AE事件上报
    // return RegisterAsyncEvent(trans_info);
    return UBS_OK;
}

Result UmqBackend::FindDevName()
{
    umq_trans_mode_t transMode = UMQ_TRANS_MODE_UB;
    int devCount = 0;
    umq_dev_info_t *umqDevInfo = UmqApi::umq_dev_info_list_get(transMode, &devCount);
    if (umqDevInfo == nullptr || devCount <= 0) {
        UBS_VLOG_ERR("[UMQ_API] umq_dev_info_list_get() failed, ret: %p, dev count: %d\n", umqDevInfo, devCount);
        return UBS_ERROR;
    }

    int index = 0;
    int bondingIndex = -1;
    for (; index < devCount; ++index) {
        const char *name = umqDevInfo[index].dev_name;
        if (strcmp(name, "bonding_dev_0") == 0) {
            bondingIndex = index;
            break;
        }
        if ((bondingIndex == -1) && (strstr(name, "bonding_dev_") != nullptr)) {
            bondingIndex = index;
        }
    }
    if ((bondingIndex == -1) || (bondingIndex > devCount) || (umqDevInfo[bondingIndex].ub.eid_cnt == 0)) {
        UBS_VLOG_ERR("Failed to find bonding dev in the environment.\n");
        return UBS_ERROR;
    }

    UmqSetting::UMQ_DEV_NAME = umqDevInfo[bondingIndex].dev_name;
    if (UmqSetting::UMQ_DEV_NAME.size() >= UMQ_DEV_NAME_SIZE) {
        UBS_VLOG_ERR("Failed to set device name, name size: %d\n", UmqSetting::UMQ_DEV_NAME.size());
        return UBS_ERROR;
    }

    UmqSetting::UMQ_LOCAL_EID = umqDevInfo[bondingIndex].ub.eid_list[0].eid;
    UmqApi::umq_dev_info_list_free(transMode, umqDevInfo);
    return UBS_OK;
}
} // namespace umq
} // namespace ubs
} // namespace ock