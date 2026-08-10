/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_URMA_SOCKET_H
#define UBS_COMM_URMA_SOCKET_H

#include "core/ubsocket_socket.h"
#include "urma_backend.h"
#include "urma_socket_buf.h"
#include "urma_socket_types.h"
#include "urma_wrapper.h"

namespace ock {
namespace ubs {
namespace urma {
class UrmaSocket : public SocketBase {
public:
    explicit UrmaSocket(int fd) noexcept;
    ~UrmaSocket() override
    {
        UnInitialize();
    }

    Result Initialize() noexcept override;
    void UnInitialize() noexcept override;

    Result CreateUrmaResources();
    void DestroyUrmaResources();

    Result ExchangeSocketResources(int fd, UrmaSocket &urma_sock, bool send_first = true);

    UrmaJettyPtr Jetty() const
    {
        return jetty_;
    }

    bool IsBindRemote() override;

    void SetBindRemote(bool bound)
    {
        is_bind_remote_ = bound;
    }

    uint32_t GetNegotiatedVersion() const
    {
        return negotiated_version_;
    }

    void SetNegotiatedVersion(uint32_t version)
    {
        negotiated_version_ = version;
    }

    uint32_t GetPeerVersion() const
    {
        return peer_version_;
    }

    void SetPeerVersion(uint32_t version)
    {
        peer_version_ = version;
    }

    urma_eid_t GetPeerEid() const
    {
        return peer_eid_;
    }

    void SetPeerEid(const urma_eid_t &eid)
    {
        peer_eid_ = eid;
    }

    int NotifySocketReadable();
    void HandleRecvCqe(const WriteImmData &imm);
    void HandlePostWrite(const WriteImmData &imm);
    void HandleWriteInBandAck(const WriteImmData &imm);
    void HandlePreWrite(const WriteImmData &imm);
    void HandleWriteInBand(const WriteImmData &imm);
    void DataReadFinsihed(uint32_t seq_no);
    void PostEmptyRecvWorkerRecords(uint32_t count = 1);

    bool ExchangeSocketReadableReady(bool b)
    {
        return socket_readable_ready_.exchange(b, std::memory_order_acq_rel);
    }

    void SetSocketReadableReady(bool b)
    {
        socket_readable_ready_.store(b, std::memory_order_release);
    }

    void SetReachEpollEvents(uint32_t events)
    {
        reach_epoll_events_.store(events, std::memory_order_relaxed);
    }

    uint32_t FetchOrReachEpollEvents(uint32_t events)
    {
        return reach_epoll_events_.fetch_or(events, std::memory_order_relaxed);
    }

    uint32_t GetReachEpollEvents() const
    {
        return reach_epoll_events_.load(std::memory_order_relaxed);
    }

protected:
    int DoNotifyWritable() override;

private:
    UrmaJettyPtr jetty_;      /* could be dedicated jetty or shared jetty */
    uintptr_t send_buf_addr_; /* dedicate send buffer, i.e. not shared */
    uintptr_t recv_buf_addr_; /* dedicate send buffer, i.e. not shared */
    uintptr_t rmt_send_buf_addr_;
    uintptr_t rmt_recv_buf_addr_;
    uint32_t send_buf_size_;           /* size of dedicate send buffer, max size 4GB */
    uint32_t recv_buf_size_;           /* size of dedicate recv buffer, max size 4GB */
    UrmaSockBufPtr send_buf_{nullptr}; /* local/send buffer and remote buffer allocator */
    UrmaContextPtr context_{nullptr};
    UrmaJfcPtr jfc_{nullptr};
    UrmaJfsPtr jfs_{nullptr};
    UrmaJfrPtr jfr_{nullptr};
    urma_target_seg_t *local_ctrl_target_seg_{nullptr};
    urma_target_seg_t *remote_ctrl_target_seg_{nullptr};
    RemoteSegmentsPtr remote_target_segs_{nullptr};

    urma_eid_t local_eid_{};
    urma_eid_t peer_eid_{};

    size_t recv_data_size_{0};
    size_t recv_data_offset_{0};
    bool recv_data_ready_{false};

    bool is_bind_remote_{false};
    uint32_t remote_uasid_{0};
    uint32_t negotiated_version_{0};
    uint32_t peer_version_{0};
    std::atomic<uint32_t> reach_epoll_events_{0};
    std::atomic<bool> socket_readable_ready_{false}; // socket初始状态为不可读

    urma_sge_t empty_recv_sge_;
    urma_jfr_wr_t empty_recv_wr_;
    friend class UrmaAcceptorOps;
    friend class UrmaConnectorOps;
    friend class UrmaTxOps;
    friend class UrmaRxOps;
};
using UrmaSocketPtr = Ref<UrmaSocket>;

ALWAYS_INLINE int UrmaSocket::DoNotifyWritable()
{
    PROF_START(URMA_EVENT_WRITE);
    FetchOrReachEpollEvents(EPOLLOUT);
    auto ret = eventfd_write(event_fd_, 1);
    PROF_END(URMA_EVENT_WRITE, true);
    return ret;
}

} // namespace urma
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_URMA_SOCKET_H
