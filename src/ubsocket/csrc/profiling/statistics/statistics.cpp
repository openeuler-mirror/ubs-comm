/*
 *Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 *Description: Provide the statistics utility for ubsocket, etc
 *Author:
 *Create: 2025-07-16
 *Note:
 *History: 2025-07-16
 */
#include "statistics.h"

#ifdef URMA_DLOPEN_BACKEND_ENABLED
#include "under_api/urma/dl_urma_api.h"
#endif

uint32_t Statistics::Recorder::m_title_len = 0;
volatile bool Statistics::GlobalStatsMgr::m_running = true;

/* TryGetRetryCount: pure text parsing, no backend dependency.
 * URMA (urma_get_perf_info) returns perf info text containing "retry_count: N",
 * this helper parses it. */
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

void Statistics::StatsMgr::UpdateReTxCount(const UbsTransMode trans_mode)
{
    (void)trans_mode;
#ifdef URMA_DLOPEN_BACKEND_ENABLED
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
