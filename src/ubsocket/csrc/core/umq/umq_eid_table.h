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
    bool operator()(const umq_eid_t& a, const umq_eid_t& b) const
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
}
}
}
#endif // HCOM_UMQ_EID_TABLE_H
