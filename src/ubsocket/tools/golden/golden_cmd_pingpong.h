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
#ifndef UBS_COMM_GOLDEN_PING_H
#define UBS_COMM_GOLDEN_PING_H

#include "golden_cmd.h"

namespace golden {
class PPClient;
class PPServer;

class SubCommandPingpong : public SubCommand {
public:
    SubCommandPingpong(const std::string &name, const ParamMap &params) : SubCommand(name, params) {}

protected:
    void SetRules() noexcept override;

    int DoInitialize() noexcept override;

    int DoExecute() noexcept override;

    friend std::ostream &operator<<(std::ostream &os, const SubCommandPingpong &o)
    {
        os << "role: " << o.role_ << ", protocol: " << o.protocol_ << ", ip: " << o.ip_ << ", port: " << o.port_;
        return os;
    }

private:
    std::string role_;
    std::string protocol_;
    std::string ip_;
    int32_t port_;

    int loop_times = 10;

    friend class PPClient;
    friend class PPServer;
};

class PPClient {
public:
    explicit PPClient(const SubCommandPingpong &cmd) : cmd_(cmd) {}
    ~PPClient();

    int Run();

private:
    const SubCommandPingpong &cmd_;
    int fd_ = -1;
};

class PPServer {
public:
    explicit PPServer(const SubCommandPingpong &cmd) : cmd_(cmd) {}
    ~PPServer();

    int Run();

private:
    const SubCommandPingpong &cmd_;
    int fd_ = -1;
    int client_fd_ = -1;
};

static SubCommand *CreatePingpong(const ParamMap &params)
{
    return new (std::nothrow) SubCommandPingpong(SUB_CMD_PINGPONG, params);
}
} // namespace golden

#endif // UBS_COMM_GOLDEN_PING_H
