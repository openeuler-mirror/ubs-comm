/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_KEEP_ALIVE_MANAGER_H
#define UBS_COMM_KEEP_ALIVE_MANAGER_H

#include <semaphore.h>
#include <atomic>
#include <cstdint>
#include <thread>

namespace ock {
namespace ubs {
namespace urma {

struct UrmaAliveConf {
    uint32_t keepalive_time = 0;         // 连接空闲多久开始探测
    uint32_t keepalive_intvl = 75;       // 两次探测间隔
    uint32_t keepalive_probes = 1;       // 最多发多少次探测
};

class UrmaAliveInfo : public Referable {
public:
    UrmaAliveInfo()
    {
        uint64_t now = ubsocket_get_timeNs();
        __atomic_store_n(&last_active_time_ns_, now, __ATOMIC_RELEASE);
        __atomic_store_n(&last_probes_sent_ns_, now, __ATOMIC_RELEASE);
    }

    void UpdateActiveTime()
    {
        __atomic_store_n(&last_active_time_ns_, ubsocket_get_timeNs(), __ATOMIC_RELEASE);
    }

    void IncProbesSent()
    {
        __atomic_add_fetch(&probes_sent_, 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&last_probes_sent_ns_, ubsocket_get_timeNs(), __ATOMIC_RELEASE);
    }

    void ResetProbes()
    {
        __atomic_store_n(&probes_sent_, 0u, __ATOMIC_RELEASE);
    }

    uint64_t GetLastActiveTime() const
    {
        return __atomic_load_n(&last_active_time_ns_, __ATOMIC_ACQUIRE);
    }

    uint64_t GetProbesSendTime() const
    {
        return __atomic_load_n(&last_probes_sent_ns_, __ATOMIC_ACQUIRE);
    }

    uint32_t GetProbesSent() const
    {
        return __atomic_load_n(&probes_sent_, __ATOMIC_ACQUIRE);
    }

private:
    /** 最后一次成功收发包的时间戳(纳秒) */
    alignas(64) uint64_t last_active_time_ns_{0};

    /** 最后一次发送探测包时间 */
    alignas(64) uint64_t last_probes_sent_ns_{0};

    /** 已发送探测包次数 */
    uint32_t probes_sent_{0};
};
using UrmaAliveInfoPtr = Ref<UrmaAliveInfo>;

/**
 * @brief keep_alive 管理器
 *
 * 负责：
 * 1. 定时检查所有连接的 last_active_time_ns，超时则发送探测包
 * 2. 收到请求包后回送响应包
 * 3. 收到响应包后更新 UrmaAliveInfo
 */
class UrmaAliveManager : public Referable {
public:
    UrmaAliveManager() = default;
    ~UrmaAliveManager() override
    {
        UnInit();
    }

    Result Init(const UrmaAliveConf &options_);

    void UnInit();

    bool KeepAliveEnable();

    void HandleAckPacket(Socket *socket);

    Result SendResponsePacket(Socket *socket);

private:
    Result SendProbePacket(Socket *socket);
    void RunInThread();

private:
    std::thread mWorkerThread;
    std::atomic<bool> mRunning{false};
    UrmaAliveConf options;
    uint32_t mIntervalMs{1000};
    sem_t mSem;
};
using UrmaAliveManagerPtr = Ref<UrmaAliveManager>;

} // namespace urma
} // namespace ubs
} // namespace ock
#endif // UBS_COMM_KEEP_ALIVE_MANAGER_H
