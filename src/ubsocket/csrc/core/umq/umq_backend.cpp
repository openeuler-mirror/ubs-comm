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

    std::lock_guard<std::mutex> guard(UMQ_MUTEX);
    if (UMQ_INITED) {
        //UBS_VLOG_DEBUG("umq already initialized");
        return UBS_OK;
    }

    /* initialize umq settting */
    UmqSetting::Init();

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
    umq_config.trans_info[0].mem_cfg.total_size = static_cast<uint64_t>(UmqSetting::UMQ_MEM_POOL_INIT_SIZE_MB);
    umq_config.trans_info[0].trans_mode = UMQ_TRANS_MODE_UB;
    umq_config.buf_pool_cfg.umq_buf_pool_max_size = UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB;
    umq_config.buf_pool_cfg.tls_qbuf_pool_depth = UmqSetting::UMQ_BUF_POOL_DEPTH;
    umq_config.io_lock_free = false;

    /* init umq */
    auto ret = UmqApi::umq_init(&umq_config);
    if (ret != 0) {
        UBS_VLOG_ERR("umq_init() failed, ret: %d\n", ret);
        // ResetBrpcAllocator();
        // SetSocketFdTransMode(SOCKET_FD_TRANS_MODE_TCP);
        return UBS_ERROR;
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
} // namespace umq
} // namespace ubs
} // namespace ock