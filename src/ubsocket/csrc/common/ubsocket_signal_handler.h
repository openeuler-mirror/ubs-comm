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
#ifndef UBS_COMM_UBSOCKET_SIGNAL_HANDLER_H
#define UBS_COMM_UBSOCKET_SIGNAL_HANDLER_H

namespace ock {
namespace ubs {
/* 信号处理器入口 — async-signal-safe，仅设置标志位。
 * 实际的 ObjectStatistics dump 由 ConsumeDumpRequest() 在普通线程上下文中完成。 */
void ubsocket_handle_signal(int signal);

/* 检查并消费 SIGUSR2 dump 请求标志。
 * 返回 true 表示自上次调用以来收到过 SIGUSR2，同时清除标志；
 * 返回 false 表示无待处理请求。
 * 在普通线程（非信号处理器）上下文中调用。 */
bool ConsumeDumpRequest() noexcept;
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UBSOCKET_SIGNAL_HANDLER_H
