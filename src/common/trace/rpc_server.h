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

#ifndef RPC_SERVER_H
#define RPC_SERVER_H

#include <string>
#include <stdint.h>
#include <functional>
#include "rpc_msg.h"
#include "hcom_err.h"

namespace ock {
namespace hcom {

using RequestHandler = std::function<SerCode(const Message &request, Message &response)>;
using SentResponse = std::function<void(SerCode result, Message &response)>;

class RpcServer {
public:
    RpcServer() {}

    void RegisterRequestHandler(const RequestHandler requestHandler)
    {
        mRequestHandler = requestHandler;
    }

    void RegisterSentResponse(const SentResponse sentResponse)
    {
        mSentResponse = sentResponse;
    }

    SerCode Start(const std::string &serverName);

    void Stop();

    uint16_t GetPort()
    {
        return mPort;
    }

private:
    RequestHandler mRequestHandler = nullptr;
    SentResponse mSentResponse = nullptr;
    int32_t mSockFd = -1;
    bool mRunning = true;
    uint16_t mPort = 0xFFFF;
};

}
}
#endif // RPC_SERVER_H
