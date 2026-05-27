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
#ifndef UBS_COMM_GOLDEN_CMD_CONNECTING_H
#define UBS_COMM_GOLDEN_CMD_CONNECTING_H

#include "common/ubsocket_thread_pool.h"
#include "core/urma/urma_wrapper.h"
#include "golden_cmd.h"

namespace golden {
class SubCommandConnecting : public SubCommand {
public:
    SubCommandConnecting(const std::string &name, const ParamMap &params) : SubCommand(name, params) {}

protected:
    void SetRules() noexcept override;

    int DoInitialize() noexcept override;

    int DoExecute() noexcept override;

    friend std::ostream &operator<<(std::ostream &os, const SubCommandConnecting &o)
    {
        os << "device name: " << o.device_name_ << ", device eid index: " << o.device_eid_index_
           << ", role: " << o.role_ << ", protocol: " << o.protocol_ << ", ip: " << o.ip_ << ", port: " << o.port_
           << ", thread count: " << o.thread_count_;

        return os;
    }

private:
    int DoExecuteUrma() noexcept;

private:
    std::string device_name_;      /* sub part of device name */
    int32_t device_eid_index_ = 0; /* eid index */
    std::string role_;             /* client or server */
    std::string protocol_;         /* protocol */
    std::string ip_;               /* server ip */
    int32_t port_ = 0;             /* server port */
    int32_t thread_count_ = 1;     /* how many concurrent thread */

    friend class UrmaServer;
    friend class UrmaClient;
};

static SubCommand *CreateConn(const ParamMap &params)
{
    return new (std::nothrow) SubCommandConnecting(SUB_CMD_CONN, params);
}

/*********************************urma sart*******************************************/
struct UrmaExchange {
    uint32_t token = 0;
    urma_jetty_id_t raw_jetty_id{};

    friend std::ostream &operator<<(std::ostream &os, const UrmaExchange &ex)
    {
        os << "jetty id: " << ex.raw_jetty_id.id << ", token: " << ex.token;
        return os;
    }
};

class UrmaClient {
public:
    explicit UrmaClient(const SubCommandConnecting &cmd, const ock::ubs::urma::UrmaContextPtr &context)
        : cmd_(cmd),
          context_(context)
    {
    }

    int Run() noexcept;

private:
    const SubCommandConnecting &cmd_;
    const ock::ubs::urma::UrmaContextPtr context_;
    ock::ubs::ExecutorService thread_pool_;
};

class UrmaServer {
public:
    explicit UrmaServer(const SubCommandConnecting &cmd, const ock::ubs::urma::UrmaContextPtr &context)
        : cmd_(cmd),
          context_(context)
    {
    }
    ~UrmaServer()
    {
        close(socket_listen_fd_);
        socket_listen_fd_ = -1;
    }

    int Run() noexcept;

private:
    const SubCommandConnecting &cmd_;
    const ock::ubs::urma::UrmaContextPtr context_;
    ock::ubs::ExecutorService thread_pool_;
    int socket_listen_fd_ = -1;
};
/*********************************urma end*******************************************/
} // namespace golden

#endif // UBS_COMM_GOLDEN_CMD_CONNECTING_H
