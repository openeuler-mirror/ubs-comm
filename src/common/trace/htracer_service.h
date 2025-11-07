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

#ifndef HTRACE_SERVICE_H
#define HTRACE_SERVICE_H

#include <stdint.h>
#include <memory>
#include <thread>
#include <condition_variable>
#include <mutex>
#include "rpc_server.h"
#include "hcom_err.h"

namespace ock {
namespace hcom {

/*!
 * trace_service
 *      1. trace by service support
 */
class HTracerService {
public:
    int32_t StartUp(const std::string &serverName);

    void ShutDown();
private:
    SerCode HandleRequest(const Message &request, Message &response);
    void SentResponse(SerCode result, Message &response);
    
private:
    std::unique_ptr<RpcServer> mRpcServer = nullptr;
    std::condition_variable mDumpCond;
    std::mutex mDumpLock;
    volatile bool mIsRunning = false;
    static int mDumpPeriod;
    static std::string dumpDir;
    static bool mDumpEnable;
};

}
}
#endif  // HTRACE_SERVICE_H
