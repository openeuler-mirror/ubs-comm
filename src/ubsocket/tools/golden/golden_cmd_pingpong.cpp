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

    LOG_DEBUG(*this);

    struct sockaddr_in addr;
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) <= 0) {
        std::cout << "Invalid ip '" << ip_ << "'" << std::endl;
        return -1;
    }

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
PPClient::~PPClient()
{
    {
        if (fd_ != -1) {
            close(fd_);
            fd_ = -1;
        }
    }
}

int PPClient::Run()
{
    LOG_DEBUG("start to run");

    /* step1: create socket */
    int fd = ubsocket_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("Create socket failed");
        return -errno;
    }
    LOG_DEBUG("created a socket");

    fd_ = fd;

    /* step2: connect */
    struct sockaddr_in serv_addr;
    serv_addr.sin_port = htons(cmd_.port_);
    serv_addr.sin_family = AF_INET;
    inet_pton(AF_INET, cmd_.ip_.c_str(), &serv_addr.sin_addr);
    auto result = ubsocket_connect(fd, (struct sockaddr *)&serv_addr, sizeof(struct sockaddr_in));
    if (result != 0) {
        std::cout << "Connect to '" << cmd_.ip_ << ":" << std::to_string(cmd_.port_) << "' failed\n";
        return result;
    }

    /* step3: send ping and recv pong */
    char ping[] = "ping";
    char pong[10]{};

    struct iovec send_data[1];
    send_data[0].iov_base = ping;
    send_data[0].iov_len = strlen(ping);

    struct iovec recv_data[1];
    recv_data[0].iov_base = pong;
    recv_data[0].iov_len = 10;

    int i = 0;
    while (i++ < 10) {
        result = ubsocket_writev(fd, send_data, sizeof(send_data));
        if (result != sizeof(ping)) {
            std::cout << "Write 'ping' to server failed, errno " << errno << std::endl;
            return -errno;
        }
        LOG_DEBUG("write ping successfully");

        result = ubsocket_readv(fd, recv_data, sizeof(recv_data));
        if (result < 0) {
            std::cout << "Read 'pong' to server failed, errno " << errno << std::endl;
            return -errno;
        }
        LOG_DEBUG("read pong successfully");
    }

    LOG_DEBUG("run finished");

    return 0;
}

/* pp server */
PPServer::~PPServer()
{
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }

    if (client_fd_ != -1) {
        close(client_fd_);
        client_fd_ = -1;
    }
}

int PPServer::Run()
{
    LOG_DEBUG("start to run");

    /* step1: create socket */
    int fd = ubsocket_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("Create socket failed");
        return -errno;
    }
    LOG_DEBUG("created a socket");

    fd_ = fd;

    int opt = 1;
    struct sockaddr_in address{};
    ubsocket_setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_port = htons(cmd_.port_);
    inet_pton(AF_INET, cmd_.ip_.c_str(), &address.sin_addr);

    /* step2: bind and listen */
    bind(fd_, (struct sockaddr *)&address, sizeof(address));
    auto result = ubsocket_listen(fd_, 3);
    if (result < 0) {
        std::cout << "Listen at '" << cmd_.ip_ << ":" << cmd_.port_ << "' failed, errnor " << errno;
        return -errno;
    }

    LOG_DEBUG("listen at '" << cmd_.ip_ << ":" << cmd_.port_ << "'");

    /* step3: accept one client */
    struct sockaddr_in client_address{};
    socklen_t addr_len = sizeof(address);
    client_fd_ = ubsocket_accept(fd_, (struct sockaddr *)&client_address, &addr_len);
    LOG_DEBUG("accepted one");

    /* step4: recv and send back */
    char ping[10] = "ping";
    char pong[] = "pong";

    struct iovec recv_data[1];
    recv_data[0].iov_base = ping;
    recv_data[0].iov_len = 10;

    struct iovec send_data[1];
    send_data[0].iov_base = pong;
    send_data[0].iov_len = strlen(pong);

    int i = 0;
    while (i++ < 10) {
        result = ubsocket_readv(fd, recv_data, sizeof(recv_data));
        if (result < 0) {
            std::cout << "Read 'ping' from client failed, errno " << errno << std::endl;
            return -errno;
        }
        LOG_DEBUG("read ping successfully");

        result = ubsocket_writev(fd, send_data, sizeof(send_data));
        if (result != sizeof(pong)) {
            std::cout << "Write 'pong' to client failed, errno " << errno << std::endl;
            return -errno;
        }
        LOG_DEBUG("write pong successfully");
    }

    return 0;
}
} // namespace golden