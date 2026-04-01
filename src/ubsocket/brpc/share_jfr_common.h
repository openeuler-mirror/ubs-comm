/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the utility for umq buffer, iov, etc
 * Author:
 * Create: 2026-02-06
 * Note:
 * History: 2026-02-06
*/

#ifndef SHARE_JFR_COMMON_H
#define SHARE_JFR_COMMON_H

#include <shared_mutex>
#include "umq_types.h"
#include "ub_lock_ops.h"

inline bool operator==(const umq_eid_t& a, const umq_eid_t& b)
{
    return ::memcmp(a.raw, b.raw, sizeof(a.raw)) == 0;
}

inline bool operator!=(const umq_eid_t& a, const umq_eid_t& b)
{
    return !(a==b);
}

namespace Brpc {
struct UmqEidHash {
    std::size_t operator()(const umq_eid_t& eid) const noexcept
    {
        uint64_t h = *reinterpret_cast<const uint64_t*>(eid.raw);
        uint64_t l = *reinterpret_cast<const uint64_t*>(eid.raw + 8);
        return std::hash<uint64_t>{}(h) ^ (std::hash<uint64_t>{}(l) << 1);
    }
};

class EidUmqTable {
public:
    static void Add(const umq_eid_t& eid, uint64_t main_umq)
    {
        EidUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        auto iter = inst->table.find(eid);
        if (iter == inst->table.end()) {
            inst->table[eid] = std::vector<uint64_t>{main_umq};
            return;
        }

        iter->second.emplace_back(main_umq);
    }

    static bool Get(const umq_eid_t& eid, std::vector<uint64_t>& out)
    {
        EidUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        auto it = inst->table.find(eid);
        if (it != inst->table.end()) {
            out = it->second;
            return true;
        }

        return false;
    }

    static void Remove(const umq_eid_t& eid)
    {
        EidUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        inst->table.erase(eid);
    }

    static void RemoveMainUmq(uint64_t main_umq)
    {
        EidUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        for (auto& [key, value] : inst->table) {
            value.erase(std::remove(value.begin(), value.end(), main_umq), value.end());
        }
    }

    static void Clean()
    {
        EidUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        inst->table.clear();
    }

    EidUmqTable(const EidUmqTable&) = delete;
    EidUmqTable& operator=(const EidUmqTable&) = delete;

private:
    EidUmqTable()
    {
        mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    }
    ~EidUmqTable()
    {
        g_external_lock_ops.destroy(mutex);
    }
    static EidUmqTable *Instance()
    {
        static EidUmqTable inst;
        return &inst;
    }

    std::unordered_map<umq_eid_t, std::vector<uint64_t>, UmqEidHash> table;
    u_external_mutex_t* mutex;
};

class SocketFdEpollTable {
public:
    static void Set(int socket_fd, int epoll_fd)
    {
        SocketFdEpollTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        inst->table[socket_fd] = epoll_fd;
    }

    static bool Get(int socket_fd, int& epoll_fd)
    {
        SocketFdEpollTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        auto it = inst->table.find(socket_fd);
        if (it != inst->table.end()) {
            epoll_fd = it->second;
            return true;
        }

        return false;
    }

    static bool Contains(int socket_fd)
    {
        SocketFdEpollTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        return inst->table.find(socket_fd) != inst->table.end();
    }

    static void Remove(int socket_fd)
    {
        SocketFdEpollTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        inst->table.erase(socket_fd);
    }

    SocketFdEpollTable(const SocketFdEpollTable&) = delete;
    SocketFdEpollTable& operator=(const SocketFdEpollTable&) = delete;

private:
    SocketFdEpollTable()
    {
        mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    }
    ~SocketFdEpollTable()
    {
        g_external_lock_ops.destroy(mutex);
    }
    static SocketFdEpollTable *Instance()
    {
        static SocketFdEpollTable inst;
        return &inst;
    }

    std::map<int, int> table;
    u_external_mutex_t* mutex;
};

class MainSubUmqTable {
public:
    static void Add(uint64_t main_umq, uint64_t sub_umq)
    {
        MainSubUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        auto iter = inst->table.find(main_umq);
        if (iter == inst->table.end()) {
            inst->table[main_umq] = std::vector<uint64_t>{sub_umq};
            return;
        }

        iter->second.emplace_back(sub_umq);
    }

    static bool GetSubUmqs(uint64_t main_umq, std::vector<uint64_t>& sub_umqs)
    {
        MainSubUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        auto it = inst->table.find(main_umq);
        if (it != inst->table.end()) {
            sub_umqs = it->second;
            return true;
        }

        return false;
    }

    static bool Contains(uint64_t main_umq)
    {
        MainSubUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        return inst->table.find(main_umq) != inst->table.end();
    }

    static void RemoveSubUmq(uint64_t main_umq, uint64_t sub_umq)
    {
        MainSubUmqTable *inst = Instance();
        ScopedUbExclusiveLocker sLock(inst->mutex);
        auto iter = inst->table.find(main_umq);
        if (iter == inst->table.end()) {
            return;
        }

        iter->second.erase(std::remove(iter->second.begin(), iter->second.end(), sub_umq), iter->second.end());
    }

    static void Clean()
    {
        decltype(table) local_table;
        {
            MainSubUmqTable *inst = Instance();
            ScopedUbExclusiveLocker sLock(inst->mutex);
            local_table = std::move(inst->table);
        }

        for (const auto &info : local_table) {
            auto main_umq = info.first;
            if (main_umq != UMQ_INVALID_HANDLE) {
                umq_state_set(main_umq, QUEUE_STATE_ERR);
            }
        }

        local_table.clear();
    }

    MainSubUmqTable(const MainSubUmqTable&) = delete;
    MainSubUmqTable& operator=(const MainSubUmqTable&) = delete;

private:
    MainSubUmqTable()
    {
        mutex = g_external_lock_ops.create(LT_EXCLUSIVE);
    }
    ~MainSubUmqTable()
    {
        g_external_lock_ops.destroy(mutex);
    }
    static MainSubUmqTable *Instance()
    {
        std::call_once(init_flag_, []() {
            instance_ = new MainSubUmqTable();
        });
        return instance_;
    }

    std::map<uint64_t, std::vector<uint64_t>> table;
    u_external_mutex_t* mutex;
    static MainSubUmqTable* instance_;
    static std::once_flag init_flag_;
};

inline MainSubUmqTable* MainSubUmqTable::instance_ = nullptr;
inline std::once_flag MainSubUmqTable::init_flag_;
}

#endif
