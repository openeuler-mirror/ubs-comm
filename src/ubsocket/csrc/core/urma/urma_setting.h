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
#ifndef UBS_COMM_URMA_SETTING_H
#define UBS_COMM_URMA_SETTING_H

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "under_api/urma/dl_urma_api.h"
#include "under_api/urma/urma_types.h"

namespace ock {
namespace ubs {
namespace urma {
class UrmaSetting {
public:
    static void Init() noexcept;

public:
    static bool UB_USING_BOUNDING_DEV;
    static std::string UB_DEV_NAME;
    static std::string UB_DEV_EID;
    static uint32_t UB_EID_INDEX;
    /* setting for tp */
    static ub_trans_mode UB_TANS_MODE;
    /* setting for jetty pool */
    static bool UB_JETTY_SHARED_ENABLED;
    static uint16_t UB_JETTY_INIT_COUNT;
    static uint16_t UB_JETTY_MAX_COUNT;
    static uint16_t UB_JETTY_JFR_COUNT;
    static uint16_t UB_JETTY_JFS_DEPTH;
    static uint16_t UB_JETTY_JFR_DEPTH;
    static urma_transport_mode_t UB_JETTY_TRANS_MODE;
    static urma_tp_type_t UB_JETTY_TP_TYPE;
    /* setting for poller */
    static uint16_t UB_POLLER_COUNT;
    static uint32_t UB_POLLER_JFC_DEPTH;
    static bool UB_POLLER_BUSY_POLLING;
    /* setting for ub buffer pool */
    static uint32_t UB_BUF_POOL_SPACE_SIZE;
    static uint16_t UB_BUF_BLOCK_SIZE_MB;
    static uint16_t UB_BUF_INIT_MEM_SIZE_MB;
    static uint16_t UB_BUF_DIRECT_INIT_BLOCK_COUNT;
    static uint16_t UB_BUF_PAGE_SIZE_KB;
    static bool UB_BUF_ASYNC_EXPAND;
    /* setting for epoll handle mode */
    static EpollHandleMode UB_EPOLL_HANDLE_MODE;
    static uint64_t UB_BAD_PORT_RECOVER_SECONDS;
    static uint32_t UB_KEEPALIVE_TIME;
    static uint32_t UB_KEEPALIVE_INTVL;
    static uint32_t UB_KEEPALIVE_PROBES;

private:
    static void AddRules();
    static Result LoadEnv();
};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_SETTING_H
