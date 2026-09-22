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

#include "umq_setting.h"
#include <arpa/inet.h>  // 包含 inet_pton、inet_ntop 函数
#include <sys/socket.h> // 包含 AF_INET、AF_INET6 等地址族常量
#include <cstdlib>
#include "core/ubsocket_socket_helper.h"
#include "umq_qbuf_list.h"

namespace ock {
namespace ubs {
namespace umq {
#define ENV_UMQ_INITIAL_CREDIT "UBSOCKET_INITIAL_CREDIT"
#define ENV_UMQ_MAX_CREDIT_PER_REQUEST "UBSOCKET_MAX_CREDIT_PER_REQUEST"
#define ENV_UMQ_MIN_RESERVED_CREDIT "UBSOCKET_MIN_RESERVED_CREDIT"
#define ENV_UMQ_BLOCK_TYPE "UBSOCKET_BLOCK_TYPE"
#define ENV_UMQ_MEM_POOL_MAX_SIZE "UBSOCKET_POOL_MAX_SIZE"
#define ENV_UMQ_TINY_POOL_ENABLE "UBSOCKET_UMQ_TINY_POOL_ENABLE"
#define ENV_UMQ_TINY_POOL_BLOCK_SIZE "UBSOCKET_UMQ_TINY_POOL_BLOCK_SIZE"
#define ENV_UMQ_TINY_POOL_BLOCK_COUNT "UBSOCKET_UMQ_TINY_POOL_BLOCK_COUNT"
#define ENV_UMQ_TLS_TINY_POOL_DEPTH "UBSOCKET_UMQ_TLS_TINY_POOL_DEPTH"
#define ENV_UMQ_SCHEDULE_POLICY "UBSOCKET_SCHEDULE_POLICY"
#define ENV_UMQ_UB_TRANS_MODE "UBSOCKET_UB_TRANS_MODE"
#define ENV_UMQ_FLOW_CONTROL_ENABLED "UBSOCKET_FLOW_CONTROL_ENABLE"
#define ENV_UMQ_RANDOM_ROUTE "UBSOCKET_RANDOM_ROUTE"
#define ENV_UMQ_LINK_PRIORITY "UBSOCKET_LINK_PRIORITY"
#define ENV_UMQ_TP_TYPE "UBSOCKET_JETTY_TYPE"
#define ENV_UMQ_TP_POOL_SIZE "UBSOCKET_JETTY_POOL_SIZE"
#define ENV_UMQ_O3_TIMEOUT_MS "UBSOCKET_O3_TIMEOUT_MS"
#define ENV_UMQ_SMALL_BUF_POOL_DEPTH "UBSOCKET_SMALL_BUF_POOL_DEPTH"
#define ENV_UMQ_SMALL_GLOBAL_POOL_DEPTH "UBSOCKET_SMALL_GLOBAL_POOL_DEPTH"
#define ENV_UMQ_MIDDLE_BUF_POOL_DEPTH "UBSOCKET_MIDDLE_BUF_POOL_DEPTH"
#define ENV_UMQ_MIDDLE_GLOBAL_POOL_DEPTH "UBSOCKET_MIDDLE_GLOBAL_POOL_DEPTH"
#define ENV_UMQ_MIDDLE_POOL_BLOCK_SIZE "UBSOCKET_MIDDLE_POOL_BLOCK_SIZE"
#define ENV_UMQ_SHRINK_DECAY_MS "UBSOCKET_SHRINK_DECAY_MS"

#define DEFAULT_DEV_SCHEDULE_POLICY "affinity_priority"
#define ROUND_ROBIN_DEV_SCHEDULE_POLICY "rr"
#define CPU_AFFINITY_DEV_SCHEDULE_POLICY "affinity"
#define CPU_AFFINITY_PRIORITY_DEV_SCHEDULE_POLICY "affinity_priority"

// AddRules 与 VerifySetting 共用, 避免约束不一致
constexpr uint32_t UMQ_MIDDLE_POOL_BLOCK_SIZE_MIN = static_cast<uint32_t>(SIZE_8K);
constexpr uint32_t UMQ_MIDDLE_POOL_BLOCK_SIZE_MAX = static_cast<uint32_t>(SIZE_1M);
constexpr uint32_t UMQ_MIDDLE_POOL_BLOCK_SIZE_ALIGN = static_cast<uint32_t>(SIZE_4K);
constexpr uint32_t UMQ_DEFAULT_SIZE_CLASS_COUNT = 2;

umq_buf_block_size_t UmqSetting::IO_BLOCK_TYPE = BLOCK_SIZE_4K;
umq_buf_block_size_t UmqSetting::UMQ_POOL_BASE_BLOCK_SIZE = BLOCK_SIZE_4K;
uint32_t UmqSetting::UMQ_SIZE_CLASS_COUNT = 2;

uint32_t UmqSetting::UMQ_EXPLICIT_BLOCK_SIZES[UMQ_SIZE_CLASS_MAX] = {4096, 65536};
uint16_t UmqSetting::UMQ_FC_DEFAULT_CREDIT = 16L;
uint16_t UmqSetting::UMQ_FC_MAX_CREDIT = 256L;
uint16_t UmqSetting::UMQ_FC_MIN_CREDIT = 2;
uint64_t UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 2048;
bool UmqSetting::UMQ_TINY_POOL_ENABLE = true;
umq_tiny_buf_block_size_t UmqSetting::UMQ_TINY_POOL_BLOCK_SIZE = TINY_BLOCK_SIZE_1K;
uint32_t UmqSetting::UMQ_TINY_POOL_BLOCK_COUNT = 1024;
uint64_t UmqSetting::UMQ_TLS_TINY_POOL_DEPTH = 1024;
uint64_t UmqSetting::UMQ_SMALL_BUF_POOL_DEPTH = 160;
uint64_t UmqSetting::UMQ_SMALL_GLOBAL_POOL_DEPTH = 320;
uint64_t UmqSetting::UMQ_MIDDLE_BUF_POOL_DEPTH = 96;
uint64_t UmqSetting::UMQ_MIDDLE_GLOBAL_POOL_DEPTH = 192;
uint32_t UmqSetting::UMQ_MIDDLE_POOL_BLOCK_SIZE = 65536;
int UmqSetting::UMQ_PROCESS_SOCKET_ID = -1;
std::vector<uint32_t> UmqSetting::UMQ_ALL_SOCKET_IDS = {};
uint32_t UmqSetting::UMQ_POST_BATCH_MAX = 256UL;
uint32_t UmqSetting::UMQ_EID_INDEX = 0;
std::string UmqSetting::UMQ_DEV_NAME = "";
std::string UmqSetting::UMQ_DEV_IP = "";
umq_eid_t UmqSetting::UMQ_LOCAL_EID = {};
std::string UmqSetting::UMQ_DEV_SCHEDULE_POLICY_NAME = DEFAULT_DEV_SCHEDULE_POLICY;
dev_schedule_policy UmqSetting::UMQ_DEV_SCHEDULE_POLICY = CPU_AFFINITY_PRIORITY;
// TODO: 根据 UBS_TRANS_MODE 来设置 UMQ_TRANS_MODE, 待增加 ENV转换器
umq_trans_mode_t UmqSetting::UMQ_TRANS_MODE = UMQ_TRANS_MODE_UB;
ub_trans_mode UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
umq_tp_mode_t UmqSetting::UMQ_UB_TP_MODE = UMQ_TM_RM;
umq_tp_type_t UmqSetting::UMQ_UB_TP_TYPE = UMQ_TP_TYPE_RTP;
bool UmqSetting::UMQ_IS_BONDING = false;
bool UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
bool UmqSetting::UMQ_RANDOM_ROUTE = true;
int8_t UmqSetting::UMQ_LINK_PRIORITY = UBSOCKET_LINK_PRIORITY_DEFAULT;
pool_type_t UmqSetting::UMQ_TP_TYPE = POOL;
uint32_t UmqSetting::UMQ_TP_POOL_SIZE = 800;
uint64_t UmqSetting::UMQ_O3_TIMEOUT_MS = 60000;
uint32_t UmqSetting::UMQ_SHRINK_DECAY_MS = 60000;

void UmqSetting::AddRules() noexcept
{
    /* int64 rule: name, required, min, max */
    Int64Rule rules_int64[] = {{ENV_UMQ_INITIAL_CREDIT, false, 1, 1024},
                               {ENV_UMQ_MAX_CREDIT_PER_REQUEST, false, 1, 1024},
                               {ENV_UMQ_MIN_RESERVED_CREDIT, false, 1, 1024},
                               {ENV_UMQ_MEM_POOL_MAX_SIZE, false, 1, 6144},
                               {ENV_UMQ_LINK_PRIORITY, false, -1, 15},
                               {ENV_UMQ_TP_POOL_SIZE, false, 1, 1000},
                                {ENV_UMQ_TINY_POOL_BLOCK_COUNT, false, 1, std::numeric_limits<int64_t>::max()},
                                {ENV_UMQ_TLS_TINY_POOL_DEPTH, false, 0, 131072},
                                {ENV_UMQ_O3_TIMEOUT_MS, false, 2, std::numeric_limits<int64_t>::max()},
                                {ENV_UMQ_SMALL_BUF_POOL_DEPTH, false, 1, 15360},
                                {ENV_UMQ_SMALL_GLOBAL_POOL_DEPTH, false, 0, 15360},
                                {ENV_UMQ_MIDDLE_BUF_POOL_DEPTH, false, 1, 15360},
                                {ENV_UMQ_MIDDLE_GLOBAL_POOL_DEPTH, false, 0, 15360},
                                 {ENV_UMQ_MIDDLE_POOL_BLOCK_SIZE, false, UMQ_MIDDLE_POOL_BLOCK_SIZE_MIN,
                                  UMQ_MIDDLE_POOL_BLOCK_SIZE_MAX},
                                {ENV_UMQ_SHRINK_DECAY_MS, false, 0, 60000}};

    /* str enum rules: name, required, enum */
    StrEnumRule rules_str_enum[] = {{ENV_UMQ_BLOCK_TYPE, false, "default|large"},
                                    {ENV_UMQ_TINY_POOL_ENABLE, false, "true|false"},
                                    {ENV_UMQ_TINY_POOL_BLOCK_SIZE, false, "512|1024|2048|4096|8192|1K|2K|4K|8K"},
                                    {ENV_UMQ_SCHEDULE_POLICY, false, "rr|affinity|affinity_priority"},
                                    {ENV_UMQ_UB_TRANS_MODE, false, "RC_TP|RM_TP|RM_CTP|RC_CTP"},
                                    {ENV_UMQ_FLOW_CONTROL_ENABLED, false, "true|false"},
                                    {ENV_UMQ_RANDOM_ROUTE, false, "true|false"},
                                    {ENV_UMQ_TP_TYPE, false, "single|pool"}};

    for (auto &item : rules_int64) {
        Validator::Instance().AddNumRule(item);
    }

    for (auto &item : rules_str_enum) {
        Validator::Instance().AddStrEnumRule(item);
    }

    UBS_SLOG_DEBUG(Validator::Instance().DumpString());
}

Result UmqSetting::LoadEnv() noexcept
{
    /* shared value from env */
    int64_t int64EnvValue = 0;
    std::string strEnvValue;
    using GS = GlobalSetting;

    /* load from env */

    if (GS::GetEnvAndValidate(ENV_UMQ_INITIAL_CREDIT, int64EnvValue)) {
        UMQ_FC_DEFAULT_CREDIT = static_cast<uint16_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MAX_CREDIT_PER_REQUEST, int64EnvValue)) {
        UMQ_FC_MAX_CREDIT = static_cast<uint16_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MIN_RESERVED_CREDIT, int64EnvValue)) {
        UMQ_FC_MIN_CREDIT = static_cast<uint16_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MEM_POOL_MAX_SIZE, int64EnvValue)) {
        UMQ_MEM_POOL_MAX_SIZE_MB = static_cast<uint64_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_SMALL_BUF_POOL_DEPTH, int64EnvValue)) {
        UMQ_SMALL_BUF_POOL_DEPTH = static_cast<uint64_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_SMALL_GLOBAL_POOL_DEPTH, int64EnvValue)) {
        UMQ_SMALL_GLOBAL_POOL_DEPTH = static_cast<uint64_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MIDDLE_BUF_POOL_DEPTH, int64EnvValue)) {
        UMQ_MIDDLE_BUF_POOL_DEPTH = static_cast<uint64_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MIDDLE_GLOBAL_POOL_DEPTH, int64EnvValue)) {
        UMQ_MIDDLE_GLOBAL_POOL_DEPTH = static_cast<uint64_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_MIDDLE_POOL_BLOCK_SIZE, int64EnvValue)) {
        UMQ_MIDDLE_POOL_BLOCK_SIZE = static_cast<uint32_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_TINY_POOL_ENABLE, strEnvValue)) {
        UMQ_TINY_POOL_ENABLE = Func::BoolFromStr(strEnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_TINY_POOL_BLOCK_SIZE, strEnvValue)) {
        UMQ_TINY_POOL_BLOCK_SIZE = TinyBlockSizeFromStr(strEnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_TINY_POOL_BLOCK_COUNT, int64EnvValue)) {
        UMQ_TINY_POOL_BLOCK_COUNT = static_cast<uint32_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_TLS_TINY_POOL_DEPTH, int64EnvValue)) {
        UMQ_TLS_TINY_POOL_DEPTH = static_cast<uint64_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_TP_TYPE, strEnvValue)) {
        if (strEnvValue == "pool") {
            UMQ_TP_TYPE = POOL;
        } else {
            UMQ_TP_TYPE = SINGLE;
        }
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_TP_POOL_SIZE, int64EnvValue)) {
        UMQ_TP_POOL_SIZE = static_cast<uint32_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_LINK_PRIORITY, int64EnvValue)) {
        UMQ_LINK_PRIORITY = static_cast<int8_t>(int64EnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_FLOW_CONTROL_ENABLED, strEnvValue)) {
        UMQ_FLOW_CONTROL_ENABLE = Func::BoolFromStr(strEnvValue);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_RANDOM_ROUTE, strEnvValue)) {
        UMQ_RANDOM_ROUTE = Func::BoolFromStr(strEnvValue);
        UBS_VLOG_DEBUG("Current random route setting: %d\n", UMQ_RANDOM_ROUTE ? 1 : 0);
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_SCHEDULE_POLICY, strEnvValue)) {
        UMQ_DEV_SCHEDULE_POLICY_NAME = strEnvValue;
        UMQ_DEV_SCHEDULE_POLICY = SchedulePolicyFromStr(strEnvValue);
        UBS_VLOG_INFO("Current policy type: %s", UMQ_DEV_SCHEDULE_POLICY_NAME.c_str());
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_UB_TRANS_MODE, strEnvValue)) {
        std::string ub_trans_mode_str = strEnvValue;
        if (ub_trans_mode_str == "RM_TP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RM_TP;
            UMQ_UB_TP_MODE = UMQ_TM_RM;
            UMQ_UB_TP_TYPE = UMQ_TP_TYPE_RTP;
        } else if (ub_trans_mode_str == "RM_CTP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RM_CTP;
            UMQ_UB_TP_MODE = UMQ_TM_RM;
            UMQ_UB_TP_TYPE = UMQ_TP_TYPE_CTP;
        } else if (ub_trans_mode_str == "RC_TP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RC_TP;
            UMQ_UB_TP_MODE = UMQ_TM_RC;
            UMQ_UB_TP_TYPE = UMQ_TP_TYPE_RTP;
        } else if (ub_trans_mode_str == "RC_CTP") {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RC_CTP;
            UMQ_UB_TP_MODE = UMQ_TM_RC;
            UMQ_UB_TP_TYPE = UMQ_TP_TYPE_CTP;
        } else {
            UMQ_UB_TRANS_MODE = ub_trans_mode::RC_TP;
            UMQ_UB_TP_MODE = UMQ_TM_RC;
            UMQ_UB_TP_TYPE = UMQ_TP_TYPE_RTP;
        }
        UBS_VLOG_INFO("Current ub trans mode");
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_BLOCK_TYPE, strEnvValue)) {
        IO_BLOCK_TYPE = BlockTypeFromStr(strEnvValue);
    } else {
        IO_BLOCK_TYPE = DefaultBlockTypeCheck();
    }

    if (GS::GetEnvAndValidate(ENV_UMQ_SHRINK_DECAY_MS, int64EnvValue)) {
        UMQ_SHRINK_DECAY_MS = static_cast<uint32_t>(int64EnvValue);
    }

    return UBS_OK;
}

Result UmqSetting::VerifySetting() noexcept
{
    auto &validator = Validator::Instance();

    if (!validator.Validate(ENV_UMQ_MEM_POOL_MAX_SIZE, (int64_t)UMQ_MEM_POOL_MAX_SIZE_MB, "ubsocket_pool_max_size")) {
        UBS_SLOG_ERR(validator.LastErrMsg());
        return UBS_INVALID_PARAM;
    }

    {
        uint32_t bs = UMQ_MIDDLE_POOL_BLOCK_SIZE;
        if (bs < UMQ_MIDDLE_POOL_BLOCK_SIZE_MIN || bs > UMQ_MIDDLE_POOL_BLOCK_SIZE_MAX ||
            bs % UMQ_MIDDLE_POOL_BLOCK_SIZE_ALIGN != 0) {
            UBS_VLOG_ERR("UBSOCKET_MIDDLE_POOL_BLOCK_SIZE(%u) must be 4K*2^n in [%u, %u]\n", bs,
                         UMQ_MIDDLE_POOL_BLOCK_SIZE_MIN, UMQ_MIDDLE_POOL_BLOCK_SIZE_MAX);
            return UBS_INVALID_PARAM;
        }
        if ((bs & (bs - 1)) != 0) {
            UBS_VLOG_ERR("UBSOCKET_MIDDLE_POOL_BLOCK_SIZE(%u) must be power of 2\n", bs);
            return UBS_INVALID_PARAM;
        }
    }

    UMQ_EXPLICIT_BLOCK_SIZES[0] = static_cast<uint32_t>(SIZE_4K);
    UMQ_EXPLICIT_BLOCK_SIZES[1] = UMQ_MIDDLE_POOL_BLOCK_SIZE;
    UMQ_SIZE_CLASS_COUNT = UMQ_DEFAULT_SIZE_CLASS_COUNT;

    UMQ_SMALL_BUF_POOL_DEPTH = UMQ_SMALL_BUF_POOL_DEPTH;
    UMQ_SMALL_GLOBAL_POOL_DEPTH = UMQ_SMALL_GLOBAL_POOL_DEPTH;
    UMQ_MIDDLE_BUF_POOL_DEPTH = UMQ_MIDDLE_BUF_POOL_DEPTH;
    UMQ_MIDDLE_GLOBAL_POOL_DEPTH = UMQ_MIDDLE_GLOBAL_POOL_DEPTH;

    UBS_VLOG_INFO("UBSOCKET_POOL_MAX_SIZE is to set: %ld MB", UMQ_MEM_POOL_MAX_SIZE_MB);
    UBS_VLOG_INFO("Pool config: small(tls=%llu,global=%llu) middle(tls=%llu,global=%llu,blk_size=%u)\n",
                  (unsigned long long)UMQ_SMALL_BUF_POOL_DEPTH, (unsigned long long)UMQ_SMALL_GLOBAL_POOL_DEPTH,
                  (unsigned long long)UMQ_MIDDLE_BUF_POOL_DEPTH, (unsigned long long)UMQ_MIDDLE_GLOBAL_POOL_DEPTH,
                  UMQ_MIDDLE_POOL_BLOCK_SIZE);


    return UBS_OK;
}

Result UmqSetting::Init() noexcept
{
    AddRules();
    auto result = LoadEnv();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("initialize failed as options are invalid");
        return result;
    }

    result = VerifySetting();
    if (result != UBS_OK) {
        UBS_VLOG_ERR("initialize failed as options are invalid");
        errno = EINVAL;
        return result;
    }

    UBS_VLOG_INFO("IO_BLOCK_TYPE=%u, POOL_BASE=%u, SIZE_CLASS_COUNT=%u",
                  static_cast<uint32_t>(IO_BLOCK_TYPE), static_cast<uint32_t>(UMQ_POOL_BASE_BLOCK_SIZE),
                  GetSizeClassCount());

    return result;
}

uint64_t UmqSetting::BlockSizeToBytes(umq_buf_block_size_t block_type) noexcept
{
    switch (block_type) {
        case BLOCK_SIZE_4K:
            return SIZE_4K;
        case BLOCK_SIZE_8K:
            return SIZE_8K;
        case BLOCK_SIZE_16K:
            return SIZE_16K;
        case BLOCK_SIZE_32K:
            return SIZE_32K;
        case BLOCK_SIZE_64K:
            return SIZE_64K;
        case BLOCK_SIZE_128K:
            return SIZE_128K;
        case BLOCK_SIZE_256K:
            return SIZE_256K;
        case BLOCK_SIZE_512K:
            return SIZE_512K;
        case BLOCK_SIZE_1M:
            return SIZE_1M;
        default:
            return SIZE_4K;
    }
}

uint32_t UmqSetting::GetSizeClassCount() noexcept
{
    return UMQ_SIZE_CLASS_COUNT;
}

uint32_t UmqSetting::GetIOBufSizeByClass(uint32_t sc) noexcept
{
    if (sc < UMQ_SIZE_CLASS_COUNT) {
        return UMQ_EXPLICIT_BLOCK_SIZES[sc] - IOBUF_DIFF;
    }
    return UMQ_EXPLICIT_BLOCK_SIZES[UMQ_SIZE_CLASS_COUNT - 1] - IOBUF_DIFF;
}

uint32_t UmqSetting::GetIOBufSize() noexcept
{
    return GetIOBufSizeByClass(0);
}

void UmqSetting::GetRXBufCountsByClass(uint32_t total, uint32_t *counts, uint32_t count_size) noexcept
{
    memset(counts, 0, sizeof(uint32_t) * count_size);
    if (count_size > 0) {
        counts[0] = total;
    }
}

void UmqSetting::CountRXBufByClass(umq_buf_t **buf, int buf_num, uint32_t *counts, uint32_t count_size) noexcept
{
    memset(counts, 0, sizeof(uint32_t) * count_size);
    for (int i = 0; i < buf_num; i++) {
        /* Classify by total block footprint (data + headroom), which reflects
         * the physical block size the buf occupies. UMQ-internal prefill bufs
         * carry data_size=4096 with headroom_size=0 (total 4096); ubsocket
         * PrefillRx bufs carry data_size=4064 with headroom_size=32 (total
         * 4096). Both fit in a 4KB RX pool block → SC[0]. */
        uint32_t total = buf[i]->data_size + buf[i]->headroom_size;
        uint32_t sc = 0;
        for (; sc < UMQ_SIZE_CLASS_COUNT; sc++) {
            if (total <= UMQ_EXPLICIT_BLOCK_SIZES[sc]) {
                break;
            }
        }
        if (sc == UMQ_SIZE_CLASS_COUNT) {
            sc = UMQ_SIZE_CLASS_COUNT - 1;
        }
        if (sc < count_size) {
            counts[sc]++;
        }
    }
}

umq_buf_t *UmqSetting::MergeBufLists(umq_buf_t **lists, uint32_t *counts, uint32_t count_size) noexcept
{
    umq_buf_t *head = nullptr;
    umq_buf_t *tail = nullptr;
    for (uint32_t sc = 0; sc < count_size; sc++) {
        if (lists[sc] == nullptr)
            continue;
        umq_buf_t *list_tail = lists[sc];
        while (QBUF_LIST_NEXT(list_tail) != nullptr)
            list_tail = QBUF_LIST_NEXT(list_tail);
        if (head == nullptr) {
            head = lists[sc];
        } else {
            QBUF_LIST_NEXT(tail) = lists[sc];
        }
        tail = list_tail;
    }
    return head;
}

uint64_t UmqSetting::FloorMask() noexcept
{
    return BlockSizeToBytes(UMQ_POOL_BASE_BLOCK_SIZE) - MASK_DIFF;
}

umq_buf_block_size_t UmqSetting::DefaultBlockTypeCheck() noexcept
{
    return BLOCK_SIZE_4K;
}

umq_buf_block_size_t UmqSetting::BlockTypeFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == LARGE_QBUF_BLOCK_TYPE) {
        return BLOCK_SIZE_64K;
    }
    return DefaultBlockTypeCheck();
}

umq_tiny_buf_block_size_t UmqSetting::TinyBlockSizeFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == "512") {
        return TINY_BLOCK_SIZE_512;
    } else if (typeStr == "1024" || typeStr == "1K") {
        return TINY_BLOCK_SIZE_1K;
    } else if (typeStr == "2048" || typeStr == "2K") {
        return TINY_BLOCK_SIZE_2K;
    } else if (typeStr == "4096" || typeStr == "4K") {
        return TINY_BLOCK_SIZE_4K;
    } else if (typeStr == "8192" || typeStr == "8K") {
        return TINY_BLOCK_SIZE_8K;
    }
    return TINY_BLOCK_SIZE_1K;
}


dev_schedule_policy UmqSetting::SchedulePolicyFromStr(const std::string &typeStr) noexcept
{
    if (typeStr == ROUND_ROBIN_DEV_SCHEDULE_POLICY) {
        return ROUND_ROBIN;
    } else if (typeStr == CPU_AFFINITY_DEV_SCHEDULE_POLICY) {
        return CPU_AFFINITY;
    } else if (typeStr == CPU_AFFINITY_PRIORITY_DEV_SCHEDULE_POLICY) {
        return CPU_AFFINITY_PRIORITY;
    }
    // 如果字符串不匹配，返回默认值
    return CPU_AFFINITY_PRIORITY;
}
} // namespace umq
} // namespace ubs
} // namespace ock
