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
#ifndef UBS_COMM_LIBC_API_HELPER_H
#define UBS_COMM_LIBC_API_HELPER_H

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstdint>
#include <mockcpp/mockcpp.hpp>

#include "under_api/dl_libc_api.h"

/**
 * libc_api_helper.h — LibcApi测试常量与sockaddr/epoll_event构造的快捷方式。
 *
 * 提供FD常量(TEST_FD/TEST_EPOLL_FD/TEST_EVENT_FD/TEST_RAW_FD)、
 * MakeTestSockAddr()(默认INADDR_LOOPBACK+port=12345)、MakeTestEpollEvent()(默认EPOLLIN)。
 *
 * LibcApi _ptr函数指针替换: variadic函数(如open)无法用mockcpp mock，
 * 通过-fno-access-control直接设置LibcApi实例的_ptr字段:
 *   LibcApi api;
 *   api.open_ptr = MockOpenSuccess;  // 自定义mock函数
 *   api.close_ptr = MockCloseSuccess;
 * 注意: _ptr初始为nullptr，未设置时调用→segfault，SetUp中必须设置或调用Load()。
 */

namespace ock {
namespace ubs {
namespace test {

constexpr int TEST_FD = 10;
constexpr int TEST_EPOLL_FD = 20;
constexpr int TEST_EVENT_FD = 30;
constexpr int TEST_RAW_FD = 40;

inline struct sockaddr_in MakeTestSockAddr(int port = 12345)
{
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return addr;
}

inline struct epoll_event MakeTestEpollEvent(int fd = TEST_FD, uint32_t events = EPOLLIN)
{
    struct epoll_event ev = {};
    ev.events = events;
    ev.data.fd = fd;
    return ev;
}

} // namespace test
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_LIBC_API_HELPER_H