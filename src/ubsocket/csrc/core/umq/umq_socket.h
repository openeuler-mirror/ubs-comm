/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UMQ_SOCKET_H
#define UBS_COMM_UMQ_SOCKET_H

#include <memory>
#include <tuple>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_version.h"
#include "core/ubsocket_socket.h"
#include "core/umq/umq_bounded_seq.h"
#include "core/umq/umq_buffer_receive_queue.h"
#include "core/umq/umq_setting.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/statistics/cli_message.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "under_api/dl_umq_api.h"

namespace ock {
namespace ubs {
namespace umq {

enum class JettyAllocState
{
    IDLE,    // 空闲状态，不需要申请Jetty资源
    WAITING, // 等待Jetty资源分配
    READY    // Jetty有空闲资源，准备分配
};

using UmqSocketSeq =
    UmqSocketBoundedSequence<UmqSetting::UMQ_SOCKET_SEQ_NUM_BIT_WIDTH, uint32_t, UmqSetting::UMQ_SOCKET_SEQ_NUM_MAX>;

class UmqSocket
    : public SocketBase
    , public UmqSocketSeq {
public:
    explicit UmqSocket(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_UMQ)
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }

    ~UmqSocket() override
    {
        UnInitialize();
    }

    Result Initialize() noexcept override;
    void UnInitialize() noexcept override;

    bool IsBonding() const noexcept
    {
        return is_bonding_;
    }
    void SetBonding(bool bonding)
    {
        is_bonding_ = bonding;
    }

    uint64_t UmqHandle() const noexcept
    {
        return umq_handle_;
    }

    uint64_t ShareUmqHandle() const noexcept
    {
        return share_umq_handle_;
    }

    bool IsBindRemote() override
    {
        return umq_is_bind_remote_;
    }

    void SetBindRemote(bool bound)
    {
        umq_is_bind_remote_ = bound;
    }

    bool IsBindind() const
    {
        return is_bonding_;
    }

    void SetIsBind(bool bound)
    {
        is_bonding_ = bound;
    }

    ub_trans_mode GetTransMode() const
    {
        return trans_mode_;
    }

    void SetTransMode(ub_trans_mode mode)
    {
        trans_mode_ = mode;
    }

    umq_topo_type_t GetTopoType()
    {
        return topo_type_;
    }

    void SetTopoType(umq_topo_type_t type)
    {
        topo_type_ = type;
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

    ALWAYS_INLINE void NewRxEpollIn()
    {
        DataRxOps *ops = rx_.GetRxOps();
        if (ops->epoll_event_num_.fetch_add(1, std::memory_order_acq_rel) == 0) {
            ops->get_and_ack_event_ = true;
            ops->poll_ = true;
            ops->expect_epoll_event_num_ = 1;
        }
    }

    ALWAYS_INLINE void NewTxEpollIn()
    {
        DataTxOps *ops = tx_.GetTxOps();
        if (ops->epoll_event_num_.fetch_add(1, std::memory_order_acq_rel) == 0) {
            ops->get_and_ack_event_ = true;
            ops->expect_epoll_event_num_ = 1;
        }
    }

    ALWAYS_INLINE void SetJettyAllocState(JettyAllocState state)
    {
        jetty_alloc_state = state;
    }

    ALWAYS_INLINE JettyAllocState GetJettyAllocState() const
    {
        return jetty_alloc_state;
    }

    Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) override;
    Result DelTxEvent(const SocketPtr &sock, int epoll_fd) override;
    bool ShouldRegisterTxEvent() override;
    Result ProcessEpollEvent(struct epoll_event &event) override;
    int GetTxFd() override;

    Result UpdateRxQueueAvailNum();
    Result CreateLocalUmq(const umq_eid_t *conn_eid, umq_used_ports_t &used_ports, umq_topo_type_t &topo_type);
    void UnbindAndFlushRemoteUmq(Socket *sock);
    void DestroyLocalUmq();
    int AddQbuf(umq_buf_t *qbuf);
    int GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size);
    void FlushRxQueue();
    Result CheckDevAdd(const umq_eid_t &conn_eid);

    std::tuple<const umq_port_id_t *, std::size_t> GetUsedPorts() const;

    /* GetData Func Set For CLI*/
    virtual void OutputStats(std::ostringstream &oss);
    virtual void GetSocketCLIData(Statistics::CLISocketData *data);
    virtual void GetSocketFlowControlData(Statistics::CLIFlowControlData *data);
    virtual void GetSocketQbufPoolData(Statistics::CLIQbufPoolData *data);
    virtual void GetSocketUmqInfoData(Statistics::CLIUmqInfoData *data);
    virtual void GetSocketIoPacketData(Statistics::CLIIoPacketData *data);
    virtual void GetSocketUmqPerfData(Statistics::CLIUmqPerfData *data);

private:
    uint64_t CreateSubUmq(umq_create_option_t *cfg, umq_eid_t *local_eid);
    uint64_t GetOrCreateMainUmq(umq_create_option_t *cfg, umq_eid_t *localEid);

    // 链接类型相关
    bool is_bonding_ = false;
    ub_trans_mode trans_mode_ = RM_TP;
    umq_topo_type_t topo_type_ = UMQ_TOPO_TYPE_FULLMESH_1D;
    // 版本协商
    uint32_t negotiated_version_ = 0;
    uint32_t peer_version_ = 0;
    // UMQ bind
    bool umq_is_bind_remote_ = false;
    // UMQ 句柄
    uint64_t umq_handle_ = UMQ_INVALID_HANDLE;

    u_mutex_t *mutex_;
    uint64_t share_umq_handle_ = UMQ_INVALID_HANDLE;

    UmqBufferReceiveQueue *rxQueue = nullptr;

    JettyAllocState jetty_alloc_state = JettyAllocState::IDLE;

    // 异常 CQE 时标记这些 port 不可用
    std::unique_ptr<umq_port_id_t[]> used_ports_;
    std::size_t used_ports_num_ = 0;
};
using UmqSocketPtr = Ref<UmqSocket>;

// UmqAcceptor 和 UmqConnector 共用结构体
struct CpMsg {
    uint64_t protocol_negotiation = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    uint64_t queue_bind_info_size;
    uint8_t queue_bind_info[UMQ_BIND_INFO_SIZE_MAX];
};

struct NegotiateReq {
    ub_trans_mode trans_mode = RM_TP;
    uint8_t is_bonding = 0;
    uint8_t enable_share_jfr = 0;
    uint8_t schedule_policy = static_cast<uint8_t>(dev_schedule_policy::ROUND_ROBIN);
    umq_eid_t local_eid = {};
};

struct NegotiateRsp {
    int32_t ret_code = 0;
    int32_t aff_sock_id = 0;
    ub_trans_mode peer_trans_mode = RM_TP;
    uint8_t is_bonding = 0;
    uint8_t reserved[2] = {0};
    uint32_t socket_id_count = 0;
    uint32_t socket_ids[NEGOTIATE_SOCKET_ID_MAX_NUM] = {0};
    umq_eid_t local_eid = {};
};

struct NegotiateRoute {
    enum : uint32_t
    {
        BACK_ROUTE_MAX_NUM = 3
    };
    umq_topo_type_t topo_type;
    umq_route master_route;
    umq_route back_routes[BACK_ROUTE_MAX_NUM];
    uint32_t back_route_num{0};
    NegotiateRoute() = default;
    NegotiateRoute(umq_topo_type_t t_type, const umq_route &m_route, const std::vector<umq_route_t> &b_routes)
        : topo_type(t_type),
          master_route(m_route),
          back_route_num(std::min(static_cast<uint32_t>(b_routes.size()), static_cast<uint32_t>(BACK_ROUTE_MAX_NUM)))
    {
        for (uint32_t i = 0; i < back_route_num; ++i) {
            back_routes[i] = b_routes[i];
        }
    }
};

struct OtherRouteMessage {
    UBHandshakeState ub_handshake_state;
    umq_route_t other_route;
    umq_route_t other_back_route;
};

// BuildNegotiateReqBuffer total wire size: magic(8) + version(4) + body_len(4) + NegotiateReq
constexpr uint32_t NEGOTIATE_REQ_WIRE_SIZE =
    sizeof(uint64_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(NegotiateReq);

#ifndef EID_FMT
#define EID_FMT "%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x:%2.2x%2.2x"
#endif

#ifndef EID_RAW_ARGS
#define EID_RAW_ARGS(eid)                                                                                      \
    eid[0], eid[1], eid[2], eid[3], eid[4], eid[5], eid[6], eid[7], eid[8], eid[9], eid[10], eid[11], eid[12], \
        eid[13], eid[14], eid[15]
#endif

#ifndef EID_ARGS
#define EID_ARGS(eid) EID_RAW_ARGS((eid).raw)
#endif

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_SOCKET_H
