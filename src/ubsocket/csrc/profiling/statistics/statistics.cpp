/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the utility for umq buffer, iov, etc
 *Author:
 *Create: 2025-07-16
 *Note:
 *History: 2025-07-16
 */
#include "statistics.h"

#ifdef UMQ_BACKEND_ENABLED
#include "umq_dfx_api.h"
#endif
#ifdef URMA_DLOPEN_BACKEND_ENABLED
#include "under_api/urma/dl_urma_api.h"
#endif

uint32_t Statistics::Recorder::m_title_len = 0;
volatile bool Statistics::GlobalStatsMgr::m_running = true;

/* TryGetRetryCount: pure text parsing, no backend dependency.
 * Both UMQ (umq_stats_tp_perf_info_get) and URMA (urma_get_perf_info) return
 * perf info text containing "retry_count: N", this helper parses it. */
namespace {
constexpr const char *PREFIX = "retry_count: ";

constexpr int MAX_DIGIT_LENGTH = 20;

bool TryGetRetryCount(const char *perfBuf, size_t bufLen, uint64_t &retryCount)
{
    if (bufLen == 0) {
        return false;
    }
    const char *ptr = static_cast<const char *>(memmem(perfBuf, bufLen, PREFIX, strlen(PREFIX)));
    if (ptr == nullptr) {
        return false;
    }
    ptr += strlen(PREFIX);
    const size_t remaining = bufLen - (ptr - perfBuf);
    const void *found = memchr(ptr, '\n', remaining);
    if (found == nullptr) {
        UBS_VLOG_ERR("Failed to parse retry_count caused by no data to process\n");
        return false;
    }
    const char *newlinePtr = static_cast<const char *>(found);
    size_t digitLen = static_cast<size_t>(newlinePtr - ptr);
    if (digitLen == 0 || digitLen > MAX_DIGIT_LENGTH) {
        return false;
    }
    const std::string numStr(ptr, digitLen);
    try {
        size_t processedCharCount = 0;
        retryCount = std::stoull(numStr, &processedCharCount);

        return processedCharCount > 0;
    } catch (const std::exception &e) {
        UBS_VLOG_ERR("Failed to parse retry_count: %s\n", e.what());
        return false;
    }
}
} // namespace

void Statistics::StatsMgr::UpdateReTxCount(const umq_trans_mode_t umq_trans_mode)
{
#ifdef UMQ_BACKEND_ENABLED
    if (!UmqApi::IsLoaded()) {
        return;
    }

    std::lock_guard<std::mutex> lock(gTpPerfSeqMutex);

    int ret = 0;
    int umqTransModeInt = umq_trans_mode;

    const bool profEnabled = GlobalSetting::UBS_PROF_ENABLE;
    if (!profEnabled) {
        if (UmqApi::umq_stats_tp_perf_start(umq_trans_mode) != 0) {
            return;
        }
    }

    char perfBuf[4096] = {};
    uint32_t perfLen = sizeof(perfBuf);
    ret = UmqApi::umq_stats_tp_perf_info_get(umq_trans_mode, perfBuf, &perfLen);
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

    if (!profEnabled) {
        if (UmqApi::umq_stats_tp_perf_stop(umq_trans_mode) != 0) {
            UBS_VLOG_ERR("Failed to stop tp perf: umq_trans_mode=%d\n", static_cast<int>(umq_trans_mode));
        }
    }
#elif defined(URMA_DLOPEN_BACKEND_ENABLED)
    (void)umq_trans_mode;
    if (!UrmaApi::IsLoaded()) {
        return;
    }

    std::lock_guard<std::mutex> lock(gTpPerfSeqMutex);

    char perfBuf[4096] = {};
    uint32_t perfLen = sizeof(perfBuf);
    auto ret = UrmaApi::urma_get_perf_info(perfBuf, &perfLen);
    if (ret == URMA_SUCCESS && perfLen > 0) {
        uint64_t retryCount = 0;
        if (TryGetRetryCount(perfBuf, perfLen, retryCount)) {
            mReTxCount.store(retryCount);
        }
    }
#endif
}
