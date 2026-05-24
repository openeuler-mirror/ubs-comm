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
#ifndef HCOM_UMQ_EID_TABLE_H
#define HCOM_UMQ_EID_TABLE_H

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_leaky_singleton.h"
#include "common/ubsocket_lock.h"
#include "under_api/umq_api.h"

namespace ock {
namespace ubs {
namespace umq {
struct UmqEidHash {
    std::size_t operator()(const umq_eid_t &eid) const noexcept
    {
        uint64_t h = *reinterpret_cast<const uint64_t *>(eid.raw);
        uint64_t l = *reinterpret_cast<const uint64_t *>(eid.raw + 8);
        return std::hash<uint64_t>{}(h) ^ (std::hash<uint64_t>{}(l) << 1);
    }
};
struct UmqEidEqual {
    bool operator()(const umq_eid_t &a, const umq_eid_t &b) const
    {
        return memcmp(a.raw, b.raw, sizeof(a.raw)) == 0;
    }
};

class MainUmqState : public std::enable_shared_from_this<MainUmqState> {
public:
    MainUmqState(ub_trans_mode mode, uint64_t umqh)
        : m_ubTransMode(mode),
          m_umqh(umqh),
          m_mutex(LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE))
    {
    }

    MainUmqState(const MainUmqState &rhs) = default;
    MainUmqState &operator=(const MainUmqState &rhs) = default;

    ~MainUmqState()
    {
        LockRegistry::LOCK_OPS.destroy(m_mutex);
    }

    ub_trans_mode GetUbTransMode() const
    {
        return m_ubTransMode;
    }

    uint64_t GetUmqHandle() const
    {
        return m_umqh;
    }

    template <typename F>
    Result EnsurePrefilled(const F &f)
    {
        if (!__atomic_load_n(&m_prefilled, __ATOMIC_ACQUIRE)) {
            Locker lock(m_mutex);
            if (!__atomic_load_n(&m_prefilled, __ATOMIC_ACQUIRE)) {
                Result ret = f();
                if (ret != UBS_OK) {
                    return ret;
                }

                __atomic_store_n(&m_prefilled, true, __ATOMIC_RELEASE);
            }
        }
        return UBS_OK;
    }

private:
    ub_trans_mode m_ubTransMode;
    uint64_t m_umqh;
    u_mutex_t *m_mutex = nullptr;
    bool m_prefilled = false;
};

class UmqEidTable {
public:
    static UmqEidTable &Instance()
    {
        static UmqEidTable instance;
        return instance;
    }

    void Add(const umq_eid_t &eid, ub_trans_mode mode, uint64_t main_umq)
    {
        Locker sLock(mutex);
        auto &vec = table[eid];
        auto it = std::find_if(vec.begin(), vec.end(), [mode, main_umq](const std::shared_ptr<MainUmqState> &state) {
            return state->GetUbTransMode() == mode && state->GetUmqHandle() == main_umq;
        });
        if (it == vec.end()) {
            vec.emplace_back(std::make_shared<MainUmqState>(mode, main_umq));
        }
    }

    bool Get(const umq_eid_t &eid, std::vector<std::shared_ptr<MainUmqState>> &out)
    {
        Locker sLock(mutex);
        auto it = table.find(eid);
        if (it != table.end()) {
            out = it->second;
            return true;
        }
        return false;
    }

    bool Get(const umq_eid_t &eid, ub_trans_mode mode, std::vector<std::shared_ptr<MainUmqState>> &out)
    {
        Locker sLock(mutex);
        auto it = table.find(eid);
        if (it == table.end()) {
            return false;
        }

        std::vector<std::shared_ptr<MainUmqState>> vec;
        for (const auto &state : it->second) {
            if (state->GetUbTransMode() == mode) {
                vec.push_back(state);
            }
        }

        if (vec.empty()) {
            return false;
        }

        out = std::move(vec);
        return true;
    }

    std::shared_ptr<MainUmqState> GetFirst(const umq_eid_t &eid, ub_trans_mode mode)
    {
        Locker lock(mutex);
        auto it = table.find(eid);
        if (it == table.end()) {
            return {};
        }

        for (const auto &state : it->second) {
            if (state->GetUbTransMode() == mode) {
                return state;
            }
        }

        return {};
    }

    void Remove(const umq_eid_t &eid)
    {
        Locker sLock(mutex);
        table.erase(eid);
    }

    void RemoveMainUmq(uint64_t main_umq)
    {
        Locker sLock(mutex);
        for (auto &[key, value] : table) {
            value.erase(std::remove_if(value.begin(), value.end(),
                                       [main_umq](const std::shared_ptr<MainUmqState> &state) {
                                           return state->GetUmqHandle() == main_umq;
                                       }),
                        value.end());
        }
    }

    void Clean()
    {
        Locker sLock(mutex);
        table.clear();
    }

    u_mutex_t *GetMainMutex()
    {
        return main_mutex;
    }

private:
    UmqEidTable()
    {
        mutex = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
        main_mutex = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }
    ~UmqEidTable()
    {
        LockRegistry::LOCK_OPS.destroy(mutex);
        LockRegistry::LOCK_OPS.destroy(main_mutex);
    }

    std::unordered_map<umq_eid_t, std::vector<std::shared_ptr<MainUmqState>>, UmqEidHash, UmqEidEqual> table;
    u_mutex_t *mutex;
    u_mutex_t *main_mutex;
};

class EidRegistry : public LeakySingleton<EidRegistry> {
    friend LeakySingleton<EidRegistry>;

public:
    bool RegisterEid(const umq_eid_t &eid)
    {
        Locker sLock(mutex_);
        return registered_eids_.insert(eid).second;
    }

    bool IsRegisteredEid(const umq_eid_t &eid)
    {
        Locker sLock(mutex_);
        return registered_eids_.count(eid) > 0;
    }

    bool UnregisterEid(const umq_eid_t &eid)
    {
        Locker sLock(mutex_);
        return registered_eids_.erase(eid) > 0;
    }

    // 控制建链轮询
    // 注册或者替换index值
    void RegisterOrReplaceEidIndex(const umq_eid_t &eid, uint32_t index)
    {
        Locker sLock(mutex_);
        eid_index_map_[eid] = index;
    }

    // 仅检查eid是否存在（不获取值）
    bool IsRegisteredEidIndex(const umq_eid_t &eid) const
    {
        Locker sLock(mutex_);
        return eid_index_map_.find(eid) != eid_index_map_.end();
    }

    // 获得index值
    bool GetEidIndex(const umq_eid_t &eid, uint32_t &index) const
    {
        Locker sLock(mutex_);
        auto it = eid_index_map_.find(eid);
        if (it != eid_index_map_.end()) {
            index = it->second;
            return true;
        }
        return false;
    }

    //删除 eid 及其值
    bool UnregisterEidIndex(const umq_eid_t &eid)
    {
        Locker sLock(mutex_);
        return eid_index_map_.erase(eid) > 0;
    }

private:
    EidRegistry()
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }
    ~EidRegistry()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
    }
    u_mutex_t *mutex_;
    std::unordered_set<umq_eid_t, UmqEidHash, UmqEidEqual> registered_eids_;         // umq_dev_add eid
    std::unordered_map<umq_eid_t, uint32_t, UmqEidHash, UmqEidEqual> eid_index_map_; // bonding eidroute_list index
};

class RouteListRegistry : public LeakySingleton<RouteListRegistry> {
    friend LeakySingleton<RouteListRegistry>;

public:
    // 注册或者替换routeList值
    void RegisterOrReplaceRouteList(const umq_eid_t &eid, const umq_route_list_t &routeList)
    {
        Locker sLock(mutex_);
        route_list_map_[eid] = routeList;
    }

    // 仅检查eid对应的routeList是否存在（不获取值）
    bool IsRegisteredRouteList(const umq_eid_t &eid) const
    {
        Locker sLock(mutex_);
        return route_list_map_.find(eid) != route_list_map_.end();
    }

    bool GetRouteList(const umq_eid_t &eid, umq_route_list_t &routeList) const
    {
        Locker sLock(mutex_);
        auto it = route_list_map_.find(eid);
        if (it != route_list_map_.end()) {
            routeList = it->second;
            return true;
        }
        return false;
    }

    //  RouteList
    bool UnregisterRouteList(const umq_eid_t &eid)
    {
        Locker sLock(mutex_);
        return route_list_map_.erase(eid) > 0;
    }

private:
    RouteListRegistry()
    {
        mutex_ = LockRegistry::LOCK_OPS.create(LT_EXCLUSIVE);
    }
    ~RouteListRegistry()
    {
        LockRegistry::LOCK_OPS.destroy(mutex_);
    }
    u_mutex_t *mutex_;
    std::unordered_map<umq_eid_t, umq_route_list_t, UmqEidHash, UmqEidEqual> route_list_map_; // route_list
};
} // namespace umq
} // namespace ubs
} // namespace ock
#endif // HCOM_UMQ_EID_TABLE_H
