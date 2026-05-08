// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
// Description: The leaky singleton pattern.

#ifndef UBSOCKET_LEAKY_SINGLETON_H_
#define UBSOCKET_LEAKY_SINGLETON_H_

#include <mutex>
#include <atomic>

namespace ubsocket {
// 重要性与解决的问题：
// 在 C++ 程序中，全局变量和静态变量的析构顺序（Static Destruction Order）是不确定的。传统的单例实现
// （如局部静态变量或全局对象）会在 main 函数结束后自动触发析构函数。
//
// 在复杂的系统中，这会引发以下严重问题：
// 1. 析构顺序冲突：如果一个全局对象 A 的析构函数依赖于单例 B，但 B 先于 A 被析构，则 A 在析构时访问
//    B 会导致未定义行为或崩溃。
// 2. 后台线程崩溃：当 main 函数返回并开始销毁全局对象时，程序可能仍有后台工作线程（Worker）在运行。
//    如果这些线程在退出前尝试访问已经被析构的单例，会导致典型的程序关闭时 coredump。
//
// LeakySingleton 的核心价值：它通过"故意泄露"的方式解决了上述问题。该模式在堆上分配单例实例（new），
// 且在整个程序生命周期内从不调用 delete。
//
// 这样做确保了：
// - 永久生命周期：单例对象在进程运行期间始终有效，直到操作系统回收整个进程的地址空间。
// - 退出安全性：无论程序何时退出，无论析构顺序如何，任何代码段、任何线程在任何时刻访问该单例都是安
//   全的，彻底杜绝了析构阶段的非法内存访问。
//
// 虽然在内存检测工具(如 Valgrind)中会被标记为泄露，但对于生命周期与进程同步的单例来说，这种权衡
// (Trade-off)是设计上故意为之，旨在换取极高的系统稳定性。
template<typename T> class LeakySingleton {
public:
    LeakySingleton() = default;

    LeakySingleton(const LeakySingleton &rhs) = delete;
    LeakySingleton &operator=(const LeakySingleton &rhs) = delete;

    // 获取单例实例，使用 std::call_once 保证多线程环境下的延迟初始化安全性
    static T &Instance()
    {
        std::call_once(m_flag, []() { m_instance.store(new T, std::memory_order_release); });
        return *m_instance.load(std::memory_order_acquire);
    }

private:
    static std::atomic<T *> m_instance;
    static std::once_flag m_flag;
};

template<typename T> std::atomic<T *> LeakySingleton<T>::m_instance{nullptr};

template<typename T> std::once_flag LeakySingleton<T>::m_flag;
}  // namespace ubsocket

#endif  // UBSOCKET_LEAKY_SINGLETON_H_
