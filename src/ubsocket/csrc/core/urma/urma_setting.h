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
    static std::string UBS_DEV_NAME;
    static std::string UBS_DEV_EID;
    static uint32_t UBS_DEV_EID_INDEX;
    /* setting for tp */
    static ub_trans_mode UBS_TRANS_MODE;
    static int8_t UBS_LINK_PRIORITY;
    /* setting for poller */
    static uint16_t UBS_POLLER_COUNT;
    static uint32_t UBS_POLLER_CQ_DEPTH;
    static bool UBS_POLLER_BUSY_POLLING;
    /* setting for ub buffer pool */
    static uint32_t UBS_BUF_POOL_SPACE_SIZE;
    static uint16_t UBS_BUF_EXPAND_SIZE_MB;
    static uint16_t UBS_BUF_INIT_SIZE_MB;
    static uint16_t UBS_BUF_DIRECT_INIT_BLOCK_COUNT;
    static std::string UBS_BUF_PAGE_SIZE_KB; /* page type: "large"(64K) | "tiny"(4K, pure CTP), unified with legacy umq block_type */
    static bool UBS_BUF_ASYNC_EXPAND;
    /* externalized from constants */
    static uint32_t UBS_CONNECTS_BUF_DEFAULT_KB; /* unified send/recv buffer default KB, negotiated via handshake */
    static uint32_t UBS_JFS_DEPTH;           /* default JFS queue depth */
    static uint32_t UBS_JFR_DEPTH;           /* default JFR queue depth */
    static uint32_t UBS_SND_BUFF_ALLOC_UNIT; /* wire offset granularity in bytes; both ends MUST be configured identically */
    /* setting for epoll handle mode */
    static EpollHandleMode UBS_EPOLL_MODE;
    static uint64_t UBS_BAD_UBPORT_TTL;
    static uint32_t UBS_KEEPALIVE_TIME;
    static uint32_t UBS_KEEPALIVE_INTVL;
    static uint32_t UBS_KEEPALIVE_PROBES;

private:
    static void AddRules();
    static Result LoadEnv();
};
} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_SETTING_H
