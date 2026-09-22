/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UMQ_DATA_PLANE_H
#define UBS_COMM_UMQ_DATA_PLANE_H

#include <atomic>
#include <cstddef>
#include <new>

#include "core/ubsocket_data_rx.h"
#include "core/ubsocket_data_tx.h"

namespace ock {
namespace ubs {
namespace umq {
class UmqSocket;

/* 数据面状态条目：TX/RX ops 状态与 DataTx/DataRx 壳不再内嵌于 UmqSocket，
 * 改存 fd 索引的分页全局表——数组存储无 malloc 头、密度完美，
 * 是 P2（链路虚拟化/SoA）方向的第一块基础设施。
 * 生命周期与 socket 的数据面一致：GenerateSocketCommOps（握手成功）经
 * ReinitTxOps 构造整条目（ReinitRxOps 仅取址，见调用顺序注释），~UmqSocket 析构。
 * owner 兼作"已构造"标志（页为零初始化原始存储，nullptr = 未构造），并用于
 * fd 复用防护（析构时仅当 owner == this 才销毁）。 */
struct DataPlaneEntry {
    DataTxOps tx;
    DataRxOps rx;
    DataTx txw; /* 通用层壳（流控/切分/回退），由 Generate 装配指向本条目的 tx/rx */
    DataRx rxw;
    UmqSocket *owner;

    DataPlaneEntry(int fd, uint64_t umq_handle, UmqSocket *o)
        : tx(fd, umq_handle, o), rx(fd, umq_handle, o), owner(o)
    {
    }
};

class DataPlaneTable {
public:
    static constexpr std::size_t PAGE_SHIFT = 10; /* 1024 条/页 */
    static constexpr std::size_t PAGE_SIZE = 1UL << PAGE_SHIFT;
    static constexpr std::size_t MAX_PAGES = 1024; /* fd 上限 1M，与 ArraySet 同量级 */

    static DataPlaneTable &Instance();

    /* fd 槽位的原始存储；页缺失时 CAS 安装（分配失败返回 nullptr，调用方按
     * 资源不足降级）。返回的指针在进程生命周期内稳定（页永不释放）。 */
    DataPlaneEntry *SlotFor(int fd);

    /* 只读：fd 越界或页未安装返回 nullptr（不触发安装）。 */
    DataPlaneEntry *Peek(int fd) const;

    /* 已构造条目（owner 非空）或 nullptr。 */
    static DataPlaneEntry *Live(int fd)
    {
        DataPlaneEntry *e = Instance().Peek(fd);
        return (e != nullptr && e->owner != nullptr) ? e : nullptr;
    }

    /* 析构侧：销毁条目并把 owner 槽清回 nullptr（原始存储约定）。 */
    static void DestroyEntry(DataPlaneEntry *e);

private:
    struct Page {
        alignas(alignof(DataPlaneEntry)) unsigned char raw[PAGE_SIZE * sizeof(DataPlaneEntry)] = {};
    };
    std::atomic<Page *> pages_[MAX_PAGES] = {};
};
} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_DATA_PLANE_H
