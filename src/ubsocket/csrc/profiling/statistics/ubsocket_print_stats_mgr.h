/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Description: Provide statistic result print class
 * Create: 2026
 */

#ifndef UBSOCKET_PRINT_STATS_MGR_H
#define UBSOCKET_PRINT_STATS_MGR_H

#include <deque>
#include <fcntl.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_logger.h"
#include "common/ubsocket_obj_statistics.h"
#include "common/ubsocket_signal_handler.h"
#include "statistics_statsmgr.h"

namespace Statistics {

/* Forward declaration: implemented in statistics.cpp, declared in statistics.h */
void ExportStatsSnapshot(const std::string &path, uint32_t pid, uint64_t perFileThresholdMB);
void ExportQbufPoolStats(const std::string &path, uint32_t pid, uint64_t perFileThresholdMB);

class PrintStatsMgr {
public:
    static ALWAYS_INLINE PrintStatsMgr *GetPrintStatsMgr()
    {
        static PrintStatsMgr mgr;
        return &mgr;
    }

    void ProcessStats()
    {
        std::ostringstream oss;
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        StatsMgr::UpdateReTxCount(mgr->m_trans_mode);
        StatsMgr::AggregatedStats agg;
        StatsMgr::AggregatePerSocketStats(agg);
        StatsMgr::OutputAllStats(oss, mgr->pidVal, agg);
        mgr->OutputJSON(oss);
        ExportStatsSnapshot(mgr->ubsocketTraceFilePath, mgr->pidVal, mgr->ubsocketPerFileThreshold);
        mgr->ExportQbufPoolStatsTick();
    }

    /* qbuf 池统计周期落盘：固定 30s 一次（独立于 UBSOCKET_MONITOR_INTERVAL 周期），
     * 与 KPI/统计快照共用同一后台线程和落盘路径（ubsocket_qbuf.txt）。
     * 仅由事件循环线程调用，无需加锁。 */
    void ExportQbufPoolStatsTick()
    {
        constexpr std::chrono::seconds kInterval(30);
        auto now = std::chrono::steady_clock::now();
        if (now - m_last_qbuf_export_ < kInterval) {
            return;
        }
        m_last_qbuf_export_ = now;
        ExportQbufPoolStats(ubsocketTraceFilePath, pidVal, ubsocketPerFileThreshold);
    }

    static void PrintStatsMgrEventLoop()
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        while (true) {
            {
                std::unique_lock<std::mutex> lock(mgr->m_mutex);
                if (!mgr->m_running) {
                    break;
                }
            }
            if (mgr->m_trace_active_) {
                mgr->ProcessStats();
            }
            mgr->DrainExternalLines();  // 在锁外执行，避免递归加锁
            /* SIGUSR2 dump 请求：信号处理器只设标志位（async-signal-safe），
             * 实际的 ObjectStatistics dump 在此普通线程上下文完成 */
            if (ConsumeDumpRequest()) {
                UBS_SLOG_ERR(ObjectStatistics::Instance().DumpStr());
            }
            std::unique_lock<std::mutex> lock(mgr->m_mutex);
            mgr->m_cv.wait_for(lock, std::chrono::seconds(mgr->ubsocketTraceTime),
                               [mgr] { return !mgr->m_running || !mgr->m_ext_lines_.empty(); });
        }
        mgr->DrainExternalLines();  // 退出前刷净
    }

    static void StartStatsCollection(uint64_t traceTime, const std::string &tracePath, uint64_t diskLimitMB,
                                     const umq_trans_mode_t trans_mode = UMQ_TRANS_MODE_UB)
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        mgr->ubsocketTraceTime = traceTime;
        // 覆盖式轮转：UBSOCKET_MONITOR_FILE_SIZE 现为 KPI 落盘磁盘总占用上限（默认 40MB，范围 [10,600]MB）。
        // 落盘最多 2 个文件（1 活动 + 1 轮转槽位）：单文件 = 上限 / 2，活动文件超阈值即整文件 rename 进槽位，
        // 循环覆写最旧的槽位文件，峰值磁盘占用 = 2 * 单文件 <= 上限。磁盘占用恒有上限。
        {
            constexpr uint32_t kFileCount = 2;  // 1 活动文件 + 1 轮转槽位，固定最多 2 个文件
            uint64_t budgetMB = (diskLimitMB > 0 ? diskLimitMB : 1);  // 磁盘总上限（MB）
            uint64_t perFileMB = budgetMB / kFileCount;               // 单文件轮转大小由上限推导
            if (perFileMB < 1) perFileMB = 1;
            mgr->ubsocketPerFileThreshold = perFileMB;  // 生效的单文件轮转上限（由磁盘上限推导）
            mgr->m_archive_count_ = kFileCount - 1;  // 轮转槽位数（=1）
            mgr->m_archive_idx_ = 0;
        }
        mgr->pidVal = static_cast<uint32_t>(getpid());
        mgr->m_trans_mode = trans_mode;

        if (!tracePath.empty()) {
            mgr->ubsocketTraceFilePath = tracePath;
        } else {
            mgr->ubsocketTraceFilePath = "/tmp/ubsocket/log";
        }

        mgr->CreateDirectory(mgr->ubsocketTraceFilePath);

        std::lock_guard<std::mutex> lock(mgr->m_mutex);
        mgr->m_trace_active_ = true;
        if (mgr->m_event_loop == nullptr) {
            mgr->m_running = true;
            mgr->m_event_loop = new std::thread(PrintStatsMgrEventLoop);
        }
    }

    static void StopStatsCollection()
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        std::unique_lock<std::mutex> lock(mgr->m_mutex);
        mgr->m_trace_active_ = false;
        // 不在此停线程：若 TX-STAT 仍激活，线程继续为外部落盘服务。
        if (!mgr->m_ext_active_ && mgr->m_event_loop != nullptr) {
            mgr->m_running = false;
            mgr->m_cv.notify_all();
            std::thread *t = mgr->m_event_loop;
            mgr->m_event_loop = nullptr;
            lock.unlock();
            t->join();
            delete t;
        }
    }

    // ---- TX-STAT 外部落盘接管（复用本后台线程，不新增线程） ----

    /* TX-STAT 调用：设置落盘路径与上限（打开常驻 fd，写版本头）。失败返回 false。 */
    bool SetExternalSink(const std::string &path, uint64_t maxMB) noexcept
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        std::lock_guard<std::mutex> lock(mgr->m_mutex);
        mgr->m_ext_path_ = path;
        mgr->m_ext_max_bytes_ = maxMB * 1024ULL * 1024ULL;
        size_t pos = path.find_last_of('/');
        if (pos != std::string::npos) {
            mgr->CreateDirectory(path.substr(0, pos));
        }
        if (mgr->m_ext_fd_ >= 0) {
            ::close(mgr->m_ext_fd_);
        }
        mgr->m_ext_fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
        if (mgr->m_ext_fd_ < 0) {
            return false;
        }
        const char *hdr = "[TX-STAT] version=1 format=key=value (post=total/ok; fail_ppm=millionths; "
                          "non-zero post.<err> and cqe.<err> buckets printed)\n";
        (void)::write(mgr->m_ext_fd_, hdr, std::strlen(hdr));
        return true;
    }

    /* TX-STAT 调用：拉起后台线程（drain-only，不打印 kpi）。线程已运行则无操作。 */
    void EnsureExternalDrain() noexcept
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        std::lock_guard<std::mutex> lock(mgr->m_mutex);
        mgr->m_ext_active_ = true;
        if (mgr->m_event_loop == nullptr) {
            mgr->m_running = true;
            mgr->m_event_loop = new std::thread(PrintStatsMgrEventLoop);
        }
    }

    /* TX-STAT 调用：提交一行（由后台线程异步写盘）。sink 未就绪则丢弃，绝不阻塞调用方。 */
    void SubmitExternalLine(const char *p, int n) noexcept
    {
        if (p == nullptr || n <= 0) {
            return;
        }
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        std::lock_guard<std::mutex> lock(mgr->m_mutex);
        if (mgr->m_ext_fd_ < 0) {
            return;
        }
        mgr->m_ext_lines_.emplace_back(p, static_cast<size_t>(n));
        mgr->m_cv.notify_all();  // 唤醒循环立即排出，低延迟
    }

    /* TX-STAT Finalize 调用：同步刷盘（确保退出前最后几行落盘）。 */
    void FlushExternal() noexcept { DrainExternalLines(); }

    /* TX-STAT Finalize 调用：停外部落盘（若 trace 也未激活则停线程）。 */
    void StopExternalDrain() noexcept
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        std::unique_lock<std::mutex> lock(mgr->m_mutex);
        mgr->m_ext_active_ = false;
        if (!mgr->m_trace_active_ && mgr->m_event_loop != nullptr) {
            mgr->m_running = false;
            mgr->m_cv.notify_all();
            std::thread *t = mgr->m_event_loop;
            mgr->m_event_loop = nullptr;
            lock.unlock();
            t->join();
            delete t;
        }
    }

private:
    PrintStatsMgr()
        : ubsocketTraceTime(ock::ubs::UBSOCKET_TRACE_TIME_DEFAULT),
          ubsocketPerFileThreshold(ock::ubs::UBSOCKET_TRACE_FILE_SIZE_DEFAULT),
          m_running(false),
          m_event_loop(nullptr),
          pidVal(0),
          m_trace_active_(false),
          m_ext_active_(false),
          m_ext_fd_(-1),
          m_ext_written_(0),
          m_ext_max_bytes_(64ULL * 1024 * 1024)
    {
        ubsocketTraceFilePath = "/tmp/ubsocket/log";
    }

    ~PrintStatsMgr()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_trace_active_ = false;
        m_ext_active_ = false;
        if (m_event_loop != nullptr) {
            m_running = false;
            m_cv.notify_all();
            std::thread *t = m_event_loop;
            m_event_loop = nullptr;
            lock.unlock();
            t->join();
            delete t;
        }
        if (m_ext_fd_ >= 0) {
            ::close(m_ext_fd_);
            m_ext_fd_ = -1;
        }
    }

    void CreateDirectory(const std::string &path)
    {
        if (path.empty()) {
            return;
        }

        constexpr mode_t DEFAULT_DIR_PERMISSION = 0750;
        std::string tmpStr = path;

        for (size_t i = 1; i < tmpStr.size(); ++i) {
            if (tmpStr[i] == '/') {
                tmpStr[i] = '\0';
                mkdir(tmpStr.c_str(), DEFAULT_DIR_PERMISSION);
                tmpStr[i] = '/';
            }
        }
        mkdir(tmpStr.c_str(), DEFAULT_DIR_PERMISSION);
    }

    /* 覆盖式轮转（最多 2 个文件）：当前 kpi 活动文件超过单文件上限时，整文件 rename 进唯一槽位
       ubsocket_kpi_<pid>.1.json，覆盖“最旧”槽位；下一轮再转回同一槽位循环覆写。单文件大小由
       StartStatsCollection 按 UBSOCKET_MONITOR_FILE_SIZE 磁盘总上限推导（= 上限 / 2）。
       磁盘占用恒 <= 2 * 单文件上限（<= 磁盘总上限），杜绝无限增长撑爆磁盘。 */
    void ArchiveJSON(const std::string &cleanPath, const uint32_t pid, const char *filename)
    {
        struct stat st;
        if (stat(filename, &st) != 0) {
            return;
        }
        uint64_t currentSize = static_cast<uint64_t>(st.st_size);
        uint64_t threshold = ubsocketPerFileThreshold * 1024ULL * 1024ULL;
        if (currentSize <= threshold || m_archive_count_ == 0) {
            return;
        }

        constexpr mode_t DEFAULT_FILE_PERMISSION = 0440;
        uint32_t slot = (m_archive_idx_ % m_archive_count_) + 1;
        ++m_archive_idx_;

        char archiveFilename[ock::ubs::UBSOCKET_TRACE_FILE_PATH_LEN_MAX] = {0};
        int ret = snprintf(archiveFilename, sizeof(archiveFilename), "%s/ubsocket_kpi_%u.%u.json",
                           cleanPath.c_str(), pid, slot);
        if (ret < 0) {
            UBS_VLOG_ERR("Failed to create archive filename for kpi json\n");
            return;
        }

        if (std::rename(filename, archiveFilename) != 0) {
            UBS_VLOG_ERR("Failed to rotate kpi json to %s\n", archiveFilename);
            return;
        }

        if (chmod(archiveFilename, DEFAULT_FILE_PERMISSION) != 0) {
            UBS_VLOG_ERR("Failed to set readonly for %s\n", archiveFilename);
            return;
        }

        UBS_VLOG_DEBUG("Rotated ubsocket kpi json: %s -> %s (size: %ld bytes)\n", filename, archiveFilename,
                       st.st_size);
    }

    void OutputJSON(std::ostringstream &oss)
    {
        const uint32_t pid = pidVal;

        char filename[ock::ubs::UBSOCKET_TRACE_FILE_PATH_LEN_MAX] = {0};
        std::string cleanPath(ubsocketTraceFilePath);

        int ret = snprintf(filename, sizeof(filename), "%s/ubsocket_kpi.json", cleanPath.c_str());
        if (ret < 0) {
            UBS_VLOG_ERR("Failed to create ubsocket kpi json.\n");
            return;
        }

        FILE *fp = fopen(filename, "a");
        if (fp) {
            fprintf(fp, "%s\n", oss.str().c_str());
            fclose(fp);
        } else {
            UBS_VLOG_ERR("Fail to open json file: %s\n", filename);
            return;
        }

        ArchiveJSON(cleanPath, pid, filename);
    }

    /* 后台线程调用：把外部队列里待写的行落盘（含 Rotate）。在 m_mutex 外调用，内部自行加锁。 */
    void DrainExternalLines() noexcept
    {
        PrintStatsMgr *mgr = GetPrintStatsMgr();
        std::deque<std::string> batch;
        {
            std::lock_guard<std::mutex> lock(mgr->m_mutex);
            if (mgr->m_ext_lines_.empty()) {
                return;
            }
            batch.swap(mgr->m_ext_lines_);
        }
        if (mgr->m_ext_fd_ < 0) {
            return;
        }
        for (auto &s : batch) {
            (void)::write(mgr->m_ext_fd_, s.data(), s.size());
            mgr->m_ext_written_ += s.size();
        }
        if (mgr->m_ext_written_ > mgr->m_ext_max_bytes_) {
            ::close(mgr->m_ext_fd_);
            std::string bak = mgr->m_ext_path_ + ".1";
            ::rename(mgr->m_ext_path_.c_str(), bak.c_str());
            mgr->m_ext_fd_ = ::open(mgr->m_ext_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0640);
            mgr->m_ext_written_ = 0;
        }
    }

    uint64_t ubsocketTraceTime;
    uint64_t ubsocketPerFileThreshold;
    uint32_t m_archive_count_ = 0;  // 覆盖式轮转槽位数（由 StartStatsCollection 按 UBSOCKET_MONITOR_FILE_SIZE 磁盘总上限推导），= 文件总数 - 1
    uint32_t m_archive_idx_ = 0;    // 轮转序号，取模得到被覆盖的最旧槽位
    volatile bool m_running;
    std::thread *m_event_loop;
    uint32_t pidVal;
    std::string ubsocketTraceFilePath;
    umq_trans_mode_t m_trans_mode;
    std::chrono::steady_clock::time_point m_last_qbuf_export_{};  // 上次 qbuf 池统计落盘时刻（事件循环线程私有）
    std::mutex m_mutex;
    std::condition_variable m_cv;

    /* 外部落盘（TX-STAT）状态 */
    bool m_trace_active_ = false;  // trace 是否激活（StartStatsCollection 设置）
    bool m_ext_active_ = false;    // TX-STAT 是否激活（EnsureExternalDrain 设置）
    std::deque<std::string> m_ext_lines_;
    std::string m_ext_path_;
    int m_ext_fd_ = -1;
    uint64_t m_ext_written_ = 0;
    uint64_t m_ext_max_bytes_ = 64ULL * 1024 * 1024;
};

}; // namespace Statistics

#endif