/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef OCK_RDMA_HEARTBEAT_1245609845341233_H
#define OCK_RDMA_HEARTBEAT_1245609845341233_H

#include <map>
#include <functional>
#include "common/net_common.h"

namespace ock {
namespace hcom {
using RKeepaliveConfig = struct RKeepaliveConfigStruct {
    uint32_t idleTime = 5;      // idle 5 seconds to start to probe
    uint32_t probeTimes = 7;    // probe times
    uint32_t probeInterval = 2; // probe interval
} __attribute__((packed));

using RIPConnBrokenCheckHandler = std::function<bool(int)>;
using RIPConnBrokenPostHandler = std::function<void(int)>;

class RIPDeviceHeartbeatManager {
public:
    explicit RIPDeviceHeartbeatManager(const std::string &name);
    ~RIPDeviceHeartbeatManager()
    {
        UnInitialize();
    }

    inline void SetKeepaliveConfig(uint32_t idleTime, uint32_t probeTimes, uint32_t probeInterval)
    {
        mKeepaliveConfig.idleTime = idleTime;
        mKeepaliveConfig.probeTimes = probeTimes;
        mKeepaliveConfig.probeInterval = probeInterval;
    }

    inline void SetConnBrokenCheckHandler(const RIPConnBrokenCheckHandler &value)
    {
        mConnBrokenCheckHandler = value;
    }

    inline void SetConnBrokenPostHandler(const RIPConnBrokenPostHandler &value)
    {
        mConnBrokenPostHandler = value;
    }

    NResult Initialize();

    void UnInitialize();

    NResult Start();
    void Stop();

    NResult AddNewIP(const std::string &ip, int fd);
    NResult GetFdByIP(const std::string &ip, int &fd);
    NResult RemoveIP(const std::string &ip);
    NResult RemoveByFD(int fd);

    static bool DefaultConnBrokenCheckCB(int fd);
    static void DefaultConnBrokenPostCB(int fd);

private:
    void RunInThread();
    void HandleEpollEvent(uint32_t eventCount, struct epoll_event *events);

private:
    std::string mName;
    std::map<std::string, int> mIpFdMap;
    std::map<int, std::string> mFdIpMap;
    std::mutex mMutex;
    RKeepaliveConfig mKeepaliveConfig;
    RIPConnBrokenCheckHandler mConnBrokenCheckHandler;
    RIPConnBrokenPostHandler mConnBrokenPostHandler;

    int mEpollHandle = -1;

    std::thread mWorkingThread;
    std::atomic<bool> mStarted;
    bool mNeedStop = false;
};
}
}

#endif // OCK_RDMA_HEARTBEAT_1245609845341233_H
