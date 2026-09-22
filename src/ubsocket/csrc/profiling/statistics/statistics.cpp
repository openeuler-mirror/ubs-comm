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
#include "core/umq/umq_data_tx_ops.h"
#include "core/umq/umq_data_rx_ops.h"
#include "profiling/statistics/tx_stat_defs.h"
#include "profiling/statistics/rx_stat_defs.h"

uint32_t Statistics::Recorder::m_title_len = 0;
volatile bool Statistics::GlobalStatsMgr::m_running = true;

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

        // 检查是否转换了所有字符
        return processedCharCount > 0;
    } catch (const std::exception &e) {
        // 处理转换失败（如：非数字字符、数值溢出等）
        UBS_VLOG_ERR("Failed to parse retry_count: %s\n", e.what());
        return false;
    }
}
} // namespace

void Statistics::StatsMgr::UpdateReTxCount(const umq_trans_mode_t umq_trans_mode)
{
    std::lock_guard<std::mutex> lock(gTpPerfSeqMutex);

    int ret = 0;
    int umqTransModeInt = umq_trans_mode;

    const bool profEnabled = GlobalSetting::UBS_PROF_ENABLE;
    if (!profEnabled) {
        if (UmqApi::umq_stats_tp_perf_start(umq_trans_mode) != 0) {
            UBS_VLOG_ERR("Failed to start tp perf: umq_trans_mode=%d\n", static_cast<int>(umq_trans_mode));
            return;
        }
    }

    // umq_stats_tp_perf_info_get 不支持多次 get, 待后续完善
    char perfBuf[4096] = {};
    uint32_t perfLen = sizeof(perfBuf);
    ret = umq_stats_tp_perf_info_get(umq_trans_mode, perfBuf, &perfLen);
    if (ret == 0 && perfLen > 0) {
        uint64_t retryCount = 0;
        if (TryGetRetryCount(perfBuf, perfLen, retryCount)) {
            mReTxCount.store(static_cast<uint32_t>(retryCount));
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
}

void Statistics::StatsMgr::AggregatePerSocketStats(AggregatedStats &out)
{
    ArraySet<Socket>::GetInstance().ForEach([&out](int fd, Socket *sock) {
        if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
            sock->create_type_ == SOCK_CREATE_TYPE_LISTEN) {
            return;
        }
        CLISocketData data{};
        ((UmqSocket *)sock)->GetSocketCLIData(&data);
        out.sendPackets += data.sendPackets;
        out.receivePackets += data.recvPackets;
        out.sendBytes += data.sendBytes;
        out.receiveBytes += data.recvBytes;
        out.errorPackets += data.errorPackets;
        out.lostPackets += data.lostPackets;
    });
}

namespace {

std::string FormatTimestamp()
{
    time_t now = time(nullptr);
    struct tm tm_buf;
    char buf[32];
    if (localtime_r(&now, &tm_buf) != nullptr) {
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    } else {
        buf[0] = '\0';
    }
    return std::string(buf);
}

std::string FormatBytes(uint64_t bytes)
{
    const char *units[] = {"B", "K", "M", "G", "T"};
    int u = 0;
    double val = static_cast<double>(bytes);
    while (val >= 1024.0 && u < 4) {
        val /= 1024.0;
        ++u;
    }
    char buf[32];
    if (u == 0) {
        snprintf(buf, sizeof(buf), "%lluB", static_cast<unsigned long long>(bytes));
    } else {
        snprintf(buf, sizeof(buf), "%.2f%s", val, units[u]);
    }
    return std::string(buf);
}

void ArchiveStatsTxt(const std::string &path, uint32_t pid, const char *filename, uint64_t thresholdBytes)
{
    struct stat st;
    if (stat(filename, &st) != 0 || st.st_size <= static_cast<off_t>(thresholdBytes)) {
        return;
    }
    char archiveFilename[512] = {0};
    snprintf(archiveFilename, sizeof(archiveFilename), "%s/ubsocket_stats_%u.1.txt", path.c_str(), pid);
    if (rename(filename, archiveFilename) != 0) {
        return;
    }
    chmod(archiveFilename, 0440);
}

void ArchiveQbufTxt(const std::string &path, uint32_t pid, const char *filename, uint64_t thresholdBytes)
{
    struct stat st;
    if (stat(filename, &st) != 0 || st.st_size <= static_cast<off_t>(thresholdBytes)) {
        return;
    }
    char archiveFilename[512] = {0};
    snprintf(archiveFilename, sizeof(archiveFilename), "%s/ubsocket_qbuf_%u.1.txt", path.c_str(), pid);
    if (rename(filename, archiveFilename) != 0) {
        return;
    }
    chmod(archiveFilename, 0440);
}

void FormatField(std::ostringstream &oss, const char *val, int width)
{
    oss << std::left << std::setw(width) << val;
}

void FormatField(std::ostringstream &oss, uint64_t val, int width)
{
    oss << std::left << std::setw(width) << val;
}

void FormatField(std::ostringstream &oss, uint32_t val, int width)
{
    oss << std::left << std::setw(width) << val;
}

void FormatField(std::ostringstream &oss, int val, int width)
{
    oss << std::left << std::setw(width) << val;
}

} // anonymous namespace

namespace Statistics {

void ExportStatsSnapshot(const std::string &path, uint32_t pid, uint64_t perFileThresholdMB)
{
    if (path.empty()) {
        return;
    }

    char filename[512] = {0};
    snprintf(filename, sizeof(filename), "%s/ubsocket_stats.txt", path.c_str());

    FILE *fp = fopen(filename, "a");
    if (fp == nullptr) {
        UBS_VLOG_ERR("ExportStatsSnapshot: failed to open %s\n", filename);
        return;
    }

    std::ostringstream oss;
    oss << "==================== " << FormatTimestamp() << " ====================\n";

    /* --- StatsMgr section --- */
    uint32_t sockNum = 0;
    ArraySet<Socket>::GetInstance().ForEach([&sockNum](int fd, Socket *sock) {
        if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
            sock->create_type_ == SOCK_CREATE_TYPE_LISTEN) {
            return;
        }
        sockNum++;
    });

    CLIDataHeader header{};
    header.socketNum = sockNum;
    header.connNum = StatsMgr::GetConnCount();
    header.activeConn = StatsMgr::GetActiveConnCount();
    header.reTxCount = StatsMgr::GetReTxCount();

    /* Query UMQ pool stats */
    ArraySet<Socket>::GetInstance().ForEach([&header](int fd, Socket *sock) {
        if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
            sock->create_type_ == SOCK_CREATE_TYPE_LISTEN) {
            return;
        }
        auto *umqSock = static_cast<UmqSocket *>(sock);
        uint64_t umqHandle = umqSock->UmqHandle();
        if (umqHandle == UMQ_INVALID_HANDLE) {
            // socket() 时即注册进 ArraySet，但 umq_handle_ 要到 connect() 的 CreateSubUmq()
            // 才赋有效值；close() 拆除时 DestroyLocalUmq() 又会先置回 0。本监控线程周期性
            // 遍历快照，撞上这两种"handle=0"的窗口属正常生命周期竞态，静默跳过即可，
            // 否则 umq_stats_transport_pool_get 侧会打 "parameter invalid" ERROR 噪音日志。
            return;
        }
        umq_transport_pool_stats_t poolStats{};
        if (umq_stats_transport_pool_get(umqHandle, &poolStats) == 0) {
            header.poolTotalNum = poolStats.total_num;
            header.poolAvailableNum = poolStats.global_num + poolStats.cache_num;
            header.poolInUseNum = poolStats.in_use_num;
        }
    });

    oss << "CLI STATISTICS MONITOR\n";
    oss << "Total Sockets       : " << header.socketNum << "\n";
    oss << "Connect Calls       : " << header.connNum << "\n";
    oss << "Active Conns        : " << header.activeConn << "\n";
    oss << "ReTx Count          : " << header.reTxCount << "\n";
    oss << "Pool Total Capacity : " << header.poolTotalNum << "\n";
    oss << "Pool Available Count: " << header.poolAvailableNum << "\n";
    oss << "Pool In Use Count   : " << header.poolInUseNum << "\n\n";

    /* per-socket StatsMgr data */
    static const int SW = 8;   /* SocketFd */
    static const int CW = 19;  /* Creation Time */
    static const int RW = 17;  /* Remote Ip */
    static const int EW = 47;  /* Eid (16 bytes × 2 hex + 15 colons) */
    static const int PW = 12;  /* Packets */
    static const int BW = 10;  /* Bytes */
    static const int EPW = 13; /* Error Packets */
    static const int LPW = 12; /* Lost Packets */
    static const int BCRW = 15; /* Bigdata CtrlRcv */
    static const int BRW = 12;  /* Bigdata Read */
    static const int BCSW = 15; /* Bigdata CtrlSnd */

    oss << " ";
    FormatField(oss, "SocketFd", SW);   oss << " | ";
    FormatField(oss, "Creation Time", CW); oss << " | ";
    FormatField(oss, "Remote Ip", RW);  oss << " | ";
    FormatField(oss, "Local Eid", EW);  oss << " | ";
    FormatField(oss, "Remote Eid", EW); oss << " | ";
    FormatField(oss, "Recv Packets", PW); oss << " | ";
    FormatField(oss, "Send Packets", PW); oss << " | ";
    FormatField(oss, "Recv Bytes", BW);  oss << " | ";
    FormatField(oss, "Send Bytes", BW);  oss << " | ";
    FormatField(oss, "Error Packets", EPW); oss << " | ";
    FormatField(oss, "Lost Packets", LPW); oss << " | ";
    FormatField(oss, "Bigdata CtrlRcv", BCRW); oss << " | ";
    FormatField(oss, "Bigdata Read", BRW); oss << " | ";
    FormatField(oss, "Bigdata CtrlSnd", BCSW);
    oss << "\n";

    ArraySet<Socket>::GetInstance().ForEach([&oss](int fd, Socket *sock) {
        if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
            sock->create_type_ == SOCK_CREATE_TYPE_LISTEN) {
            return;
        }
        CLISocketData data{};
        ((UmqSocket *)sock)->GetSocketCLIData(&data);
        time_t ct = static_cast<time_t>(data.createTime);
        struct tm tm_buf;
        char timeBuf[32];
        localtime_r(&ct, &tm_buf);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

        char eidBuf[UMQ_EID_SIZE * 3];
        oss << " ";
        FormatField(oss, fd, SW);      oss << " | ";
        FormatField(oss, timeBuf, CW);  oss << " | ";
        FormatField(oss, data.remoteIp, RW); oss << " | ";
        /* Local Eid */
        {
            int pos = 0;
            for (int i = 0; i < UMQ_EID_SIZE; ++i) { pos += snprintf(eidBuf + pos, sizeof(eidBuf) - pos, "%02x", data.localEid[i]); if (i < UMQ_EID_SIZE - 1) eidBuf[pos++] = ':'; }
            eidBuf[pos] = '\0';
            FormatField(oss, eidBuf, EW);
        }
        oss << " | ";
        /* Remote Eid */
        {
            int pos = 0;
            for (int i = 0; i < UMQ_EID_SIZE; ++i) { pos += snprintf(eidBuf + pos, sizeof(eidBuf) - pos, "%02x", data.remoteEid[i]); if (i < UMQ_EID_SIZE - 1) eidBuf[pos++] = ':'; }
            eidBuf[pos] = '\0';
            FormatField(oss, eidBuf, EW);
        }
        oss << " | ";
        FormatField(oss, data.recvPackets, PW);   oss << " | ";
        FormatField(oss, data.sendPackets, PW);    oss << " | ";
        FormatField(oss, FormatBytes(data.recvBytes).c_str(), BW);  oss << " | ";
        FormatField(oss, FormatBytes(data.sendBytes).c_str(), BW);  oss << " | ";
        FormatField(oss, data.errorPackets, EPW);  oss << " | ";
        FormatField(oss, data.lostPackets, LPW);   oss << " | ";
        FormatField(oss, data.bigdataCtrlRecv, BCRW); oss << " | ";
        FormatField(oss, data.bigdataRead, BRW);    oss << " | ";
        FormatField(oss, data.bigdataCtrlSend, BCSW);
        oss << "\n";
    });
    oss << "\n";

    /* --- TxStatReporter section --- */
    oss << "CLI TX STATISTICS MONITOR\n";
    static const char *txHdr[] = {"SocketFd", "eagain_all", "eagain_part", "enobufs_all", "enobufs_part",
                                  "emlink", "timeout", "eflowctl", "no_badqbuf", "other",
                                  "cqe_rnr", "cqe_ack_timeout", "cqe_fc", "cqe_remote", "cqe_local", "cqe_other"};
    static const int txW[] = {8, 12, 13, 12, 14, 7, 8, 9, 11, 6, 8, 16, 7, 10, 9, 9};
    static const int TXNCOL = sizeof(txHdr) / sizeof(txHdr[0]);

    oss << " ";
    for (int i = 0; i < TXNCOL; ++i) { FormatField(oss, txHdr[i], txW[i]); if (i < TXNCOL - 1) oss << " | "; }
    oss << "\n";
    ArraySet<Socket>::GetInstance().ForEach([&oss](int fd, Socket *sock) {
        if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
            sock->create_type_ == SOCK_CREATE_TYPE_LISTEN) {
            return;
        }
        auto *umqSock = static_cast<UmqSocket *>(sock);
        auto *txOps = umqSock->GetUmqTxOps();
        auto *txc = txOps != nullptr ? txOps->GetTxStatCounters() : nullptr;
        oss << " ";
        FormatField(oss, fd, txW[0]);
        if (txc != nullptr) {
            for (int i = 0; i < txstat::POST_ERR_MAX; ++i) { oss << " | "; FormatField(oss, txc->post_err[i], txW[i + 1]); }
            for (int i = 0; i < txstat::CQE_ERR_MAX; ++i) { oss << " | "; FormatField(oss, txc->cqe_err[i], txW[txstat::POST_ERR_MAX + i + 1]); }
        } else {
            for (int i = 1; i < TXNCOL; ++i) { oss << " | "; FormatField(oss, static_cast<uint64_t>(0), txW[i]); }
        }
        oss << "\n";
    });
    oss << "\n";

    /* --- RxStatReporter section --- */
    oss << "CLI RX STATISTICS MONITOR\n";
    static const char *rxHdr[] = {"SocketFd", "get_event_fail", "poll_fail", "refill_alloc_fail", "refill_post_fail",
                                  "qbuf_pop_fail", "rxe_fc", "rxe_remote", "rxe_local", "rxe_ack_timeout",
                                  "rxe_rnr", "rxe_other", "dataset_no_block", "flow_ctrl_failed", "rearm_fail", "peer_closed"};
    static const int rxW[] = {8, 15, 11, 18, 18, 13, 7, 11, 10, 15, 8, 10, 16, 17, 11, 12};
    static const int RXNCOL = sizeof(rxHdr) / sizeof(rxHdr[0]);

    oss << " ";
    for (int i = 0; i < RXNCOL; ++i) { FormatField(oss, rxHdr[i], rxW[i]); if (i < RXNCOL - 1) oss << " | "; }
    oss << "\n";
    ArraySet<Socket>::GetInstance().ForEach([&oss](int fd, Socket *sock) {
        if (sock == nullptr || sock->Type() == SocketType::SOCK_TYPE_TCP ||
            sock->create_type_ == SOCK_CREATE_TYPE_LISTEN) {
            return;
        }
        auto *umqSock = static_cast<UmqSocket *>(sock);
        auto *rxOps = umqSock->GetUmqRxOps();
        auto *rxc = rxOps != nullptr ? rxOps->GetRxStatCounters() : nullptr;
        oss << " ";
        FormatField(oss, fd, rxW[0]);
        if (rxc != nullptr) {
            for (int i = 0; i < rxstat::RX_POLL_ERR_MAX; ++i) { oss << " | "; FormatField(oss, rxc->poll_err[i], rxW[i + 1]); }
            for (int i = 0; i < rxstat::RXCQE_ERR_MAX; ++i) { oss << " | "; FormatField(oss, rxc->cqe_err[i], rxW[rxstat::RX_POLL_ERR_MAX + i + 1]); }
            for (int i = 0; i < rxstat::RX_DATASET_ERR_MAX; ++i) { oss << " | "; FormatField(oss, rxc->dataset_err[i], rxW[rxstat::RX_POLL_ERR_MAX + rxstat::RXCQE_ERR_MAX + i + 1]); }
        } else {
            for (int i = 1; i < RXNCOL; ++i) { oss << " | "; FormatField(oss, static_cast<uint64_t>(0), rxW[i]); }
        }
        oss << "\n";
    });
    oss << "\n";

    fprintf(fp, "%s", oss.str().c_str());
    fclose(fp);

    ArchiveStatsTxt(path, pid, filename, perFileThresholdMB * 1024ULL * 1024ULL);
}

void ExportQbufPoolStats(const std::string &path, uint32_t pid, uint64_t perFileThresholdMB)
{
    if (path.empty()) {
        return;
    }

    umq_qbuf_pool_stats_t poolStats{};
    /* 直接调用 umq 池级接口取数（统计在 umq 侧完成，此处只负责记录）：
     * normal 池 + tiny 池。normal 池未初始化（进程尚无 umq 活动）时报错，跳过本轮 */
    if (umq_qbuf_pool_info_get(&poolStats) != 0) {
        UBS_VLOG_DEBUG("ExportQbufPoolStats: umq_qbuf_pool_info_get failed\n");
        return;
    }
    (void)umq_tiny_qbuf_pool_info_get(&poolStats);

    char poolBuf[32768] = {0}; /* 32KB: stats_to_str output grew with per-SC breakdown */
    if (umq_qbuf_pool_stats_to_str(&poolStats, poolBuf, sizeof(poolBuf)) <= 0) {
        return;
    }

    char filename[512] = {0};
    snprintf(filename, sizeof(filename), "%s/ubsocket_qbuf.txt", path.c_str());

    FILE *fp = fopen(filename, "a");
    if (fp == nullptr) {
        UBS_VLOG_ERR("ExportQbufPoolStats: failed to open %s\n", filename);
        return;
    }

    fprintf(fp, "==================== %s ====================\n%s\n", FormatTimestamp().c_str(), poolBuf);
    fclose(fp);

    ArchiveQbufTxt(path, pid, filename, perFileThresholdMB * 1024ULL * 1024ULL);
}

} // namespace Statistics