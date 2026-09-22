/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "ubsocket_socket_connector.h"
#include "common/ubsocket_common_includes.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "ubsocket_socket.h"

namespace ock {
namespace ubs {
// ======================== 基础方法 ========================
int Connector::Connect(const SocketPtr &sock, const struct sockaddr *address, socklen_t address_len)
{
    PROF_START(CORE_CONNECT);
    auto sockBase = RefConvert<Socket, SocketBase>(sock);
    Result ret = 0;
    if (connector_ops_ == nullptr) {
        /* 协商 ops 已随上一次建链成功释放：链路已建立，重复 connect 直接拒绝 */
        UBS_VLOG_ERR("Connect on an established socket (negotiation ops released), fd: %d\n", raw_fd_);
        errno = EISCONN;
        PROF_END(CORE_CONNECT, false);
        return -1;
    }
    if (connector_ops_ != nullptr) {
        ret = connector_ops_->PrepareConnect(raw_fd_, address, address_len, sock);
        if (ret != 0) {
            PROF_END(CORE_CONNECT, false);
            /* 方案B 在 connect 之前就预建了 umq：TCP 连不上也要当场还掉它的 id（issue #49） */
            sockBase->DiscardUnboundUmq();
            ArraySet<Socket>::GetInstance().OverrideItem(raw_fd_, nullptr);
            SocketConnHelper::FlushSocketMsg(raw_fd_);
            if (ret != -1) {
                errno = EBADE;
            }
            return -1;
        }

        ret = connector_ops_->Negotiate(raw_fd_, sock);

        if (ret == UBS_OK) {
            ret = connector_ops_->CreateSocketResources(sock);
        }
    }
    if (ret != UBS_OK) {
        if (IsDegradable(ret) && GlobalSetting::UBS_ENABLE_DEGRADE) {
            UBS_VLOG_INFO("Version mismatch, fallback to TCP, fd: %d\n", raw_fd_);
            ret = UBS_OK;
        } else {
            UBS_VLOG_ERR("Failed to establish UB connection, fd: %d\n", raw_fd_);
            ret = -1;
        }
        /* 与 accept 失败路径同理：延迟释放 + fd 复用的窗口，先清槽再摘除；
         * 从未 bind 的 umq 当场销毁，id 不随 socket 对象的寿命陪葬（issue #49） */
        sockBase->ReleaseDataPlane();
        sockBase->DiscardUnboundUmq();
        ArraySet<Socket>::GetInstance().OverrideItem(raw_fd_, nullptr);
        SocketConnHelper::FlushSocketMsg(raw_fd_);
    }
    bool is_blocking = SocketConnHelper::IsBlocking(raw_fd_);
    if (is_blocking) {
        SocketConnHelper::SetBlocking(raw_fd_);
    }

    if (RawConnInfoV4 *ci = sock->MutableConnInfo()) {
        ci->type_fd = 1;
    }
    sock->create_type_ = SOCK_CREATE_TYPE_CONNECT;

    if (ret == UBS_OK) {
        /* 建链完成（含降级 TCP 成功）：钩子内把身份/EID 收拢到 socket 本体
         * 并释放协商 ops（此后重复 connect 由入口 EISCONN 拦截） */
        sockBase->OnEstablished();
    }

#ifdef UBS_SPLIT_TRACE_ENABLED_COMPILE
    sock->TryCreateSplitTrace();
#endif

    if (GlobalSetting::UBS_MONITOR_ENABLE) {
        SocketBasePtr sockptr = RefConvert<Socket, SocketBase>(sock);
        if (sockptr != nullptr) {
            if (auto *mgr = sockptr->GetStatsMgr()) {
                mgr->UpdateTraceStats(Statistics::StatsMgr::CONN_COUNT, 1);
                mgr->UpdateTraceStats(Statistics::StatsMgr::ACTIVE_OPEN_COUNT, 1);
            }
        } else {
            UBS_VLOG_DEBUG("socket is Null, no UpdateTraceStats!");
        }
    }

    if (ret == -1 && errno == 0) {
        errno = EBADE;
    }
    PROF_END(CORE_CONNECT, !ret);
    return ret;
}

Connector::~Connector()
{
    /* CONN_COUNT/ACTIVE_CONN_COUNT 的递减已统一收敛到 SocketBase 析构，
     * 此处不再单独递减，避免与内嵌 acceptor_ 造成双重递减。 */
}
} // namespace ubs
} // namespace ock
