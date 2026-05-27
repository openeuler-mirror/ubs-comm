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
#ifndef UBS_COMM_UBSOCKET_SCOPE_EXIT_H
#define UBS_COMM_UBSOCKET_SCOPE_EXIT_H

#include <utility>

namespace ock {
namespace ubs {
/// ScopeExit 主要功能为作用域退出时执行一些动作, 常用于清理
template <typename F>
class ScopeExit final {
public:
    ScopeExit(F f, bool active) : m_holder(std::move(f), active) {}

    ScopeExit(ScopeExit &&) noexcept = default;
    ScopeExit &operator=(ScopeExit &&) noexcept = delete;

    ~ScopeExit()
    {
        if (Active()) {
            m_holder();
        }
    }

    void Deactivate()
    {
        m_holder.m_active = false;
    }

    bool Active() const
    {
        return m_holder.m_active;
    }

private:
    struct FuncHolder : F {
        FuncHolder(F f, bool active) : F(std::move(f)), m_active(active) {}

        FuncHolder(FuncHolder &&rhs) noexcept : F(std::move(rhs)), m_active(rhs.m_active)
        {
            rhs.m_active = false;
        }

        FuncHolder &operator=(FuncHolder &&) noexcept = delete;

        bool m_active;
    };

    FuncHolder m_holder;
};

template <typename F>
auto MakeScopeExit(F f, bool active = true) -> ScopeExit<F>
{
    return ScopeExit<F>(std::move(f), active);
}

} // namespace ubs
} // namespace ock
#endif // UBS_COMM_UBSOCKET_SCOPE_EXIT_H
