/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the probe manager
 * Author:
 * Create: 2026-04-22
 * Note:
 * History: 2026-04-22
*/

#ifndef PROBE_MANAGER_H
#define PROBE_MANAGER_H

#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "core/ubsocket_core_types.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_socket.h"
#include "profiling/statistics/cli_message.h"

#include "umq_api.h"
#include "umq_dfx_api.h"
#include "umq_pro_api.h"
#include "umq_pro_types.h"
#include "umq_types.h"

using ock::ubs::ArraySet;
using ock::ubs::Socket;
using ock::ubs::SocketPtr;
using ock::ubs::umq::UmqSetting;
using ock::ubs::umq::UmqSocket;

namespace Statistics {
// --- 配置常量 ---
static constexpr uint32_t PROBE_INTERVAL_SEC = 1;
static constexpr uint32_t PROBE_USER_DATA_ID = UmqSetting::UMQ_PROBE_USER_DATA_ID;
static constexpr uint64_t PROBE_SEM_MS_TO_NS = 1000000ULL;
static constexpr uint64_t PROBE_SEM_S_TO_NS = 1000000000ULL;

// --- dump constants ---
static constexpr const char *PROBE_DUMP_DEFAULT_PATH = "/tmp/ubsocket/probe";
static constexpr const char *PROBE_DUMP_FILE_PREFIX = "/ubsocket_probe_";
static constexpr const char *PROBE_DUMP_FILE_SUFFIX = ".log";
static constexpr const char *PROBE_DUMP_ARCHIVE_SUFFIX = ".gz";
static constexpr int64_t PROBE_DUMP_FILE_MAX_SIZE = 10 * 1024 * 1024;
static constexpr int PROBE_DUMP_MAX_ARCHIVES = 3;
static constexpr int PROBE_DUMP_SLEEP_CHUNK_MS = 10;

// --- 探测类型枚举 ---
enum ProbeType
{
    PROBE_TYPE_REQUEST = 1, // 请求包
    PROBE_TYPE_RESPONSE = 2 // 响应包
};

// --- 更新掩码定义 ---
enum ProbeUpdateMask
{
    MASK_NONE = 0x00,
    MASK_CLIENT_SEND = 0x01,
    MASK_CLIENT_RSP = 0x02,
    MASK_SERVER_RECV = 0x04,
    MASK_SERVER_RSP = 0x08,
    MASK_UMQ_CLIENT_POST = 0x10,
    MASK_UMQ_CLIENT_RECV = 0x20,
    MASK_UMQ_SERVER_RECV = 0x40,
    MASK_UMQ_SERVER_RSP = 0x80
};

/**
 * @brief 探测包头结构 (Payload)
 */
struct __attribute__((packed)) ProbeTimeInfo {
    uint32_t type;   // 探测类型
    uint32_t seq_id; // 序列号

    // --- Client Side Timestamps ---
    uint64_t client_send_time_ns;
    uint64_t client_recv_rsp_time_ns;
    uint64_t umq_client_post_time_ns;
    uint64_t umq_client_recv_time_ns;

    // --- Server Side Timestamps ---
    uint64_t server_recv_time_ns;
    uint64_t server_rsp_time_ns;
    uint64_t umq_server_recv_time_ns;
    uint64_t umq_server_rsp_time_ns;
};

/**
 * @brief 探测数据记录结构 (内存统计用)
 */
struct ProbeRecord {
    uint32_t mSockFd;
    ProbeTimeInfo mProbeInfo;
    uint64_t mLastRttNs;
    bool mIsCompleted;

    ProbeRecord() : mSockFd(0), mLastRttNs(0), mIsCompleted(false)
    {
        memset(&mProbeInfo, 0, sizeof(ProbeTimeInfo));
    }
};

/**
 * @brief 独立的探测管理器
 */
class ProbeManager {
public:
    static ProbeManager &GetInstance()
    {
        static ProbeManager instance;
        return instance;
    }

    ProbeManager(const ProbeManager &) = delete;
    ProbeManager &operator=(const ProbeManager &) = delete;

    /**
     * @brief 启动探测服务
     */
    void Start(uint32_t intervalMs, uint32_t batch, int bindCore)
    {
        if (mRunning.exchange(true))
            return;

        mIntervalMs = intervalMs;
        sem_init(&mSem, 0, 0);

        mProbeBatch = batch;     // 初始化批次
        mCurrentCursor.store(0); // 初始化游标
        mQueueSt = mQueueEd = 0;

        mCoreId = bindCore;
        mWorkerThread = std::thread(&ProbeManager::PeriodicProbe, this);

        if (mCoreId >= 0) {
            BindThreadToCore(mWorkerThread, mCoreId);
        }

        // 注册 umq 打点回调
        RegisterUmqCallbacks();

        // 启动 dump 线程
        DumpStart();

        UBS_VLOG_DEBUG("ProbeManager start probe time is %d ms, probe batch is %d\n", mIntervalMs, mProbeBatch);
    }

    void Stop()
    {
        if (!mRunning.exchange(false))
            return;

        // 先停 dump 线程（它在 DumpData 中会调 GetCLIProbeData 加 mMutex，必须在工作线程 join 之前停止）
        DumpStop();

        std::lock_guard<std::mutex> lock(mMutex);
        sem_post(&mSem);

        if (mWorkerThread.joinable()) {
            try {
                mWorkerThread.join();
            } catch (...) {
                // 防止 join 抛出异常导致后续资源无法释放
                UBS_VLOG_ERR("Exception caught during thread join\n");
            }
        }

        sem_destroy(&mSem);

        mRecords.clear();
        mQueueSt = 0;
        mQueueEd = 0;

        UBS_VLOG_DEBUG("ProbeManager is stop\n");
    }

    /**
     * @brief 2. 注册 UMQ 回调接口
     */
    void RegisterUmqCallbacks()
    {
        // 调用 umq 官方接口注册回调
        int ret = umq_io_perf_callback_register(&ProbeManager::UmqPerfCallback);
        if (ret != 0) {
            UBS_VLOG_ERR("Failed to register umq perf callback, ret: %d\n", ret);
        } else {
            UBS_VLOG_DEBUG("UMQ perf callback registered successfully\n");
        }
    }

    /**
     * @brief 3. 统一的 Buffer 更新时间戳 (Static)
     * 修改为 Static，方便在 Static 回调中直接调用
     */
    static void UpdateBuffer(ProbeTimeInfo *info, uint32_t mask)
    {
        if (!info)
            return;
        auto now_clock = std::chrono::system_clock::now();
        uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(now_clock.time_since_epoch()).count();

        if (mask & MASK_CLIENT_SEND)
            info->client_send_time_ns = now;
        if (mask & MASK_CLIENT_RSP)
            info->client_recv_rsp_time_ns = now;
        if (mask & MASK_SERVER_RECV)
            info->server_recv_time_ns = now;
        if (mask & MASK_SERVER_RSP)
            info->server_rsp_time_ns = now;

        if (mask & MASK_UMQ_CLIENT_POST)
            info->umq_client_post_time_ns = now;
        if (mask & MASK_UMQ_CLIENT_RECV)
            info->umq_client_recv_time_ns = now;
        if (mask & MASK_UMQ_SERVER_RECV)
            info->umq_server_recv_time_ns = now;
        if (mask & MASK_UMQ_SERVER_RSP)
            info->umq_server_rsp_time_ns = now;
    }

    /**
     * @brief 4. UMQ 官方回调入口 (Static)
     * 匹配 umq_io_perf_callback_t 签名
     */
    static void UmqPerfCallback(umq_perf_record_type_t record_type, umq_buf_t *qbuf)
    {
        if (!qbuf || !qbuf->buf_data)
            return;

        // --- 解析 umq_buf_pro_t ---
        umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(qbuf->qbuf_ext);
        if (buf_pro == nullptr) {
            return;
        }

        // --- 校验是否为探针包 ---
        if (buf_pro->imm.user_data != PROBE_USER_DATA_ID) {
            return;
        }

        // --- 获取 ProbeTimeInfo ---
        ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(qbuf->buf_data);

        // --- 根据 record_type 更新对应字段 ---
        if (record_type == UMQ_PERF_RECORD_TRANSPORT_POST_SEND) {
            // 发送阶段 (Post)
            if (probeInfo->type == PROBE_TYPE_REQUEST) {
                UpdateBuffer(probeInfo, MASK_UMQ_CLIENT_POST);
            } else {
                UpdateBuffer(probeInfo, MASK_UMQ_SERVER_RSP);
            }
        } else if (record_type == UMQ_PERF_RECORD_TRANSPORT_POLL_RX) {
            // 接收阶段 (Completion)
            if (probeInfo->type == PROBE_TYPE_REQUEST) {
                // 服务端收到请求
                UpdateBuffer(probeInfo, MASK_UMQ_SERVER_RECV);
            } else {
                // 客户端收到响应
                UpdateBuffer(probeInfo, MASK_UMQ_CLIENT_RECV);
            }
        }
    }

    /**
     * @brief 发送探测请求包 (Client 端)
     */
    int SendProbePacket(UmqSocket *sockObj)
    {
        uint32_t fd = sockObj->Fd();

        size_t payloadSize = UmqSetting::GetIOBufSize();
        if (payloadSize < sizeof(ProbeTimeInfo)) {
            payloadSize = sizeof(ProbeTimeInfo);
        }

        umq_buf_t *buf = umq_buf_alloc(payloadSize, 1, UMQ_INVALID_HANDLE, nullptr);
        if (buf == nullptr) {
            UBS_VLOG_ERR("Failed to allocate probe buffer\n");
            return -1;
        }

        umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);
        ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(buf->buf_data);

        // --- 初始化 umq_buf_pro_t ---
        buf_pro->opcode = UMQ_OPC_SEND_IMM;
        buf_pro->flag.value = 0;
        buf_pro->flag.bs.complete_enable = 1;
        buf_pro->imm_data = 0;                                // 清零整个 imm 联合体 (含 rsvd0)
        buf_pro->imm.user_data = PROBE_USER_DATA_ID;          // 标记为探针包

        // --- 初始化 ProbeTimeInfo ---
        {
            std::lock_guard<std::mutex> lock(mMutex);
            auto &record = mRecords[fd];
            record.mSockFd = fd;
            record.mIsCompleted = false;
            record.mProbeInfo.seq_id++;

            probeInfo->type = PROBE_TYPE_REQUEST;
            probeInfo->seq_id = record.mProbeInfo.seq_id;
            // 初始化其他字段为0
            probeInfo->server_recv_time_ns = 0;
            probeInfo->umq_server_recv_time_ns = 0;
        }

        umq_buf_t *bad_qbuf = nullptr;
        uint64_t umqh = sockObj->UmqHandle();
        // 记录应用层发送时间
        UpdateBuffer(probeInfo, MASK_CLIENT_SEND);
        umq_io_option_t option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                  UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
        int ret = umq_post(umqh, buf, &option, &bad_qbuf);
        if (ret != 0) {
            umq_buf_free(buf);
            return -1;
        }
        // 如果成功调用后，poll tx在brpc业务writev的时候执行，poll到buf直接释放
        return 0;
    }

    /**
     * @brief 发送探测响应包 (Server 端)
     */
    int SendResponsePacket(UmqSocket *sockObj, ProbeTimeInfo *reqProbeInfo)
    {
        size_t payloadSize = UmqSetting::GetIOBufSize();
        if (payloadSize < sizeof(ProbeTimeInfo)) {
            payloadSize = sizeof(ProbeTimeInfo);
        }

        umq_buf_t *buf = umq_buf_alloc(payloadSize, 1, UMQ_INVALID_HANDLE, nullptr);
        if (buf == nullptr)
            return -1;

        umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);
        ProbeTimeInfo *rspProbeInfo = reinterpret_cast<ProbeTimeInfo *>(buf->buf_data);

        // --- 初始化 umq_buf_pro_t ---
        buf_pro->opcode = UMQ_OPC_SEND_IMM;
        buf_pro->flag.value = 0;
        buf_pro->flag.bs.complete_enable = 1;
        buf_pro->user_ctx = 0;
        buf_pro->imm.user_data = PROBE_USER_DATA_ID; // 标记为探针包

        // --- 初始化 ProbeTimeInfo ---
        *rspProbeInfo = *reqProbeInfo;            // 拷贝 Client 数据
        rspProbeInfo->type = PROBE_TYPE_RESPONSE; // 改为响应包

        // 记录 Server 回复时间 (应用层 + UMQ层)
        UpdateBuffer(rspProbeInfo, MASK_SERVER_RSP);
        umq_buf_t *bad_qbuf = nullptr;
        umq_io_option_t option = {UMQ_IO_OPTION_FLAG_DIRECTION, UMQ_IO_TX,
                                  UmqSetting::UMQ_IO_OPTION_DEFAULT_TP_HANDLE_IDX};
        int ret = umq_post(sockObj->UmqHandle(), buf, &option, &bad_qbuf);
        if (ret != 0) {
            UBS_VLOG_ERR("Failed to Send Probe Packet\n");
            umq_buf_free(buf);
            return -1;
        }
        return 0;
    }

    /**
     * @brief 处理接收到的包 (业务层回调)
     */
    void HandleReceivedPacket(uint32_t fd, umq_buf_t *buf)
    {
        if (!buf || !buf->buf_data)
            return;

        umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(buf->qbuf_ext);

        // 过滤非探针包
        if (buf_pro->imm.user_data != PROBE_USER_DATA_ID) {
            return;
        }

        if (!mRunning.load()) {
            UBS_VLOG_ERR("Probe Manager Not Running while recv probe packet. Did you Enable probe args?\n");
            return;
        }

        ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(buf->buf_data);

        if (probeInfo->type == PROBE_TYPE_REQUEST) {
            UpdateBuffer(probeInfo, MASK_SERVER_RECV);

            // 同步内存
            std::lock_guard<std::mutex> lock(mMutex);
            mRecvQueue[mQueueEd].mSockFd = fd;
            mRecvQueue[mQueueEd].mProbeInfo = *probeInfo;

            if (++mQueueEd == RPC_ADPT_FD_MAX) {
                mQueueEd = 0U;
            }
            sem_post(&mSem);
        } else if (probeInfo->type == PROBE_TYPE_RESPONSE) {
            UpdateBuffer(probeInfo, MASK_CLIENT_RSP);

            // --- 客户端收到响应 ---
            std::lock_guard<std::mutex> lock(mMutex);
            auto it = mRecords.find(fd);
            if (it == mRecords.end()) {
                it = mRecords.emplace(fd, ProbeRecord()).first;
            }
            ProbeRecord &record = it->second;

            uint64_t rtt = 0;
            if (probeInfo->client_recv_rsp_time_ns > probeInfo->client_send_time_ns) {
                rtt = probeInfo->client_recv_rsp_time_ns - probeInfo->client_send_time_ns;
            }

            record.mLastRttNs = rtt;
            record.mIsCompleted = true;
            record.mProbeInfo = *probeInfo;
        }
    }

    void GetCLIProbeData(std::vector<CLIProbeData> &outDataVec)
    {
        // 1. 加锁保护
        std::lock_guard<std::mutex> lock(mMutex);

        // 2. 预分配内存
        outDataVec.clear(); // 确保是空的
        outDataVec.reserve(mRecords.size());

        // 3. 遍历并填充
        for (const auto &pair : mRecords) {
            const uint32_t &fd = pair.first;
            const ProbeRecord &record = pair.second;

            CLIProbeData item;
            item.fd = static_cast<int32_t>(fd);
            item.rtt = record.mLastRttNs;

            // 拷贝时间戳信息
            const ProbeTimeInfo &info = record.mProbeInfo;
            item.client_send_time_ns = info.client_send_time_ns;
            item.client_recv_rsp_time_ns = info.client_recv_rsp_time_ns;
            item.umq_client_post_time_ns = info.umq_client_post_time_ns;
            item.umq_client_recv_time_ns = info.umq_client_recv_time_ns;

            item.server_recv_time_ns = info.server_recv_time_ns;
            item.server_rsp_time_ns = info.server_rsp_time_ns;
            item.umq_server_recv_time_ns = info.umq_server_recv_time_ns;
            item.umq_server_rsp_time_ns = info.umq_server_rsp_time_ns;

            outDataVec.push_back(item);
        }
    }

    ~ProbeManager()
    {
        Stop();
    }

private:
    ProbeManager() : mRunning(false), mIntervalMs(PROBE_INTERVAL_SEC), mCoreId(-1) {}

    void BindThreadToCore(std::thread &th, int coreId)
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(coreId, &cpuset);
        int rc = pthread_setaffinity_np(th.native_handle(), sizeof(cpu_set_t), &cpuset);
        if (rc != 0)
            UBS_VLOG_ERR("Bind core failed: %d \n", rc);
    }

    // 处理 Server 回包逻辑
    void ProcessServerQueue(uint32_t &sentCount)
    {
        if (ArraySet<Socket>::GetInstance().Size() == 0) {
            return;
        }

        while (sentCount < mProbeBatch) {
            ProbeTimeInfo info;
            SocketPtr sockRef;
            UmqSocket *sockObj = nullptr;

            {
                std::lock_guard<std::mutex> lock(mMutex);
                // 队列空，直接跳过
                if (mQueueSt == mQueueEd) {
                    break;
                }
                // 获取 fd 和 对象指针
                int fd = mRecvQueue[mQueueSt].mSockFd;
                sockRef = ArraySet<Socket>::GetInstance().GetItem(fd);
                sockObj = static_cast<UmqSocket *>(sockRef.Get());
                // 获取有效数据
                if (sockObj) {
                    info = mRecvQueue[mQueueSt].mProbeInfo;
                    if (++mQueueSt == RPC_ADPT_FD_MAX)
                        mQueueSt = 0U;
                }
            }

            if (sockObj) {
                SendResponsePacket(sockObj, &info);
                sentCount++;
            } else {
                break;
            }
        }
    }

    // 处理 Client 主动探测逻辑
    void ProcessClientProbing(uint32_t &sentCount)
    {
        if (sentCount >= mProbeBatch)
            return;

        uint32_t currentPos = mCurrentCursor.load();
        uint32_t scans = 0;
        // 限制扫描范围，防止死循环
        uint32_t maxScan = ArraySet<Socket>::GetInstance().Capacity();

        while (sentCount < mProbeBatch && scans < maxScan) {
            if (currentPos >= maxScan)
                currentPos = 0;

            UmqSocket *sockObj =
                static_cast<UmqSocket *>(ArraySet<Socket>::GetInstance().GetItem(static_cast<int>(currentPos)).Get());
            if (sockObj && sockObj->IsClient()) {
                SendProbePacket(sockObj);
                sentCount++;
                mCurrentCursor.store(currentPos + 1);
            }
            currentPos++;
            scans++;
        }
    }

    void UpdateTimespec(struct timespec *t, uint64_t ms)
    {
        t->tv_nsec += ms * PROBE_SEM_MS_TO_NS;
        t->tv_sec += t->tv_nsec / PROBE_SEM_S_TO_NS;
        t->tv_nsec %= PROBE_SEM_S_TO_NS;
    }

    // 3. 探测主函数
    void PeriodicProbe()
    {
        struct timespec exceptTime;
        // 初始化wait超时时间
        clock_gettime(CLOCK_REALTIME, &exceptTime);
        UpdateTimespec(&exceptTime, mIntervalMs);

        while (mRunning) {
            // --- 等待信号或超时 ---
            int ret = sem_timedwait(&mSem, &exceptTime);
            if (!mRunning)
                break; // 退出检查

            uint32_t sentCount = 0;

            // --- 被post唤醒，处理 Server 回包 ---
            if (ret == 0) {
                ProcessServerQueue(sentCount);
            } else if (errno == ETIMEDOUT) {
                // --- wait超时, 处理 Client 探测 ---
                ProcessClientProbing(sentCount);
                // 刷新wait的超时时间
                clock_gettime(CLOCK_REALTIME, &exceptTime);
                UpdateTimespec(&exceptTime, mIntervalMs);
            }
        }
    }

    // --- dump 线程 ---

    void DumpStart()
    {
        std::lock_guard<std::mutex> lock(mDumpStartMutex);
        if (mDumpRunning) {
            return;
        }
        mDumpRunning = true;
        mDumpThread = std::thread(&ProbeManager::DumpLoop, this);
    }

    void DumpStop()
    {
        std::lock_guard<std::mutex> lock(mDumpStartMutex);
        if (!mDumpRunning) {
            return;
        }
        mDumpRunning = false;
        if (mDumpThread.joinable()) {
            try {
                mDumpThread.join();
            } catch (...) {
                UBS_VLOG_ERR("Exception caught during dump thread join\n");
            }
        }
        if (mDumpFile.is_open()) {
            mDumpFile.close();
            mDumpDirCreated = false;
        }
    }

    void DumpLoop()
    {
        pthread_setname_np(pthread_self(), "ubs_probe_dump");

        while (mDumpRunning) {
            auto sleepDuration = std::chrono::milliseconds(mIntervalMs);
            auto chunkMs = std::chrono::milliseconds(PROBE_DUMP_SLEEP_CHUNK_MS);
            auto elapsed = std::chrono::milliseconds(0);
            while (mDumpRunning && elapsed < sleepDuration) {
                auto remaining = sleepDuration - elapsed;
                auto sleepChunk = (remaining < chunkMs) ? remaining : chunkMs;
                std::this_thread::sleep_for(sleepChunk);
                elapsed += sleepChunk;
            }
            if (!mDumpRunning) {
                break;
            }
            DumpData();
        }

        // final drain
        DumpData();
    }

    void DumpData() noexcept
    {
        std::vector<CLIProbeData> probeDataList;
        GetCLIProbeData(probeDataList);

        // server 端无探测记录，跳过 dump（与 probe 只在 client 端发起的规格一致）
        if (probeDataList.empty()) {
            return;
        }

        std::ostringstream oss;
        WriteDumpTitle(oss, static_cast<uint32_t>(probeDataList.size()));

        for (const auto &probeData : probeDataList) {
            WriteProbeRow(oss, &probeData);
            WriteProbeDetails(oss, &probeData);
        }

        if (WriteDumpData(oss) != 0) {
            UBS_VLOG_WARN("Probe dump data write skipped, please check path and permissions.\n");
            return;
        }
        UBS_VLOG_DEBUG("Probe dump thread success");
    }

    void WriteDumpTitle(std::ostringstream &oss, uint32_t sockNum)
    {
        constexpr int timeBufSize = 32;
        time_t now = time(nullptr);
        char timeBuf[timeBufSize];
        struct tm timeInfo;
        if (localtime_r(&now, &timeInfo) != nullptr) {
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeInfo);
        } else {
            timeBuf[0] = '\0';
        }
        oss << "timeStamp: " << timeBuf << "\n";
        oss << "=== Probe Statistics ===\n";
        oss << "Total Probes (SocketNum): " << sockNum << "\n\n";
        oss << "FD       | UBS RTT(ns) | Cli\xCE\x94(ns)   | Srv\xCE\x94(ns)   | "
               "UMQ RTT(ns)  | UMQ Cli\xCE\x94(ns) | UMQ Srv\xCE\x94(ns)\n";
        oss << "---------------------------------------------------------------------------------------------------"
               "-------------------------------\n";
    }

    void WriteProbeRow(std::ostringstream &oss, const CLIProbeData *probeData)
    {
        double clientDelta = static_cast<double>(probeData->client_recv_rsp_time_ns - probeData->client_send_time_ns);
        double serverDelta = static_cast<double>(probeData->server_rsp_time_ns - probeData->server_recv_time_ns);
        double ubsRtt = clientDelta - serverDelta;
        double umqClientDelta =
            static_cast<double>(probeData->umq_client_recv_time_ns - probeData->umq_client_post_time_ns);
        double umqServerDelta =
            static_cast<double>(probeData->umq_server_rsp_time_ns - probeData->umq_server_recv_time_ns);
        double umqRtt = umqClientDelta - umqServerDelta;

        oss << std::left << std::setw(8) << probeData->fd << " | "
            << std::setw(10) << std::fixed << std::setprecision(3) << ubsRtt << " | "
            << std::setw(10) << clientDelta << " | "
            << std::setw(10) << serverDelta << " | "
            << std::setw(12) << umqRtt << " | "
            << std::setw(12) << umqClientDelta << " | "
            << std::setw(12) << umqServerDelta << "\n";
    }

    void WriteProbeDetails(std::ostringstream &oss, const CLIProbeData *probeData)
    {
        oss << "  +-- [Client] ubsocket_client_send(ns): " << probeData->client_send_time_ns
            << " | ubsocket_client_recv(ns): " << probeData->client_recv_rsp_time_ns << "\n";
        oss << "  |            umq_post(ns): " << probeData->umq_client_post_time_ns
            << " | umq_recv(ns): " << probeData->umq_client_recv_time_ns << "\n";
        oss << "  +-- [Server] ubsocket_server_recv(ns): " << probeData->server_recv_time_ns
            << " | ubsocket_server_rsp(ns): " << probeData->server_rsp_time_ns << "\n";
        oss << "  |            umq_recv(ns): " << probeData->umq_server_recv_time_ns
            << " | umq_rsp(ns): " << probeData->umq_server_rsp_time_ns << "\n";
        oss << "\n";
    }

    int WriteDumpData(std::ostringstream &oss)
    {
        std::string currentPath;
        {
            std::lock_guard<std::mutex> lock(ock::ubs::GlobalSetting::ProbeDumpMutex);
            currentPath = ock::ubs::GlobalSetting::UBS_PROBE_DUMP_PATH;
        }
        if (currentPath.empty()) {
            currentPath = PROBE_DUMP_DEFAULT_PATH;
        }

        if (currentPath != mDumpLastFilePath) {
            if (mDumpFile.is_open()) {
                mDumpFile.close();
            }
            mDumpFileName.clear();
            mDumpDirCreated = false;
            mDumpLastFilePath = currentPath;
        }

        if (CreateDumpDirectory(currentPath) != 0) {
            return -1;
        }

        if (mDumpFileName.empty()) {
            std::ostringstream ossFileName;
            ossFileName << currentPath << PROBE_DUMP_FILE_PREFIX << getpid() << PROBE_DUMP_FILE_SUFFIX;
            mDumpFileName = ossFileName.str();
        }

        if (!mDumpFile.is_open()) {
            mDumpFile.open(mDumpFileName, std::ios::out | std::ios::app);
            if (!mDumpFile.is_open()) {
                UBS_VLOG_WARN("File %s open skipped, errno: %d, errmsg: %s.\n", mDumpFileName.c_str(), errno,
                              ock::ubs::Func::Error2Str(errno));
                return -1;
            }
        }

        if (mDumpFile.is_open()) {
            mDumpFile << oss.str() << std::endl;
            mDumpFile.flush();
        }

        RotateDumpFile();
        return 0;
    }

    int CreateDumpDirectory(std::string &path)
    {
        if (mDumpDirCreated) {
            return 0;
        }
        if (path.empty()) {
            path = PROBE_DUMP_DEFAULT_PATH;
        }
        constexpr mode_t DEFAULT_DIR_PERMISSION = 0750;
        std::string currentPath;
        for (char c : path) {
            currentPath += c;
            if (c == '/') {
                if (mkdir(currentPath.c_str(), DEFAULT_DIR_PERMISSION) == -1) {
                    if (errno == EEXIST) {
                        continue;
                    }
                    UBS_VLOG_WARN("File path %s creation skipped, errno: %d, errmsg: %s.\n", currentPath.c_str(),
                                  errno, ock::ubs::Func::Error2Str(errno));
                    return -1;
                }
            }
        }
        if (mkdir(path.c_str(), DEFAULT_DIR_PERMISSION) == -1 && errno != EEXIST) {
            UBS_VLOG_WARN("File path %s creation skipped, errno: %d, errmsg: %s.\n", path.c_str(), errno,
                          ock::ubs::Func::Error2Str(errno));
            return -1;
        }
        mDumpDirCreated = true;
        return 0;
    }

    bool CompressDumpFile(const std::string &filePath)
    {
        pid_t pid = fork();
        if (pid < 0) {
            UBS_VLOG_WARN("fork failed for compress, errno: %d.\n", errno);
            return false;
        }
        if (pid == 0) {
            execlp("gzip", "gzip", "-f", filePath.c_str(), nullptr);
            _exit(127);
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            UBS_VLOG_WARN("waitpid failed for compress, errno: %d.\n", errno);
            return false;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return true;
        }
        UBS_VLOG_WARN("gzip compress failed for %s, status: %d.\n", filePath.c_str(), status);
        return false;
    }

    void RotateDumpFile()
    {
        struct stat st;
        if (stat(mDumpFileName.c_str(), &st) != 0) {
            return;
        }
        if (st.st_size < PROBE_DUMP_FILE_MAX_SIZE) {
            return;
        }

        if (mDumpFile.is_open()) {
            mDumpFile.close();
        }

        if (!CompressDumpFile(mDumpFileName)) {
            unlink(mDumpFileName.c_str());
        }

        std::string archivePattern = PROBE_DUMP_FILE_PREFIX + std::to_string(getpid()) + PROBE_DUMP_FILE_SUFFIX;
        std::vector<std::string> archives;
        DIR *dir = opendir(mDumpLastFilePath.c_str());
        if (dir != nullptr) {
            struct dirent *entry = nullptr;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name(entry->d_name);
                if (name.find(archivePattern) != std::string::npos &&
                    name.size() > archivePattern.size() &&
                    name.substr(archivePattern.size()) == PROBE_DUMP_ARCHIVE_SUFFIX) {
                    archives.push_back(mDumpLastFilePath + "/" + name);
                }
            }
            closedir(dir);
        }

        std::sort(archives.begin(), archives.end(), [](const std::string &a, const std::string &b) {
            struct stat sa, sb;
            if (stat(a.c_str(), &sa) != 0) {
                return true;
            }
            if (stat(b.c_str(), &sb) != 0) {
                return false;
            }
            return sa.st_mtime < sb.st_mtime;
        });

        while (static_cast<int>(archives.size()) > PROBE_DUMP_MAX_ARCHIVES) {
            unlink(archives.front().c_str());
            archives.erase(archives.begin());
        }

        mDumpFileName.clear();
    }

private:
    std::thread mWorkerThread;
    std::atomic<bool> mRunning;
    uint32_t mIntervalMs;

    uint32_t mProbeBatch;
    std::atomic<uint32_t> mCurrentCursor;

    int mCoreId = -1;
    std::unordered_map<uint32_t, ProbeRecord> mRecords;
    std::mutex mMutex;

    sem_t mSem;
    ProbeRecord mRecvQueue[RPC_ADPT_FD_MAX];
    uint32_t mQueueSt;
    uint32_t mQueueEd;

    // --- dump members ---
    std::thread mDumpThread;
    std::atomic<bool> mDumpRunning{false};
    std::mutex mDumpStartMutex;
    std::string mDumpLastFilePath;
    bool mDumpDirCreated = false;
    std::string mDumpFileName;
    std::ofstream mDumpFile;
};
} // namespace Statistics
#endif // PROBE_MANAGER_H