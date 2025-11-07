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

#include "rpc_server.h"
#include <cerrno>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/unistd.h>
#include <netinet/in.h>
#include <thread>
#include <unistd.h>
#include "rpc_msg.h"
#include "securec.h"
#include "hcom_log.h"

#define MAX_CONNECT_NUM (2)

namespace ock {
namespace hcom {
SerCode RpcServer::Start(const std::string &serverName)
{
    // create listen socket;
    mSockFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (mSockFd == -1) {
        NN_LOG_WARN("[HTRACER] failed to create sock");
        return SER_ERROR;
    }

    std::string abstractSockName(1, '\0');
    abstractSockName += serverName;

    struct sockaddr_un un;
    auto ret = memset_s(&un, sizeof(un), 0, sizeof(un));
    if (ret != 0) {
        NN_LOG_WARN("[HTRACER] failed to memset_s sockaddr un");
        close(mSockFd);
        return SER_ERROR;
    }
    un.sun_family = AF_UNIX;
    ret = memcpy_s(un.sun_path, abstractSockName.length() + 1, abstractSockName.c_str(), abstractSockName.length() + 1);
    if (ret != 0) {
        NN_LOG_WARN("[HTRACER] failed to memcpy_s to sun_path");
        close(mSockFd);
        return SER_ERROR;
    }

    if (bind(mSockFd, reinterpret_cast<struct sockaddr *>(&un), sizeof(un)) < 0) {
        NN_LOG_WARN("[HTRACER] failed to bind socket");
        close(mSockFd);
        return SER_ERROR;
    }

    if (listen(mSockFd, MAX_CONNECT_NUM) < 0) {
        NN_LOG_WARN("[HTRACER] listen failed");
        std::cout<<"failed to listen"<<std::endl;
        close(mSockFd);
        return SER_ERROR;
    }

    std::thread runThread([&] {
        while (mRunning) {
            int32_t connFd = ::accept(mSockFd, nullptr, nullptr);
            if (connFd == -1) {
                NN_LOG_WARN("[HTRACER] failed to accept connection link");
                std::cout<<"failed to accept"<<std::endl;
                return;
            }

            std::thread connThread([&, connFd] {
                // receive request
                int32_t recvBufferSize = NN_NO1024;
                char recvBuffer[recvBufferSize];
                if (::recv(connFd, recvBuffer, recvBufferSize, 0) == -1) {
                    NN_LOG_WARN("[HTRACER] failed to receive message");
                    ::close(connFd);
                    return;
                }
                Message request(recvBuffer, recvBufferSize);
                if (!MessageValidator::Validate(request)) {
                    NN_LOG_WARN("[HTRACER] message is invalidate");
                    ::close(connFd);
                    return;
                }

                // handle message
                if (mRequestHandler == nullptr) {
                    NN_LOG_WARN("[HTRACER] message handler is nullptr");
                    ::close(connFd);
                    return;
                }

                Message response(nullptr, 0);
                if (mRequestHandler(request, response) != SER_OK) {
                    NN_LOG_WARN("[HTRACER] failed to handle message");
                    ::close(connFd);
                    return;
                }
                if (!MessageValidator::Validate(response)) {
                    NN_LOG_WARN("[HTRACER] response message is invalidate");
                    if (mSentResponse != nullptr) {
                        mSentResponse(SER_ERROR, response);
                    }
                    ::close(connFd);
                    return;
                }

                // send response.
                void *sendBuffer = response.GetData();
                uint32_t sendBufferSize = response.GetSize();
                if (::send(connFd, sendBuffer, sendBufferSize, 0) == -1) {
                    NN_LOG_WARN("[HTRACER] failed to send message");
                    if (mSentResponse != nullptr) {
                        mSentResponse(SER_ERROR, response);
                    }
                    ::close(connFd);
                    return;
                }
                if (mSentResponse != nullptr) {
                    mSentResponse(SER_OK, response);
                }
                ::close(connFd);
            });
            connThread.detach();
        }
    });
    runThread.detach();
    return SER_OK;
}

void RpcServer::Stop()
{
    if (mSockFd != -1) {
        close(mSockFd);
        mSockFd = -1;
    }
    mRunning = false;
}
}
}