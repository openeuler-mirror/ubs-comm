/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UMQ_SETTING_H
#define UBS_COMM_UMQ_SETTING_H

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "include/ubsocket_def.h"
#include "under_api/dl_umq_api.h"
#include "umq/umq_types.h"

namespace ock {
namespace ubs {
namespace umq {

/* RX pool only carries 4KB blocks (SC[0]). Bigger bufs (READ dest from
 * DoReadOffer) belong to NORMAL pool and must not be refilled into RX pool. */
constexpr uint32_t UMQ_RX_POOL_SIZE_CLASS_COUNT = 1;


class UmqSetting {
public:
    UmqSetting() = delete;

    static uint32_t GetIOBufSize() noexcept;

    static uint32_t GetIOBufSizeByClass(uint32_t sc) noexcept;

    static uint32_t GetSizeClassCount() noexcept;

    static void GetRXBufCountsByClass(uint32_t total, uint32_t *counts, uint32_t count_size) noexcept;

    /* Classify polled RX bufs by total block footprint (data_size +
     * headroom_size) against the raw block size. RX pool blocks are 4KB;
     * UMQ-internal prefill bufs carry data_size=4096 with headroom=0 (total
     * 4096), while ubsocket bufs carry data_size=4064 with headroom=32 (total
     * 4096). Both fit in SC[0]. The IOBUF_DIFF-adjusted boundary (4064) or
     * data_size-only check would misclassify the former as SC[1]. */
    static void CountRXBufByClass(umq_buf_t **buf, int buf_num, uint32_t *counts, uint32_t count_size) noexcept;

    static umq_buf_t *MergeBufLists(umq_buf_t **lists, uint32_t *counts, uint32_t count_size) noexcept;

    static uint64_t FloorMask() noexcept;

public:
    static std::string UMQ_DEV_IP;
    static std::string UMQ_DEV_NAME;
    static uint32_t UMQ_EID_INDEX;
    static umq_eid_t UMQ_LOCAL_EID;
    static uint16_t UMQ_FC_DEFAULT_CREDIT;
    static uint16_t UMQ_FC_MAX_CREDIT;
    static uint16_t UMQ_FC_MIN_CREDIT;
    static uint64_t UMQ_MEM_POOL_MAX_SIZE_MB;
    static bool UMQ_TINY_POOL_ENABLE;
    static umq_tiny_buf_block_size_t UMQ_TINY_POOL_BLOCK_SIZE;
    static uint32_t UMQ_TINY_POOL_BLOCK_COUNT;
    static uint64_t UMQ_TLS_TINY_POOL_DEPTH;
    static uint32_t UMQ_POST_BATCH_MAX;
    static umq_buf_block_size_t IO_BLOCK_TYPE;
    static umq_buf_block_size_t UMQ_POOL_BASE_BLOCK_SIZE;
    static uint32_t UMQ_SIZE_CLASS_COUNT;
    static uint32_t UMQ_EXPLICIT_BLOCK_SIZES[UMQ_SIZE_CLASS_MAX];
    static uint64_t UMQ_SMALL_BUF_POOL_DEPTH;
    static uint64_t UMQ_SMALL_GLOBAL_POOL_DEPTH;
    static uint64_t UMQ_MIDDLE_BUF_POOL_DEPTH;
    static uint64_t UMQ_MIDDLE_GLOBAL_POOL_DEPTH;
    static uint32_t UMQ_MIDDLE_POOL_BLOCK_SIZE;
    static umq_trans_mode_t UMQ_TRANS_MODE;
    static int UMQ_PROCESS_SOCKET_ID;
    static std::vector<uint32_t> UMQ_ALL_SOCKET_IDS;
    static std::string UMQ_DEV_SCHEDULE_POLICY_NAME;
    static dev_schedule_policy UMQ_DEV_SCHEDULE_POLICY;
    static ub_trans_mode UMQ_UB_TRANS_MODE;
    static umq_tp_mode_t UMQ_UB_TP_MODE;
    static umq_tp_type_t UMQ_UB_TP_TYPE;
    static bool UMQ_IS_BONDING;
    static bool UMQ_FLOW_CONTROL_ENABLE;
    static bool UMQ_RANDOM_ROUTE;
    static int8_t UMQ_LINK_PRIORITY;
    static pool_type_t UMQ_TP_TYPE;
    static uint32_t UMQ_TP_POOL_SIZE;

    static uint64_t UMQ_O3_TIMEOUT_MS;
    static uint32_t UMQ_SHRINK_DECAY_MS;

    static constexpr size_t UMQ_SOCKET_SEQ_NUM_BIT_WIDTH = 24;
    static constexpr size_t UMQ_SOCKET_SEQ_NUM_MAX = (1ULL << UMQ_SOCKET_SEQ_NUM_BIT_WIDTH) - 2;
    static constexpr uint32_t UMQ_PROBE_USER_DATA_ID = 0xFFFFFF;

    static constexpr uint32_t UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX = 0;

private:
    static Result Init() noexcept;

    static void AddRules() noexcept;
    static Result LoadEnv() noexcept;
    static Result VerifySetting() noexcept;

    static uint64_t BlockSizeToBytes(umq_buf_block_size_t block_type) noexcept;
    static umq_buf_block_size_t DefaultBlockTypeCheck() noexcept;
    static umq_buf_block_size_t BlockTypeFromStr(const std::string &typeStr) noexcept;
    static umq_tiny_buf_block_size_t TinyBlockSizeFromStr(const std::string &typeStr) noexcept;
    static dev_schedule_policy SchedulePolicyFromStr(const std::string &policyStr) noexcept;

    friend class UmqBackend;
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_SETTING_H
