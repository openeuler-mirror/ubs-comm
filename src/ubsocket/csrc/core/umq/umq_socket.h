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

#include "common/ubsocket_common_includes.h"
#include "core/ubsocket_qbuf_queue.h"
#include "core/ubsocket_socket.h"
#include "core/ubsocket_socket_set.h"
#include "core/umq/umq_setting.h"
#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_umq_api.h"
#include "cli/cli_message.h"
#include "cli/statistics_statsmgr.h"

namespace ock {
namespace ubs {
namespace umq {

class UmqSocket : public SocketBase {
public:
    explicit UmqSocket(int fd) : SocketBase(fd, SocketType::SOCK_TYPE_UMQ)
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        stats_mgr_.InitStatsMgr();
    }
    ~UmqSocket() override
    {
        if (GlobalSetting::UBS_TRACE_ENABLED) {
            Statistics::StatsMgr::SubMConnCount();
            if (IsClient()) {
                Statistics::StatsMgr::SubMActiveConnCount();
            }
        }
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

    bool IsClient() { 
        return connector_->IsClient();
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

    Result AddTxEvent(const SocketPtr &sock, int epoll_fd, struct epoll_event *event) override;
    Result DelTxEvent(const SocketPtr &sock, int epoll_fd) override;
    Result AddRxEventToRunner(uintptr_t event_poll, const SocketPtr &sock, int epoll_fd,
                              struct epoll_event *event) override;
    int GetTxFd() override;


    Result CreateLocalUmq(umq_eid_t *conn_eid, umq_used_ports_t &used_ports, umq_eid_t *conn_eid_used,
                          umq_topo_type_t &topo_type);
    Result PrefillRx();
    void UnbindAndFlushRemoteUmq(const SocketPtr &sock);
    void DestroyLocalUmq();
    int AddQbuf(umq_buf_t *qbuf);
    int GetAndPopQbuf(umq_buf_t **buf, uint32_t max_buf_size);
    void FlushRxQueue();
    Result CheckDevAdd(const umq_eid_t &conn_eid);

    /* GetData Func Set For CLI*/
    virtual void OutputStats(std::ostringstream &oss);
    virtual void GetSocketCLIData(Statistics::CLISocketData *data);
    virtual void GetSocketFlowControlData(Statistics::CLIFlowControlData *data);
    virtual void GetSocketQbufPoolData(Statistics::CLIQbufPoolData *data);
    virtual void GetSocketUmqInfoData(Statistics::CLIUmqInfoData *data);
    virtual void GetSocketIoPacketData(Statistics::CLIIoPacketData *data);
    virtual void GetSocketUmqPerfData(Statistics::CLIUmqPerfData *data);

    Statistics::StatsMgr stats_mgr_ = {};

private:
    uint64_t CreateSubUmq(umq_create_option_t *cfg, umq_eid_t *local_eid);
    uint32_t getLeftPostRxNum(uint64_t umq_handle);
    uint64_t GetOrCreateMainUmq(umq_create_option_t *cfg, umq_eid_t *localEid);
    Result GetDevEid(char *dev_name, uint32_t eid_idx, umq_eid_t *eid);

    // 链接类型相关
    bool is_bonding_ = false;
    ub_trans_mode trans_mode_ = RM_TP;
    umq_topo_type_t topo_type_ = UMQ_TOPO_TYPE_FULLMESH_1D;
    // UMQ bind
    bool umq_is_bind_remote_ = false;
    // UMQ 句柄
    uint64_t umq_handle_ = UMQ_INVALID_HANDLE;

    u_mutex_t *mutex_;
    uint64_t share_umq_handle_ = UMQ_INVALID_HANDLE;

    QbufQueue<umq_buf_t *> *rxQueue = nullptr;
};
using UmqSocketPtr = Ref<UmqSocket>;

// UmqAcceptor 和 UmqConnector 共用结构体
struct CpMsg {
    uint64_t protocol_negotiation = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
    uint64_t queue_bind_info_size;
    uint8_t queue_bind_info[UMQ_BIND_INFO_SIZE_MAX];
};

struct NegotiateReq {
    uint64_t magic_number = CONTROL_PLANE_PROTOCOL_NEGOTIATION;
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
    umq_topo_type_t topo_type;
    umq_route master_route;
    umq_route back_route;
    NegotiateRoute() = default;
    NegotiateRoute(umq_topo_type_t t_type, const umq_route &m_route, const umq_route &b_route)
        : topo_type(t_type),
          master_route(m_route),
          back_route(b_route)
    {
    }
};

struct OtherRouteMessage {
    UBHandshakeState ub_handshake_state;
    umq_route_t other_route;
    umq_route_t other_back_route;
};


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
