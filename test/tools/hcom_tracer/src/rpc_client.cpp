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

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/unistd.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "rpc_client.h"
#include "htracer_log.h"
#include "htracer_utils.h"

std::string RpcClient::serverName = "udx_server";

SerCode RpcClient::SyncCall(const Message &request, Message &response)
{
    if (!MessageValidator::Validate(request)) {
        LOG_ERR("request message is invalidate");
        return SER_ERROR;
    }

    int32_t sockFd = Connect();
    if (sockFd == -1) {
        LOG_ERR("failed to connect, please check server is available");
        return SER_ERROR;
    }

    if (::send(sockFd, request.GetData(), request.GetSize(), 0) == -1) {
        LOG_ERR("failed to send message");
        ::close(sockFd);
        return SER_ERROR;
    }

    if (::recv(sockFd, response.GetData(), response.GetSize(), 0) == -1) {
        LOG_ERR("failed to receive message");
        ::close(sockFd);
        return SER_ERROR;
    }

    if (!MessageValidator::Validate(response)) {
        LOG_ERR("response message is invalidate");
        ::close(sockFd);
        return SER_ERROR;
    }
    ::close(sockFd);
    return SER_OK;
}

int32_t RpcClient::Connect()
{
    std::string abstractSockName(1, '\0');
    abstractSockName += RpcClient::serverName;

    struct sockaddr_un un;
    auto ret = memset_s(&un, sizeof(un), 0, sizeof(un));
    if (ret != 0) {
        LOG_ERR("failed to memset_s sockaddr un");
        return -1;
    }
    un.sun_family = AF_UNIX;
    ret = memcpy_s(un.sun_path, abstractSockName.length() + 1, abstractSockName.c_str(), abstractSockName.length() + 1);
    if (ret != 0) {
        LOG_ERR("failed to memcpy_s to sun_path");
        return -1;
    }

    int32_t sockFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockFd == -1) {
        LOG_ERR("failed to create connection socket");
        return -1;
    }

    if (connect(sockFd, reinterpret_cast<struct sockaddr *>(&un), sizeof(un)) < 0) {
        LOG_ERR("connect failed");
        close(sockFd);
        return -1;
    }

    return sockFd;
}