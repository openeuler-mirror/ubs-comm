// SPDX-License-Identifier: MIT
// Copyright (c) Huawei Technologies Co., Ltd. 2026-6026. All rights reserved.

#ifndef UBSOCKET_SCOPE_EXIT_H
#define UBSOCKET_SCOPE_EXIT_H

#include <utility>

namespace ubsocket {
/// ScopeExit 主要功能为作用域退出时执行一些动作, 常用于清理
template<typename F> class ScopeExit final {
public:
    ScopeExit(F f, bool active) : m_holder(std::move(f), active)
    {
    }

    ScopeExit(ScopeExit &&) noexcept = delete;
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
        FuncHolder(F f, bool active) : F(std::move(f)), m_active(active)
        {
        }

        FuncHolder(FuncHolder &&) noexcept = delete;
        FuncHolder &operator=(FuncHolder &&) noexcept = delete;

        bool m_active;
    };

    FuncHolder m_holder;
};

template<typename F> auto MakeScopeExit(F f, bool active = true) -> ScopeExit<F>
{
    return ScopeExit<F>(std::move(f), active);
}

}  // namespace ubsocket

#endif  // UBSOCKET_SCOPE_EXIT_H
