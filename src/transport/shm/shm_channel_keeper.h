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
#ifndef HCOM_SHM_CHANNEL_KEEPER_H
#define HCOM_SHM_CHANNEL_KEEPER_H

#include <atomic>
#include <map>

#include "hcom_def.h"
#include "shm_common.h"

namespace ock {
namespace hcom {
/*
 * Keeper message types
 */
enum ShmChKeeperMsgType : uint16_t {
    RESET_BY_PEER = 0,
    ACTIVE_CLOSE_CH = 1,
    EXCHANGE_CH_FD = 2,
    GET_MR_FD = 3,
    SEND_MR_FD = 4,
    EXCHANGE_USER_FD = 5,
};

/*
 * Keeper message header
 */
struct ShmChKeeperMsgHeader {
    uint16_t msgType = 0;  /* message type */
    uint16_t dataSize = 0; /* data size */
} __attribute__((packed));

/*
 * Callback function when received message
 */
using NewKeeperMsgHandler = std::function<void(const ShmChKeeperMsgHeader &, const ShmChannelPtr &)>;

/*
 * ShmChannelKeeper is for polling uds event including:
 * 1 reset by peer, for example peer process crashed
 * 2 close by peer actively
 * 3 exchange fd for shm files
 */
class ShmChannelKeeper {
public:
    ShmChannelKeeper(const std::string &name, uint16_t driverIndex)
        : mDriverIndex(driverIndex), mName(name + std::to_string(driverIndex))
    {
        OBJ_GC_INCREASE(ShmChannelKeeper);
    }

    ~ShmChannelKeeper()
    {
        OBJ_GC_DECREASE(ShmChannelKeeper);
    }

    HResult Start();
    void Stop();

    HResult AddShmChannel(const ShmChannelPtr &ch);
    HResult RemoveShmChannel(uint64_t id);

    inline void RegisterMsgHandler(const NewKeeperMsgHandler &handler)
    {
        mMsgHandler = handler;
    }

private:
    void StopInner();
    void RunInThread();
    void HandleEpollEvent(uint32_t eventCount, struct epoll_event *events);
    HResult ExchangeFdProcess(ShmChKeeperMsgHeader &header, const ShmChannelPtr &ch);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
private:
    int mEpollHandle = -1;
    NewKeeperMsgHandler mMsgHandler = nullptr;
    std::map<uint64_t, ShmChannelPtr> mShmChannels;

    std::mutex mMutex;
    std::mutex mChMapMutex;
    bool mStarted = false;
    bool mNeedStop = false;
    uint16_t mDriverIndex = 0;
    std::thread mEPollThread;
    std::atomic_bool mThreadStarted { false };
    std::string mName;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};
}
}

#endif // HCOM_SHM_CHANNEL_KEEPER_H
