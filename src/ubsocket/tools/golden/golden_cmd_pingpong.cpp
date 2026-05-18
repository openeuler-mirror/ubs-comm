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
#include "golden_cmd_pingpong.h"

#include "ubsocket.h"

namespace golden {
/***************************/
/* function of sub command pingpong */
void SubCommandPingpong::SetRules() noexcept
{
    param_rules_[PARAM_ROLE] = {PARAM_ROLE, PDT_STR_ENUM, true, "", "client|server", ""};
    param_rules_[PARAM_PROTOCOL] = {PARAM_PROTOCOL, PDT_STR_ENUM, true, "tcp", "tcp|ub_tp", ""};
    param_rules_[PARAM_IP] = {PARAM_IP, PDT_STR, true, "", "", ""};
    param_rules_[PARAM_PORT] = {PARAM_PORT, PDT_INT64, true, 10001L, 10000, 65535, ""};

    example_.push_back("server: " + program + " " + name_ + " --" + PARAM_ROLE + "=server --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001");
    example_.push_back("client: " + program + " " + name_ + " --" + PARAM_ROLE + "=client --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001");
}

int SubCommandPingpong::DoInitialize() noexcept
{
    role_ = param_rules_[PARAM_ROLE].strRule.value;
    protocol_ = param_rules_[PARAM_PROTOCOL].strRule.value;
    ip_ = param_rules_[PARAM_IP].strRule.value;
    port_ = param_rules_[PARAM_PORT].int64Rule.value;
    return 0;
}

int SubCommandPingpong::DoExecute() noexcept
{
    /* initialize ubsocket */
    u_init_options_t options;
    if (ubsocket_init_options(&options) != 0) {
        std::cout << "Inner error: set ubsocket options failed" << std::endl;
        return -1;
    }

    /* set options */
    options.allowed_protocol = Func::ProtocolFromString(protocol_);

    /* init ubsocket */
    if (ubsocket_init(&options) != 0) {
        std::cout << "Inner error: initialize ubsocket options failed" << std::endl;
        return -1;
    }

    /* run client and server */
    if (role_ == "client") {
        PPClient client(*this);
        return client.Run();
    } else if (role_ == "server") {
        PPServer server(*this);
        return server.Run();
    }

    std::cout << "Un-reachable path" << std::endl;
    return -1;
}

/* pp client */
int PPClient::Run()
{
    std::cout << "client run" << std::endl;
    return 0;
}

/* pp server */
int PPServer::Run()
{
    std::cout << "server run" << std::endl;
    return 0;
}
} // namespace golden