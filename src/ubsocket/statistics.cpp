/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-16
 *Note:
 *History: 2025-07-16
*/
#include "statistics.h"
#include "umq_dfx_api.h"
#include "configure_settings.h"

uint32_t Statistics::Recorder::m_title_len = 0;
volatile bool Statistics::GlobalStatsMgr::m_running = true;

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
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to parse retry_count caused by no data to process\n");
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
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to parse retry_count: %s\n", e.what());
            return false;
        }
    }
}

void Statistics::StatsMgr::UpdateReTxCount(const umq_trans_mode_t umq_trans_mode)
{
    int ret = umq_stats_tp_perf_start(umq_trans_mode);
    int umqTransModeInt = umq_trans_mode;
    if (ret < 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "Failed to start tp perf: umq_trans_mode=%d\n", umqTransModeInt);
        return;
    }

    ret = umq_stats_tp_perf_stop(umq_trans_mode);
    if (ret < 0) {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "Failed to stop tp perf: umq_trans_mode=%d\n", umqTransModeInt);
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
            RPC_ADPT_VLOG_ERR(ubsocket::UBSocket, "Failed to parse retry_count from perf info.\n");
        }
    } else {
        RPC_ADPT_VLOG_ERR(ubsocket::UMQ_API, "Failed to get tp perf info: umq_trans_mode=%d\n", umqTransModeInt);
    }
}
