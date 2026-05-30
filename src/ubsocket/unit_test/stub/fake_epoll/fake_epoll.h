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
#ifndef UBS_COMM_FAKE_EPOLL_H
#define UBS_COMM_FAKE_EPOLL_H

#include <sys/epoll.h>
#include <cstdint>
#include <vector>

/**
 * fake_epoll.h — 链接时epoll/eventfd/close替换的控制API。
 *
 * fake_epoll_static库提供与libc同名的C全局函数(epoll_create1/epoll_ctl/epoll_wait/
 * epoll_pwait/eventfd/eventfd_write/eventfd_read/close)，链接器优先解析fake符号；
 * close()对非fake fd通过dlsym(RTLD_NEXT)转发到真实libc。
 *
 * 控制API(FakeEpollCtl)提供:
 * 1. 单次注入: SetNextEpollCreateReturn(fd)、SetNextEpollWaitReturn(ret)、
 *    SetNextEventfdReturn(fd)、SetNextEpollWaitEvents(events) — 每次调用注入一个返回值。
 * 2. FD管理: AllocFakeFd()/ReleaseFakeFd()/IsFakeFd(fd)/GetFdCount() — fake fd从100开始。
 * 3. 重置: Reset() — 清空所有内部状态，SetUp中调用。
 *
 * 多步epoll_wait序列场景: 当前仅支持单次SetNextEpollWaitReturn，
 * 需模拟连续返回不同结果时，可在test case中多次调用Set或扩展FakeEpollState增加序列队列。
 */

namespace ock {
namespace ubs {
namespace test {

struct FakeEpollCtl {
    static constexpr int FAKE_FD_BASE = 100;

    static void Reset();

    static void SetNextEpollWaitEvents(const std::vector<struct epoll_event> &events);

    static void SetNextEpollWaitReturn(int ret);

    static void SetNextEventfdReturn(int fd);

    static void SetNextEpollCreateReturn(int fd);

    static bool IsFakeFd(int fd);

    static int AllocFakeFd();

    static void ReleaseFakeFd(int fd);

    static int GetFdCount();

private:
    static int next_fd_;
};

} // namespace test
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_FAKE_EPOLL_H