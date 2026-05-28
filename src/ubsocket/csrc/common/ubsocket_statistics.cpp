/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: Provide statistic class
 * Create: 2026
 */

#include "ubsocket_statistics.h"

namespace {
    constexpr const char* PREFIX = "retry_count: ";

    constexpr int MAX_DIGIT_LENGTH = 20;

    bool TryGetRetryCount(const char* perfBuf, size_t bufLen, uint64_t& retryCount)
    {
        if (bufLen == 0) {
            return false;
        }
        const char* ptr = static_cast<const char*>(memmem(perfBuf, bufLen, PREFIX, strlen(PREFIX)));
        if (ptr == nullptr) {
            return false;
        }
        ptr += strlen(PREFIX);
        const size_t remaining = bufLen - (ptr - perfBuf);
        const void* found = memchr(ptr, '\n', remaining);
        if (found == nullptr) {
            UBS_VLOG_ERR("Failed to parse retry_count caused by no data to process\n");
            return false;
        }
        const char* newlinePtr = static_cast<const char*>(found);
        size_t digitLen = static_cast<size_t>(newlinePtr - ptr);
        if (digitLen == 0 || digitLen > MAX_DIGIT_LENGTH) {
            return false;
        }
        const std::string numStr(ptr, digitLen);
        try {
            size_t processedCharCount = 0;
            retryCount = std::stoull(numStr, &processedCharCount);

            // 检查是否转换了所有字符
            return processedCharCount > 0;
        } catch (const std::exception& e) {
            // 处理转换失败（如：非数字字符、数值溢出等）
            UBS_VLOG_ERR("Failed to parse retry_count: %s\n", e.what());
            return false;
        }
    }
}

void Statistics::StatsMgr::UpdateReTxCount(const umq_trans_mode_t umq_trans_mode)
{
    int ret = umq_stats_tp_perf_start(umq_trans_mode);
    int umqTransModeInt = umq_trans_mode;
    if (ret < 0) {
        UBS_VLOG_ERR("Failed to start tp perf: umq_trans_mode=%d\n", umqTransModeInt);
        return;
    }

    ret = umq_stats_tp_perf_stop(umq_trans_mode);
    if (ret < 0) {
        UBS_VLOG_ERR("Failed to stop tp perf: umq_trans_mode=%d\n", umqTransModeInt);
        return;
    }

    char perfBuf[4096] = {};
    uint32_t perfLen = sizeof(perfBuf);
    ret = umq_stats_tp_perf_info_get(umq_trans_mode, perfBuf, &perfLen);
    if (ret == 0 && perfLen > 0) {
        uint64_t retryCount = 0;
        if (TryGetRetryCount(perfBuf, perfLen, retryCount)) {
            mReTxCount.store(retryCount);
        } else {
            UBS_VLOG_ERR("Failed to parse retry_count from perf info.\n");
        }
    } else {
        UBS_VLOG_ERR("Failed to get tp perf info: umq_trans_mode=%d\n", umqTransModeInt);
    }
}
