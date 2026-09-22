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

/*
 * Unit tests for ubsocket_bigdata.cpp (批 1-3 全部内容).
 *
 * 覆盖范围:
 *   1. UbsBigdata::HandleRxControl 内容校验门禁(吸收原
 *      ubsocket_bigdata_handler_test.cpp 拒绝路径用例,改用真实 UmqSocket +
 *      mockcpp,正例 READ_DONE/READ_ABORT 走到 ReleasePinned + umq_buf_free)
 *   2. UbsBigdata::TrySenderPost 发送路径(入口校验 / validateSegs / 4064
 *      边界 / coalesce / offer 封板 / UMQ_BATCH_SIZE 截断 / umq_post
 *      全拒与部分接受回滚 / SN 回退 / ReleasePinned 回滚 / gen check 头房)
 *   3. 接收与完成路径(批 2): DoReadOffer(经 HandleRxControl 正例)→
 *      HandleTxCompletion(READ CQE 驱动)→ FinalizeIo
 *   4. 超时/回压/清理(批 3): SweepExpiredForSocket 两阶段(pin 超时 + rx-ctx
 *      半边)、RetryPendingReadsForSocket、HandleFlowControlUpdate、
 *      NeedsPollerAttention、CleanupSocketState、GetGenCheckStats
 * 注: ubsocket_bigdata_order.h 纯头 inline 辅助的测试已拆分到
 *     ubsocket_bigdata_order_test.cpp(独立极轻 target,见该文件头注释)。
 *
 * mock 策略: UmqApi 无 _ptr(adapter 后端),全部全局 C 函数直接
 * MOCKER_CPP(::umq_xxx);Block::DecRef 归零调用 ::ubsocket_iobuf_deallocate,
 * 每个用例都挂载。
 */
#include "ubsocket_bigdata.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include <mockcpp/mockcpp.hpp>

#include "ubsocket.h"
#include "ubsocket_data.h"
#include "ubsocket_data_tx.h"
#include "ubsocket_global_setting.h"
#include "ubsocket_iobuf.h"
#include "ubsocket_lock.h"
#include "ubsocket_proto.h"
#include "ubsocket_ref.h"
#include "ubsocket_set.h"
#include "ubsocket_socket.h"
#include "ubsocket_tx_cqe_poller.h"
#include "umq_pro_types.h"
#include "umq_socket.h"

using namespace ock::ubs;
using namespace ock::ubs::umq;
namespace p = ock::ubs::proto;

namespace {
static const int TEST_FD = 42;
static const uint64_t TEST_UMQ_HANDLE = 0x1000;
/* sizeof(urma_seg_t); ubsocket_bigdata.cpp:436 内部 constexpr 对测试不可见,镜像常量 */
static constexpr uint32_t TEST_URMA_SEG_T_SIZE = 48;

/* ------------------------------------------------------------------ */
/* TestBuf: 构造带指定 payload 的 umq_buf_t(吸收自原
 * ubsocket_bigdata_handler_test.cpp)                                  */
/* ------------------------------------------------------------------ */
struct TestBuf {
    umq_buf_t qbuf;
    uint8_t data[4096];

    TestBuf()
    {
        memset(&qbuf, 0, sizeof(qbuf));
        memset(data, 0, sizeof(data));
        qbuf.buf_data = reinterpret_cast<char *>(data);
        qbuf.data_size = 0;
        qbuf.qbuf_ext[0] = 0;
    }

    /* Fill buf_data with a valid UbsCtrlHdr (total_len/data_size 与布局公式自洽;
     * 仅适用于 nsegs==nmempool_infos 的构造(正例 DONE/ABORT 与零段 offer)) */
    void SetCtrlHdr(uint8_t type, uint16_t nsegs, uint32_t infos_bytes, uint16_t inline_data_len, uint64_t seq)
    {
        auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(data);
        ctrl->type = type;
        ctrl->nmempool_infos = static_cast<uint8_t>(nsegs);
        ctrl->nsegs = nsegs;
        ctrl->inline_data_len = inline_data_len;
        ctrl->total_len = static_cast<uint16_t>(p::ctrl_total_len(nsegs, infos_bytes, inline_data_len));
        ctrl->seq = seq;
        ctrl->read_gen = 0;
        qbuf.data_size = ctrl->total_len;
    }

    void SetOrdinaryData(const uint8_t *src, uint32_t size)
    {
        memcpy(data, src, size > sizeof(data) ? sizeof(data) : size);
        qbuf.data_size = size;
    }

    void SetPatternData(uint8_t pattern, uint32_t size)
    {
        memset(data, pattern, size > sizeof(data) ? sizeof(data) : size);
        qbuf.data_size = size;
    }
};

/* ------------------------------------------------------------------ */
/* FakeUmqApi: 每用例独立的 umq API 假后端。mockcpp invoke 只接受
 * static 函数,经 g_current 路由到当前用例的实例状态。                */
/* ------------------------------------------------------------------ */
struct FakeUmqApi {
    static FakeUmqApi *g_current;

    /* umq_buf_alloc 的缓冲池。用 deque: emplace_back 不使既有元素地址失效,
     * 生产代码在 TrySenderPost 中途再 alloc 时旧 buf 指针必须保持有效
     * (真实 UMQ 池分配语义),vector 扩容会导致 use-after-free。 */
    struct PoolBuf {
        umq_buf_t buf{};
        std::unique_ptr<uint8_t[]> region; /* 8B gen 头房 + 4096B 数据区 */
    };
    std::deque<PoolBuf> pool;
    size_t alloc_count{0};
    size_t alloc_fail_after{std::numeric_limits<size_t>::max()};

    /* umq_data_to_head: Block data -> 持有它的 qbuf 注册表 */
    struct BackingQbuf {
        umq_buf_t qbuf{};
    };
    std::vector<BackingQbuf> backings;
    bool data_to_head_fail{false};

    /* umq_mempool_info_get 旋钮 */
    bool info_get_fail{false};
    uint32_t blob_len{UBS_MEMPOOL_INFO_HDR_SIZE + TEST_URMA_SEG_T_SIZE}; /* 72 */

    /* umq_post 旋钮 */
    int post_ret{0};
    int post_bad_index{-1}; /* -1: bad==nullptr; 0: bad==head; N: bad==链上第 N 个 buf */
    size_t post_buf_count{0};
    std::vector<umq_buf_t *> posts; /* 每次 umq_post 的链头 */
    std::vector<int> post_dirs;

    /* 接收侧(mempool import / READ WR 构建)旋钮 */
    int mempool_state_ret{UMQ_REMOTE_MEMPOOL_STATE_REUSE};
    int info_set_ret{0};
    size_t info_set_count{0};
    int get_remote_fields_ret{0};
    size_t get_remote_fields_count{0};
    /* 从第 N 次调用起返回 get_remote_fields_ret(默认 0: 恒失败, 兼容既有语义) */
    size_t get_remote_fields_fail_after{0};
    /* READ dest buf 需 total_data_size==request_size 才能过
     * ValidateStandaloneReadQbufChain(真实池语义);发送侧用例不关心,
     * 保持默认 false 以零影响批 1 行为 */
    bool alloc_set_total_size{false};

    /* 计数 */
    size_t free_count{0};
    size_t dealloc_count{0};

    umq_buf_t *Alloc(uint32_t request_size, uint32_t request_qbuf_num, uint64_t umqh, umq_alloc_option_t *option)
    {
        (void)request_qbuf_num;
        (void)option;
        if (alloc_count >= alloc_fail_after) {
            return nullptr;
        }
        ++alloc_count;
        pool.emplace_back();
        PoolBuf &pb = pool.back();
        pb.region.reset(new uint8_t[4096 + 8]);
        memset(&pb.buf, 0, sizeof(pb.buf));
        pb.buf.umqh = umqh;
        pb.buf.alloc_state = 1;
        pb.buf.buf_data = reinterpret_cast<char *>(pb.region.get() + 8);
        pb.buf.buf_size = 4096;
        if (request_size > pb.buf.buf_size) {
            pb.buf.buf_size = request_size; /* 仅登记容量,不实际分配更多 */
        }
        if (alloc_set_total_size) {
            pb.buf.data_size = request_size;
            pb.buf.total_data_size = request_size;
        }
        return &pb.buf;
    }

    void Free(umq_buf_t *qbuf)
    {
        (void)qbuf;
        ++free_count;
    }

    void RegisterBacking(char *data, uint32_t size, uint32_t mempool_id)
    {
        backings.emplace_back();
        BackingQbuf &b = backings.back();
        b.qbuf.buf_data = data;
        b.qbuf.buf_size = size;
        b.qbuf.mempool_id = mempool_id;
    }
};

FakeUmqApi *FakeUmqApi::g_current = nullptr;

static umq_buf_t *FakeUmqBufAlloc(uint32_t request_size, uint32_t request_qbuf_num, uint64_t umqh,
                                  umq_alloc_option_t *option)
{
    return FakeUmqApi::g_current->Alloc(request_size, request_qbuf_num, umqh, option);
}

static void FakeUmqBufFree(umq_buf_t *qbuf)
{
    FakeUmqApi::g_current->Free(qbuf);
}

static umq_buf_t *FakeDataToHead(void *data)
{
    FakeUmqApi *f = FakeUmqApi::g_current;
    if (f->data_to_head_fail) {
        return nullptr;
    }
    for (auto &b : f->backings) {
        char *beg = b.qbuf.buf_data;
        char *end = beg + b.qbuf.buf_size;
        char *d = static_cast<char *>(data);
        if (d >= beg && d < end) {
            return &b.qbuf;
        }
    }
    return nullptr;
}

static int FakeMempoolInfoGet(uint64_t umqh, uint32_t mempool_id, uint8_t *mempool_info, uint32_t mempool_info_size,
                              uint32_t *mempool_info_len)
{
    (void)umqh;
    (void)mempool_id;
    (void)mempool_info;
    (void)mempool_info_size;
    FakeUmqApi *f = FakeUmqApi::g_current;
    if (f->info_get_fail) {
        return -1;
    }
    *mempool_info_len = f->blob_len;
    return 0;
}

static int FakeUmqPost(uint64_t umqh, umq_buf_t *qbuf, umq_io_option_t *option, umq_buf_t **bad_qbuf)
{
    (void)umqh;
    FakeUmqApi *f = FakeUmqApi::g_current;
    f->posts.push_back(qbuf);
    f->post_dirs.push_back(option != nullptr ? static_cast<int>(option->io_direction) : -1);
    f->post_buf_count = 0;
    for (umq_buf_t *b = qbuf; b != nullptr; b = b->qbuf_next) {
        ++f->post_buf_count;
    }
    if (f->post_ret != 0) {
        if (f->post_bad_index < 0) {
            *bad_qbuf = nullptr;
        } else {
            umq_buf_t *cur = qbuf;
            for (int i = 0; cur != nullptr && i < f->post_bad_index; ++i) {
                cur = cur->qbuf_next;
            }
            *bad_qbuf = cur;
        }
    }
    return f->post_ret;
}

static int FakeRemoteMempoolStateCheck(uint64_t umqh, const uint8_t *mempool_info, uint32_t mempool_info_len)
{
    (void)umqh;
    (void)mempool_info;
    (void)mempool_info_len;
    return FakeUmqApi::g_current->mempool_state_ret;
}

static int FakeMempoolInfoSet(uint64_t umqh, const uint8_t *mempool_info, uint32_t mempool_info_len)
{
    (void)umqh;
    (void)mempool_info;
    (void)mempool_info_len;
    FakeUmqApi *f = FakeUmqApi::g_current;
    ++f->info_set_count;
    return f->info_set_ret;
}

static int FakeGetRemoteFields(uint64_t umqh, const uint8_t *mempool_info, uint32_t mempool_info_len, uint32_t *mp_id,
                               uint32_t *tok_id, uint32_t *tok_val)
{
    (void)umqh;
    (void)mempool_info;
    (void)mempool_info_len;
    FakeUmqApi *f = FakeUmqApi::g_current;
    ++f->get_remote_fields_count;
    if (f->get_remote_fields_ret != 0 && f->get_remote_fields_count >= f->get_remote_fields_fail_after) {
        return f->get_remote_fields_ret;
    }
    *mp_id = 7;
    *tok_id = 8;
    *tok_val = 9;
    return 0;
}

static void FakeIobufDeallocate(void *addr)
{
    (void)addr;
    FakeUmqApi::g_current->dealloc_count++;
}

/* ------------------------------------------------------------------ */
/* Fixture                                                             */
/* ------------------------------------------------------------------ */
class UbsBigdataTest : public ::testing::Test {
protected:
    Ref<UmqSocket> umqSock_; /* 具体类型: 测试需访问 umq_handle_/LoadSeqNum 等 */
    FakeUmqApi fake_;
    /* §5 helper 的 offer 载体: DoReadOffer 会保留 offer_rx_buf 指针直到
     * ctx 收尾, 必须是整个用例存活的成员(helper 局部 TestBuf 会悬垂——
     * 栈上 qbuf 在 helper 返回后被覆盖, FreeQbufChain 读 qbuf_next 崩) */
    TestBuf offerTbuf_;

    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = false;
        GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 1000;
        GlobalSetting::UBS_GRACE_MS = 100;
        GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS = 0; /* 关闭预留，隔离既有用例 */
        GlobalSetting::UBS_TX_DEPTH = 255;              /* 与生产默认对齐，避免用例间污染 */
        FakeUmqApi::g_current = &fake_;
        ClearPollerActive();
        MOCKER_CPP(::ubsocket_iobuf_deallocate).stubs().will(invoke(&FakeIobufDeallocate));
        umqSock_ = MakeRef<UmqSocket>(TEST_FD);
    }

    void TearDown() override
    {
        /* ~UmqSocket → UnInitialize → CleanupSocketState → pinned DecRef →
         * ::ubsocket_iobuf_deallocate 必须在 mock 仍挂载时发生,故先复位
         * umq_handle_ 让 UnInitialize 在清理 state 后提前返回,再释放引用,
         * 最后才 verify()。 */
        if (umqSock_ != nullptr) {
            umqSock_->umq_handle_ = UMQ_INVALID_HANDLE;
            /* 主动清数据面槽位: handle 置无效让 ~UmqSocket 提前返回的同时也
             * 跳过了条目销毁,槽位会悬挂给已销毁 socket;后续用例 ReinitTxOps
             * 撞 "owner 不符" 拒绝重建(此前靠堆地址复用侥幸通过,插入新用例
             * 即破坏)。ReleaseDataPlane 仅在 owner==this 时清槽,安全幂等。 */
            umqSock_->ReleaseDataPlane();
        }
        ClearPollerActive();
        umqSock_ = nullptr;
        ArraySet<Socket>::GetInstance().ReleaseAll();
        GlobalMockObject::verify();
        errno = 0;
    }

    /* MarkActive 会把 socket 加入 poller active 集合(stopped_ 默认 false),
     * 每用例结束必须清空,否则持有悬挂 SocketPtr。 */
    void ClearPollerActive()
    {
        auto &poller = TxCqePoller::Instance();
        std::lock_guard<std::mutex> lk(poller.active_mutex_);
        poller.active_.clear();
    }

    /* 发送路径 mock 全家桶 */
    void MountSendMocks()
    {
        MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&FakeUmqBufAlloc));
        MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&FakeUmqBufFree));
        MOCKER_CPP(::umq_post).stubs().will(invoke(&FakeUmqPost));
        MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&FakeDataToHead));
        MOCKER_CPP(::umq_mempool_info_get).stubs().will(invoke(&FakeMempoolInfoGet));
    }

    /* 接收侧 mock 全家桶: 发送侧之上追加 mempool import / READ WR 构建三件套 */
    void MountReceiveMocks()
    {
        MountSendMocks();
        MOCKER_CPP(::umq_remote_mempool_state_check).stubs().will(invoke(&FakeRemoteMempoolStateCheck));
        MOCKER_CPP(::umq_mempool_info_set).stubs().will(invoke(&FakeMempoolInfoSet));
        MOCKER_CPP(::umq_mempool_info_get_remote_fields).stubs().will(invoke(&FakeGetRemoteFields));
        fake_.alloc_set_total_size = true;
    }

    /* 把 fixture 的 UmqSocket 注册进 ArraySet 并给 fake handle */
    void PrepareSendSocket()
    {
        ArraySet<Socket>::GetInstance().OverrideItem(TEST_FD, umqSock_.Get());
        umqSock_->umq_handle_ = TEST_UMQ_HANDLE;
    }

    /* 接收侧: 在 PrepareSendSocket 之上装配 rxQueue——FinalizeIo 的
     * DeliverToRxQueue → AddQbuf 走真实入队(SPSC 非 O3 路径,不依赖 SN) */
    void PrepareReceiveSocket()
    {
        PrepareSendSocket();
        if (umqSock_->rxQueue == nullptr) {
            umqSock_->rxQueue = std::make_unique<UmqBufferReceiveQueue>();
        }
    }

    /* 测试共享常量 */
    static constexpr uint64_t TEST_REMOTE_ADDR_BASE = 0x100000; /* offer UbsSeg.addr 基址 */
    /* pending_reads 容量边界(生产常量 UBS_BIG_PENDING_READ_MAX 是 .cpp 内部
     * constexpr,测试不可见,此处按值镜像并注明来源) */
    static constexpr size_t TEST_PENDING_READ_MAX = 2048;
    /* deferred_ctrl 容量边界(生产常量 UBS_BIG_DEFERRED_CTRL_MAX 镜像) */
    static constexpr size_t TEST_DEFERRED_CTRL_MAX = 2048;

    /* 构造合法 READ_OFFER 载荷: UbsCtrlHdr + nsegs×UbsSeg + nsegs×info_len 字节
     * 不透明 mempool blob(全零,ub socket 不透传字段)。经 HandleRxControl 门禁
     * 校验 + ParseReadOffer 通过(1:1 idx、info_len 下限、total_len 自洽)。 */
    static void BuildReadOffer(TestBuf &tbuf, uint16_t nsegs, uint64_t seq, uint64_t read_gen, uint32_t info_len,
                               uint32_t seg_len)
    {
        memset(&tbuf.qbuf, 0, sizeof(tbuf.qbuf));
        memset(tbuf.data, 0, sizeof(tbuf.data));
        tbuf.qbuf.buf_data = reinterpret_cast<char *>(tbuf.data);
        auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
        ctrl->type = UBS_READ_OFFER;
        ctrl->nsegs = nsegs;
        ctrl->nmempool_infos = nsegs;
        ctrl->inline_data_len = 0;
        ctrl->seq = seq;
        ctrl->read_gen = read_gen;
        auto *segs = reinterpret_cast<UbsSeg *>(tbuf.data + UBS_CTRL_HDR_SIZE);
        uint8_t *infos = tbuf.data + UBS_CTRL_HDR_SIZE + static_cast<uint32_t>(nsegs) * UBS_SEG_SIZE;
        for (uint16_t i = 0; i < nsegs; ++i) {
            memset(&segs[i], 0, sizeof(segs[i]));
            segs[i].addr = TEST_REMOTE_ADDR_BASE + i;
            segs[i].length = seg_len;
            segs[i].mempool_info_idx = static_cast<uint8_t>(i);
            segs[i].mempool_info_len = static_cast<uint16_t>(info_len);
            memset(infos, 0, info_len);
            infos += info_len;
        }
        ctrl->total_len = static_cast<uint16_t>(p::ctrl_total_len(nsegs, static_cast<uint32_t>(nsegs) * info_len, 0));
        tbuf.qbuf.data_size = ctrl->total_len;
    }

    /* 在 fake 池中按分配顺序收集 DoReadOffer 构建的 READ 目标 buf(slot 序同池序) */
    static std::vector<umq_buf_t *> FindReadDestBufs(FakeUmqApi &fake)
    {
        std::vector<umq_buf_t *> out;
        for (auto &pb : fake.pool) {
            auto *pro = reinterpret_cast<umq_buf_pro_t *>(pb.buf.qbuf_ext);
            if (pro->opcode == UMQ_OPC_READ) {
                out.push_back(&pb.buf);
            }
        }
        return out;
    }

    /* 模拟一个 READ CQE: HandleTxCompletion 收到 slot->pending 指向的同一 qbuf
     * (CQE 携带 pro 元数据,status 由测试注入)。 */
    bool SimulateReadCqe(umq_buf_t *qbuf, uint32_t status, uint32_t *span, int *deferred_fd)
    {
        qbuf->status = status;
        return UbsBigdata::HandleTxCompletion(umqSock_.Get(), qbuf, span, deferred_fd);
    }

    /* 构造单段 data_list */
    ubs_data_list_t MakeList(ubs_segment_t *segs, uint16_t nsegs)
    {
        ubs_data_list_t list;
        list.segments = segs;
        list.nsegs = nsegs;
        return list;
    }

    /* 在 posted 链中按控制类型找 buf(小包 buf_data 指向测试 block 数据,全零,不会误中) */
    static umq_buf_t *FindBufWithType(umq_buf_t *head, uint8_t type)
    {
        for (umq_buf_t *b = head; b != nullptr; b = b->qbuf_next) {
            if (b->buf_data != nullptr && reinterpret_cast<const UbsCtrlHdr *>(b->buf_data)->type == type) {
                return b;
            }
        }
        return nullptr;
    }

    static UbsBigdata::GenCheckStats GenStats()
    {
        return UbsBigdata::GetGenCheckStats();
    }

    /* §5 helper: 越过 1ms 级 deadline(用例构造 deadline = now + timeoutMs,
     * 见 SweepExpiredForSocket 系列) */
    static void SleepPastDeadline()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    /* §5 helper: 发送侧 offer 构造 pinned 条目(gen check on, 指定本地超时)。
     * deadline = now + timeoutMs(UBS_BIG_PIN_TIMEOUT_MARGIN_MS 被清零)。 */
    void PostPinOffer(Block &block, uint32_t segLen, uint32_t timeoutMs)
    {
        GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
        GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 0;
        umqSock_->SetLocalRpcTimeoutMs(timeoutMs);
        ubs_segment_t segs[1] = {};
        segs[0].block = &block;
        segs[0].len = segLen;
        ubs_data_list_t list = MakeList(segs, 1);
        ssize_t out = -123;
        errno = 0;
        EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
        EXPECT_EQ(out, 1);
    }

    /* §5 helper: 接收侧 offer → READ post EAGAIN(bad==head, 未提交)→ ctx 入
     * pending_reads。peer 超时 > 0 时 ctx 带 deadline。前提: post_ret 已设为
     * -UMQ_ERR_EAGAIN 且 post_bad_index=0。 */
    void OfferIntoPending(uint64_t seq, uint32_t peerTimeoutMs)
    {
        umqSock_->SetPeerRpcTimeoutMs(peerTimeoutMs);
        /* offer 载体必须用 fixture 成员: DoReadOffer 保留 offer_rx_buf 直到
         * ctx 收尾, helper 局部 TestBuf 返回后悬垂(见 core.md 陷阱 #18) */
        BuildReadOffer(offerTbuf_, 1, seq, 0, 72, 4065);
        EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &offerTbuf_.qbuf));
    }

    /* §5 helper: 接收侧 offer → post 成功 → ctx 入 active_io。peer 超时 > 0
     * 时 ctx 带 deadline。前提: post_ret=0。 */
    void OfferIntoActive(uint64_t seq, uint32_t peerTimeoutMs)
    {
        umqSock_->SetPeerRpcTimeoutMs(peerTimeoutMs);
        /* offer 载体必须用 fixture 成员: DoReadOffer 保留 offer_rx_buf 直到
         * ctx 收尾, helper 局部 TestBuf 返回后悬垂(见 core.md 陷阱 #18) */
        BuildReadOffer(offerTbuf_, 1, seq, 0, 72, 4065);
        EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &offerTbuf_.qbuf));
    }

    /* §5 helper: mempool import 失败 + ABORT post EAGAIN → 1 个 deferred ctrl
     * (本函数自设 post 旋钮, import 旋钮)。 */
    void OfferImportFailIntoDeferred(uint64_t seq)
    {
        fake_.mempool_state_ret = UMQ_REMOTE_MEMPOOL_STATE_NEED_IMPORT;
        fake_.info_set_ret = -1;
        fake_.post_ret = -UMQ_ERR_EAGAIN;
        fake_.post_bad_index = 0;
        /* offer 载体必须用 fixture 成员: DoReadOffer 保留 offer_rx_buf 直到
         * ctx 收尾, helper 局部 TestBuf 返回后悬垂(见 core.md 陷阱 #18) */
        BuildReadOffer(offerTbuf_, 1, seq, 0, 72, 4065);
        EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &offerTbuf_.qbuf));
    }
};

/* ================================================================== */
/* §2 UbsBigdata::HandleRxControl 内容校验门禁(吸收孤儿测试拒绝路径)    */
/* ================================================================== */

TEST_F(UbsBigdataTest, HandleRxControl_NullSock_ReturnsFalse)
{
    TestBuf tbuf;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(SocketPtr(), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_NullQbuf_ReturnsFalse)
{
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), nullptr));
}

TEST_F(UbsBigdataTest, HandleRxControl_NullBufData_ReturnsFalse)
{
    TestBuf tbuf;
    tbuf.qbuf.buf_data = nullptr;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_DataSizeBelowCtrlHdr_ReturnsFalse)
{
    TestBuf tbuf;
    tbuf.qbuf.data_size = UBS_CTRL_HDR_SIZE - 1;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_AllZeroData_NotControl)
{
    TestBuf tbuf;
    tbuf.SetPatternData(0x00, 64);
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_RandomOrdinaryData_NotControl)
{
    TestBuf tbuf;
    uint8_t payload[256];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = static_cast<uint8_t>(i * 13 + 5); /* 首字节 5 ∉ {OFFER,DONE,ABORT} */
    }
    tbuf.SetOrdinaryData(payload, sizeof(payload));
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_AsciiTextData_NotControl)
{
    TestBuf tbuf;
    const char *text = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    tbuf.SetOrdinaryData(reinterpret_cast<const uint8_t *>(text), strlen(text));
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_FirstByteMatchesOffer_RestGarbage)
{
    TestBuf tbuf;
    tbuf.data[0] = UBS_READ_OFFER;
    memset(tbuf.data + 1, 0xAA, 255);
    tbuf.qbuf.data_size = 256;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_FirstByteMatchesDone_TotalLenMismatch)
{
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_DONE;
    ctrl->nsegs = 0;
    ctrl->nmempool_infos = 0;
    ctrl->inline_data_len = 0;
    ctrl->total_len = 24; /* != data_size(32) → total_len 失配门禁 */
    ctrl->seq = 0;
    tbuf.qbuf.data_size = 32;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_ValidType_NsegsExceedsMax)
{
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_OFFER;
    ctrl->nsegs = UBS_SEG_MAX + 1;
    ctrl->nmempool_infos = UBS_SEG_MAX + 1;
    ctrl->inline_data_len = 0;
    ctrl->total_len = UBS_CTRL_HDR_SIZE; /* 24,过 size 门禁后落到 nsegs 上限 */
    ctrl->seq = 0;
    tbuf.qbuf.data_size = UBS_CTRL_HDR_SIZE;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferWithZeroNsegs_Rejected)
{
    TestBuf tbuf;
    tbuf.SetCtrlHdr(UBS_READ_OFFER, 0, 0, 0, 1);
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferNsegsNotEqualMempoolInfos_Rejected)
{
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_OFFER;
    ctrl->nsegs = 2;
    ctrl->nmempool_infos = 1;
    ctrl->inline_data_len = 0;
    ctrl->total_len = UBS_CTRL_HDR_SIZE;
    ctrl->seq = 0;
    tbuf.qbuf.data_size = UBS_CTRL_HDR_SIZE;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_DoneCarriesNsegs_Rejected)
{
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_DONE;
    ctrl->nsegs = 1;
    ctrl->nmempool_infos = 0;
    ctrl->inline_data_len = 0;
    ctrl->total_len = UBS_CTRL_HDR_SIZE;
    ctrl->seq = 0;
    tbuf.qbuf.data_size = UBS_CTRL_HDR_SIZE;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_SegsRegionOverflow_Rejected)
{
    /* nsegs=1 但 data_size 恰好等于头长:segs_region(16) > 0 */
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_OFFER;
    ctrl->nsegs = 1;
    ctrl->nmempool_infos = 1;
    ctrl->inline_data_len = 0;
    ctrl->total_len = UBS_CTRL_HDR_SIZE;
    ctrl->seq = 0;
    tbuf.qbuf.data_size = UBS_CTRL_HDR_SIZE;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_InfoLenBelowMin_Rejected)
{
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_OFFER;
    ctrl->nsegs = 1;
    ctrl->nmempool_infos = 1;
    ctrl->inline_data_len = 0;
    auto *seg = reinterpret_cast<UbsSeg *>(tbuf.data + UBS_CTRL_HDR_SIZE);
    seg->mempool_info_len = UBS_MEMPOOL_INFO_HDR_SIZE + TEST_URMA_SEG_T_SIZE - 1; /* 71 < 72 */
    ctrl->total_len = UBS_CTRL_HDR_SIZE + UBS_SEG_SIZE + seg->mempool_info_len;
    tbuf.qbuf.data_size = ctrl->total_len;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_InfosBytesOverflow_Rejected)
{
    /* 两个 info 的累计长度超出剩余空间 */
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_OFFER;
    ctrl->nsegs = 2;
    ctrl->nmempool_infos = 2;
    ctrl->inline_data_len = 0;
    auto *segs = reinterpret_cast<UbsSeg *>(tbuf.data + UBS_CTRL_HDR_SIZE);
    segs[0].mempool_info_len = UBS_MEMPOOL_INFO_HDR_SIZE + TEST_URMA_SEG_T_SIZE; /* 72 */
    segs[1].mempool_info_len = UBS_MEMPOOL_INFO_HDR_SIZE + TEST_URMA_SEG_T_SIZE;
    ctrl->total_len = UBS_CTRL_HDR_SIZE + 2 * UBS_SEG_SIZE + 72;     /* 与 data_size 一致,过 total_len 门禁 */
    tbuf.qbuf.data_size = UBS_CTRL_HDR_SIZE + 2 * UBS_SEG_SIZE + 72; /* 第二个 info 越界 */
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_TotalLenNotMatchLayout_Rejected)
{
    TestBuf tbuf;
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(tbuf.data);
    ctrl->type = UBS_READ_DONE;
    ctrl->nsegs = 0;
    ctrl->nmempool_infos = 0;
    ctrl->inline_data_len = 50;
    ctrl->total_len = 116; /* 期望 24+50=74 */
    ctrl->seq = 0;
    tbuf.qbuf.data_size = 116;
    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
}

TEST_F(UbsBigdataTest, HandleRxControl_AllInvalidTypeValues_Rejected)
{
    TestBuf tbuf;
    for (int t = 0; t <= 255; ++t) {
        if (t == UBS_READ_OFFER || t == UBS_READ_DONE || t == UBS_READ_ABORT) {
            continue;
        }
        tbuf.data[0] = static_cast<uint8_t>(t);
        memset(tbuf.data + 1, 0, 63);
        tbuf.qbuf.data_size = 64;
        EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf))
            << "type=" << t << " should be rejected as invalid control type";
    }
}

/* 正例: READ_DONE 通过全部校验,走到真实 UmqSocket 的 state 创建 +
 * ReleasePinned(miss) + umq_buf_free 消费。 */
TEST_F(UbsBigdataTest, HandleRxControl_ValidReadDone_ConsumedAndFreed)
{
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&FakeUmqBufFree));
    TestBuf tbuf;
    tbuf.SetCtrlHdr(UBS_READ_DONE, 0, 0, 0, 42);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    EXPECT_EQ(fake_.free_count, 1u);
    /* 惰性 state 已创建 */
    EXPECT_NE(umqSock_->GetBigdataState(), nullptr);
}

TEST_F(UbsBigdataTest, HandleRxControl_ValidReadAbort_ConsumedAndFreed)
{
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&FakeUmqBufFree));
    TestBuf tbuf;
    tbuf.SetCtrlHdr(UBS_READ_ABORT, 0, 0, 0, 99);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    EXPECT_EQ(fake_.free_count, 1u);
}

/* ================================================================== */
/* §3 UbsBigdata::TrySenderPost 发送路径                               */
/* 顺序约束: 组内最后一个用例(SendPath_TxDepthAccounting)会装配          */
/* DataPlaneTable 条目,~UmqSocket 在 handle 无效时提前返回、条目销毁     */
/* 被跳过而残留;后续发送组用例复用 TEST_FD 时该悬挂条目会干扰其           */
/* GetTxOps()/记账。发送组新用例只能追加在它之前。                      */
/* ================================================================== */

TEST_F(UbsBigdataTest, TrySenderPost_NullDataList_ReturnsFalse)
{
    ssize_t out = -123;
    errno = 0;
    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, nullptr, &out));
    EXPECT_EQ(out, -1);
}

TEST_F(UbsBigdataTest, TrySenderPost_NullOut_ReturnsFalse)
{
    ubs_segment_t segs[1] = {};
    ubs_data_list_t list = MakeList(segs, 1);
    errno = 0;
    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, nullptr));
}

TEST_F(UbsBigdataTest, TrySenderPost_EmptyBatch_ReturnsTrueOutZero)
{
    ubs_data_list_t list = MakeList(nullptr, 0);
    ssize_t out = -123;
    errno = 0;
    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 0);
}

TEST_F(UbsBigdataTest, TrySenderPost_NullSegments_ReturnsFalseEinval)
{
    ubs_data_list_t list = MakeList(nullptr, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(UbsBigdataTest, TrySenderPost_NotUmqSocket_ReturnsFalseEpipe)
{
    ubs_segment_t segs[1] = {};
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EPIPE);
}

TEST_F(UbsBigdataTest, TrySenderPost_ValidateSegs_NullBlock_ReturnsFalse)
{
    PrepareSendSocket();
    ubs_segment_t segs[1] = {};
    segs[0].block = nullptr;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(UbsBigdataTest, TrySenderPost_ValidateSegs_OffsetOverflow_ReturnsFalse)
{
    PrepareSendSocket();
    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].offset = UINT32_MAX;
    segs[0].len = 1;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EINVAL);
}

TEST_F(UbsBigdataTest, TrySenderPost_SmallSingle_ZeroCopyPosted)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    EXPECT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(fake_.post_buf_count, 1u);
    EXPECT_EQ(fake_.post_dirs.back(), UMQ_IO_TX);
    umq_buf_t *small = fake_.posts[0];
    /* 零拷贝: buf_data 重指到 block data */
    EXPECT_EQ(small->buf_data, block.data);
    EXPECT_EQ(small->data_size, 100u);
    EXPECT_EQ(small->total_data_size, 100u);
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(small->qbuf_ext);
    EXPECT_EQ(pro->opcode, UMQ_OPC_SEND_IMM);
    EXPECT_EQ(pro->flag.bs.complete_enable, 1);
    EXPECT_EQ(pro->imm.user_data, 0u); /* 首个 SN=0 */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 1u);
    EXPECT_EQ(block.nshared.load(), 2); /* 测试 + Send 侧 IncRef */
    EXPECT_EQ(fake_.free_count, 0u);
}

TEST_F(UbsBigdataTest, TrySenderPost_SmallAt4064Boundary_StillSmall)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t raw[UBS_SMALL_DATA_MAX];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = UBS_SMALL_DATA_MAX; /* 4064 == small_max,走小包 */
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    EXPECT_EQ(fake_.post_buf_count, 1u);
    EXPECT_EQ(fake_.posts[0]->data_size, static_cast<uint32_t>(UBS_SMALL_DATA_MAX));
    EXPECT_EQ(umqSock_->LoadSeqNum(), 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_Large4065_OfferPath)
{
    PrepareSendSocket();
    MountSendMocks();
    const uint32_t len = UBS_SMALL_DATA_MAX + 1; /* 4065 > small_max,走大段 */
    uint8_t raw[UBS_SMALL_DATA_MAX + 1];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = len;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    EXPECT_EQ(fake_.post_buf_count, 1u);
    umq_buf_t *offer = fake_.posts[0];
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(offer->buf_data);
    EXPECT_EQ(ctrl->type, UBS_READ_OFFER);
    EXPECT_EQ(ctrl->nsegs, 1);
    EXPECT_EQ(ctrl->nmempool_infos, 1);
    EXPECT_EQ(ctrl->total_len, p::ctrl_total_len(1, fake_.blob_len, 0));
    EXPECT_EQ(ctrl->read_gen, 0u); /* gen check 关闭 */
    /* pin 已登记: block 引用计数 = 测试 + pin */
    EXPECT_EQ(block.nshared.load(), 2);
    EXPECT_EQ(umqSock_->LoadSeqNum(), 1u);
    /* offer_total 计数 +1 */
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_GE(after.offer_total, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_OfferThenReadDone_ReleasesPin)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    ASSERT_EQ(fake_.posts.size(), 1u);
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(fake_.posts[0]->buf_data);
    EXPECT_EQ(block.nshared.load(), 2); /* pin 持有 */

    /* 模拟对端 READ_DONE: ReleasePinned 应解 pin 并 DecRef */
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&FakeUmqBufFree));
    TestBuf done;
    done.SetCtrlHdr(UBS_READ_DONE, 0, 0, 0, ctrl->seq);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &done.qbuf));
    EXPECT_EQ(block.nshared.load(), 1);
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_CoalescedTwoSmalls_OneBufOneSn)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t raw1[2000], raw2[2000];
    Block b1(reinterpret_cast<char *>(raw1), sizeof(raw1)), b2(reinterpret_cast<char *>(raw2), sizeof(raw2));
    memset(raw1, 0x11, sizeof(raw1));
    memset(raw2, 0x22, sizeof(raw2));
    ubs_segment_t segs[2] = {};
    segs[0].block = &b1;
    segs[0].len = 2000;
    segs[1].block = &b2;
    segs[1].len = 2000;
    ubs_data_list_t list = MakeList(segs, 2);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 2);
    EXPECT_EQ(fake_.post_buf_count, 1u);
    umq_buf_t *coalesced = fake_.posts[0];
    EXPECT_EQ(coalesced->data_size, 4000u);
    EXPECT_EQ(coalesced->is_coalesced_small, 1);
    EXPECT_EQ(memcmp(coalesced->buf_data, raw1, sizeof(raw1)), 0);
    EXPECT_EQ(memcmp(coalesced->buf_data + sizeof(raw1), raw2, sizeof(raw2)), 0);
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(coalesced->qbuf_ext);
    EXPECT_EQ(pro->imm.user_data, 0u);
    EXPECT_EQ(umqSock_->LoadSeqNum(), 1u); /* N 段共享 1 个 SN */
    /* coalesce 路径不 IncRef 源 block */
    EXPECT_EQ(b1.nshared.load(), 1);
    EXPECT_EQ(b2.nshared.load(), 1);
}

TEST_F(UbsBigdataTest, TrySenderPost_CoalesceBudgetBreak_TwoSingletonSmalls)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t raw1[4000], raw2[4000];
    Block b1(reinterpret_cast<char *>(raw1), sizeof(raw1)), b2(reinterpret_cast<char *>(raw2), sizeof(raw2));
    ubs_segment_t segs[2] = {};
    segs[0].block = &b1;
    segs[0].len = 4000;
    segs[1].block = &b2;
    segs[1].len = 4000; /* 4000+4000 > 4064 → 各为单段 */
    ubs_data_list_t list = MakeList(segs, 2);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 2);
    EXPECT_EQ(fake_.post_buf_count, 2u);
    EXPECT_EQ(umqSock_->LoadSeqNum(), 2u);
}

TEST_F(UbsBigdataTest, TrySenderPost_LargeThenSmall_OfferFlushedBeforeSmall)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t raw1[5000], raw2[100];
    Block b1(reinterpret_cast<char *>(raw1), sizeof(raw1)), b2(reinterpret_cast<char *>(raw2), sizeof(raw2));
    fake_.RegisterBacking(b1.data, b1.cap, 0x77);
    ubs_segment_t segs[2] = {};
    segs[0].block = &b1;
    segs[0].len = 5000;
    segs[1].block = &b2;
    segs[1].len = 100;
    ubs_data_list_t list = MakeList(segs, 2);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 2);
    EXPECT_EQ(fake_.post_buf_count, 2u);
    /* 链头是已 seal 的 offer,其后跟小包 */
    umq_buf_t *offer = fake_.posts[0];
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(offer->buf_data);
    EXPECT_EQ(ctrl->type, UBS_READ_OFFER);
    EXPECT_EQ(ctrl->nsegs, 1);
    EXPECT_EQ(offer->qbuf_next->data_size, 100u);
    EXPECT_EQ(umqSock_->LoadSeqNum(), 2u); /* offer 1 SN + small 1 SN */
}

TEST_F(UbsBigdataTest, TrySenderPost_SmallThenLarge_BatchTwoBufs)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t raw1[100], raw2[5000];
    Block b1(reinterpret_cast<char *>(raw1), sizeof(raw1)), b2(reinterpret_cast<char *>(raw2), sizeof(raw2));
    fake_.RegisterBacking(b2.data, b2.cap, 0x77);
    ubs_segment_t segs[2] = {};
    segs[0].block = &b1;
    segs[0].len = 100;
    segs[1].block = &b2;
    segs[1].len = 5000;
    ubs_data_list_t list = MakeList(segs, 2);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 2);
    EXPECT_EQ(fake_.post_buf_count, 2u);
    EXPECT_EQ(umqSock_->LoadSeqNum(), 2u);
}

TEST_F(UbsBigdataTest, TrySenderPost_33LargeSegs_TwoOffers)
{
    PrepareSendSocket();
    MountSendMocks();
    constexpr uint32_t SEG_LEN = UBS_SMALL_DATA_MAX + 1; /* 4065 */
    constexpr uint16_t NSEGS = 33;
    std::vector<uint8_t> raw(static_cast<size_t>(SEG_LEN) * NSEGS);
    Block block(reinterpret_cast<char *>(raw.data()), static_cast<uint32_t>(raw.size()));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[NSEGS] = {};
    for (uint16_t i = 0; i < NSEGS; ++i) {
        segs[i].block = &block;
        segs[i].offset = i * SEG_LEN;
        segs[i].len = SEG_LEN;
    }
    ubs_data_list_t list = MakeList(segs, NSEGS);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, static_cast<ssize_t>(NSEGS));
    EXPECT_EQ(fake_.post_buf_count, 2u); /* offer(32) + offer(1) */
    /* 32 封板: 第一份 offer 恰好 32 段 */
    umq_buf_t *first = fake_.posts[0];
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(first->buf_data);
    EXPECT_EQ(ctrl->nsegs, UBS_SEG_MAX);
    umq_buf_t *second = first->qbuf_next;
    auto *ctrl2 = reinterpret_cast<UbsCtrlHdr *>(second->buf_data);
    EXPECT_EQ(ctrl2->nsegs, 1);
    EXPECT_EQ(umqSock_->LoadSeqNum(), 2u);      /* 每份 offer 1 个 SN */
    EXPECT_EQ(block.nshared.load(), 1 + NSEGS); /* 测试 + 33 个 pin */
}

TEST_F(UbsBigdataTest, TrySenderPost_AllocFail_Small_Eio)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.alloc_fail_after = 0;
    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EIO);                 /* 无 errno 的失败路径收敛到 EIO */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 0u); /* SN 已回退 */
    EXPECT_EQ(fake_.free_count, 0u);
}

TEST_F(UbsBigdataTest, TrySenderPost_AllocFail_Offer_Eagain)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.alloc_fail_after = 0;
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EAGAIN); /* OfferBuilder::Alloc 置 EAGAIN */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 0u);
    EXPECT_EQ(fake_.free_count, 0u);
}

TEST_F(UbsBigdataTest, TrySenderPost_DataToHeadFail_OfferRollback)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.data_to_head_fail = true;
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EIO);                 /* Append 失败未设 errno → EIO */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 0u); /* offer SN 回退 */
    EXPECT_EQ(fake_.free_count, 1u);       /* ctrl buf 释放 */
    EXPECT_EQ(block.nshared.load(), 1);    /* pin 未发生 */
}

TEST_F(UbsBigdataTest, TrySenderPost_MempoolInfoGetFail_OfferRollback)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.info_get_fail = true;
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EIO);
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_BlobLenBelowMin_OfferRollback)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.blob_len = UBS_MEMPOOL_INFO_HDR_SIZE + TEST_URMA_SEG_T_SIZE - 1; /* 71 */
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EIO);
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_BlobLenAboveMax_OfferRollback)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.blob_len = UMQ_MEMPOOL_INFO_MAX_SIZE + 1; /* 1097 > 1096 */
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_FALSE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EIO);
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_PostAllRejected_ReturnsTrueEagain)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.post_ret = -EAGAIN;
    fake_.post_bad_index = 0; /* bad == head → 全部未提交 */
    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(fake_.free_count, 1u); /* 整批回滚 */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 0u);
    EXPECT_EQ(fake_.post_buf_count, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_PostEnomemBadNull_AllRejected)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.post_ret = -ENOMEM;
    fake_.post_bad_index = -1; /* bad == nullptr → 全部未提交 */
    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(fake_.free_count, 1u);
    EXPECT_EQ(umqSock_->LoadSeqNum(), 0u);
}

TEST_F(UbsBigdataTest, TrySenderPost_PartialAccept_TailRolledBack)
{
    PrepareSendSocket();
    MountSendMocks();
    fake_.post_ret = -EAGAIN;
    fake_.post_bad_index = 1; /* 第 2 个 buf 起未提交 */
    uint8_t raw[2500];
    Block b1(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[3] = {};
    for (int i = 0; i < 3; ++i) {
        segs[i].block = &b1;
        segs[i].len = 2500;
    }
    ubs_data_list_t list = MakeList(segs, 3);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1); /* 只接受 1 个 buf → 1 段 */
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(fake_.free_count, 2u);       /* 尾部 2 个 buf 回滚 */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 1u); /* 已提交 1 个 SN,回退 2 个 */
    /* 已接受 buf 的 qbuf_next 必须摘断,避免 UMQ 回收时走入已释放内存 */
    EXPECT_EQ(fake_.posts[0]->qbuf_next, nullptr);
}

TEST_F(UbsBigdataTest, TrySenderPost_BatchOver256_FirstChunkPosted)
{
    PrepareSendSocket();
    MountSendMocks();
    constexpr uint16_t NSEGS = 300;
    std::vector<uint8_t> raw(static_cast<size_t>(2500) * NSEGS);
    Block block(reinterpret_cast<char *>(raw.data()), static_cast<uint32_t>(raw.size()));
    ubs_segment_t segs[NSEGS] = {};
    for (uint16_t i = 0; i < NSEGS; ++i) {
        segs[i].block = &block;
        segs[i].offset = i * 2500;
        segs[i].len = 2500; /* 单段运行(2500*2 > 4064) → 300 个 buf */
    }
    ubs_data_list_t list = MakeList(segs, NSEGS);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    const size_t chunk = std::min(static_cast<size_t>(UMQ_BATCH_SIZE), static_cast<size_t>(GlobalSetting::UBS_TX_DEPTH));
    EXPECT_EQ(out, static_cast<ssize_t>(chunk)); /* min(256, TX_DEPTH=255) */
    EXPECT_EQ(fake_.post_buf_count, chunk);
    EXPECT_EQ(fake_.free_count, static_cast<size_t>(NSEGS) - chunk); /* 尾部回滚 */
    EXPECT_EQ(umqSock_->LoadSeqNum(), static_cast<uint32_t>(chunk));
    EXPECT_EQ(block.nshared.load(), 1 + static_cast<int>(chunk)); /* posted: IncRef无CQE DecRef; 尾部 IncRef+DecRef */
}

/* 发送侧按 UBS_TX_DEPTH 切片：1G 会封出数百 OFFER，单次不得硬顶过 SQ 深度。 */
TEST_F(UbsBigdataTest, TrySenderPost_TxDepthClamp_BatchClampedToTxDepth)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_TX_DEPTH = 8;
    constexpr uint16_t NSEGS = 20;
    std::vector<uint8_t> raw(static_cast<size_t>(2500) * NSEGS);
    Block block(reinterpret_cast<char *>(raw.data()), static_cast<uint32_t>(raw.size()));
    ubs_segment_t segs[NSEGS] = {};
    for (uint16_t i = 0; i < NSEGS; ++i) {
        segs[i].block = &block;
        segs[i].offset = i * 2500;
        segs[i].len = 2500;
    }
    ubs_data_list_t list = MakeList(segs, NSEGS);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 8);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(fake_.post_buf_count, 8u);
    EXPECT_EQ(fake_.free_count, static_cast<size_t>(NSEGS - 8));
    EXPECT_EQ(umqSock_->LoadSeqNum(), 8u);
}

TEST_F(UbsBigdataTest, TrySenderPost_GenCheckOn_HeadroomWritten)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    umqSock_->SetLocalRpcTimeoutMs(100); /* effective_timeout=100 → gen != 0 */
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    UbsBigdata::GenCheckStats before = GenStats();

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(fake_.posts[0]->buf_data);
    EXPECT_NE(ctrl->read_gen, 0u); /* 整块段 → 携带 gen */
    /* 头房(data-8)已写入同代 gen */
    const uint64_t headroom = *reinterpret_cast<const volatile uint64_t *>(rawHead);
    EXPECT_EQ(headroom, ctrl->read_gen);
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.offer_total - before.offer_total, 1u);
    EXPECT_EQ(after.gen_fallback - before.gen_fallback, 0u); /* 未走 fallback */
}

TEST_F(UbsBigdataTest, TrySenderPost_GenCheckOn_SlicedSeg_Fallback)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    umqSock_->SetLocalRpcTimeoutMs(100);
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    uint8_t sliced[5000];
    fake_.RegisterBacking(reinterpret_cast<char *>(sliced), sizeof(sliced), 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].start_pos = sliced; /* sliced seg → read_gen=0 fallback;len>4064 走大段 */
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    UbsBigdata::GenCheckStats before = GenStats();

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(fake_.posts[0]->buf_data);
    EXPECT_EQ(ctrl->read_gen, 0u);
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.gen_fallback - before.gen_fallback, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_GenCheckOn_NoHeadroomFlag_Fallback)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    umqSock_->SetLocalRpcTimeoutMs(100);
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw)); /* 无 GEN_HEADROOM flag */
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    UbsBigdata::GenCheckStats before = GenStats();

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(fake_.posts[0]->buf_data);
    EXPECT_EQ(ctrl->read_gen, 0u);
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.gen_fallback - before.gen_fallback, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_GenCheckOn_NoTimeout_Fallback)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true; /* 未设 local/peer 超时 → gen=0 */
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    UbsBigdata::GenCheckStats before = GenStats();

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.offer_total - before.offer_total, 1u);
    EXPECT_EQ(after.gen_fallback - before.gen_fallback, 1u);
}

TEST_F(UbsBigdataTest, TrySenderPost_UnifiedPollOn_NotifyPaths)
{
    /* UBS_TX_UNIFIED_POLL_ENABLED=true 时成功提交走 NotifyInflight/NotifyPosted */
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true;
    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
}

/* 控制帧槽位预留(UBSOCKET_BIG_CTRL_RESERVED_SLOTS): 发送端批量被裁剪到
 * avail - 预留 的额度内; 被裁掉的尾部走既有回滚(free + SN 回退) + errno=EAGAIN,
 * 由 KeepWrite 续发。预留槽位不被数据 SEND 占用。 */
TEST_F(UbsBigdataTest, TrySenderPost_CtrlReserve_BatchClampedToBudget)
{
    PrepareSendSocket();
    MountSendMocks();
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK);
    GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS = 2;
    DataTxOps *tx_ops = umqSock_->GetTxOps();
    ASSERT_NE(tx_ops, nullptr);
    tx_ops->tx_queue_avail_num_.store(5, std::memory_order_relaxed); /* 额度 = 5 - 2 = 3 */

    uint8_t raw[2500];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[6] = {};
    for (int i = 0; i < 6; ++i) {
        segs[i].block = &block;
        segs[i].len = 2500; /* 单段单 buf(2500*2 > 4064 不可合并) */
    }
    ubs_data_list_t list = MakeList(segs, 6);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 3); /* 只提交额度内 3 个 buf */
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(fake_.post_buf_count, 3u);
    EXPECT_EQ(fake_.free_count, 3u);       /* 尾部 3 个回滚 */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 3u); /* SN 回退 3 个 */
    EXPECT_EQ(tx_ops->tx_queue_avail_num_.load(), 2u); /* 5 - 3: 预留 2 槽未被数据占用 */
}

/* 控制帧槽位预留: 额度为 0(avail == 预留)时整批不 post(umq_post 不被调用),
 * 语义同整批被拒 EAGAIN,等 TX CQE 回补后由 KeepWrite 重试。 */
TEST_F(UbsBigdataTest, TrySenderPost_CtrlReserve_BudgetZero_WholeBatchEagainNoPost)
{
    PrepareSendSocket();
    MountSendMocks();
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK);
    GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS = 2;
    DataTxOps *tx_ops = umqSock_->GetTxOps();
    ASSERT_NE(tx_ops, nullptr);
    tx_ops->tx_queue_avail_num_.store(2, std::memory_order_relaxed); /* avail == 预留 → 额度 0 */

    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_EQ(fake_.posts.size(), 0u); /* 完全没有 post */
    EXPECT_EQ(fake_.free_count, 1u);   /* 整批回滚 */
    EXPECT_EQ(umqSock_->LoadSeqNum(), 0u);
    EXPECT_EQ(tx_ops->tx_queue_avail_num_.load(), 2u); /* 预留完好 */
}

TEST_F(UbsBigdataTest, TrySenderPost_SendPath_TxDepthAccounting)
{
    /* mock 模式可行性确认(批 1): GenerateSocketCommOps 装配
     * DataPlaneTable 条目后,成功提交路径应 fetch_sub(accepted_bufs)
     * 并 MarkActive 进 poller active 集合。顺序约束见 §3 分组标题注释。 */
    PrepareSendSocket();
    MountSendMocks();
    /* 装配数据面条目 + 壳(txw/rxw),等价 GenerateSocketCommOps 生产路径;
     * 仅 ReinitTxOps 时 txw 壳的 tx_ops_ 仍为默认 nullptr,GetTxOps() 会取空 */
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK);
    uint8_t raw[100];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 100;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;

    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    DataTxOps *tx_ops = umqSock_->GetTxOps();
    ASSERT_NE(tx_ops, nullptr);
    EXPECT_EQ(tx_ops->tx_queue_avail_num_.load(), GlobalSetting::UBS_TX_DEPTH - 1);
    /* MarkActive 已把 socket 挂入 poller active 集合(CQE 回补时 fetch_add) */
    auto &poller = TxCqePoller::Instance();
    std::lock_guard<std::mutex> lk(poller.active_mutex_);
    bool found = false;
    for (auto &s : poller.active_) {
        if (s.Get() == umqSock_.Get()) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

/* ================================================================== */
/* §4 接收与完成路径组(批 2): DoReadOffer(经 HandleRxControl 正例)   */
/* → HandleTxCompletion(READ CQE 驱动)→ FinalizeIo                    */
/* ================================================================== */
/* mock 策略: 全链——mempool import 三件套(state_check/info_set/       */
/* get_remote_fields) + buf_alloc/post/free,READ dest buf 由 fake 池   */
/* 提供(total_data_size==request_size 贴近真实池语义,过                */
/* ValidateStandaloneReadQbufChain);CQE 用 slot->pending 同一 qbuf     */
/* 注入 status 模拟。 */

TEST_F(UbsBigdataTest, HandleRxControl_OfferValid_SingleSeg_Posted)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = true; /* 覆盖 DoReadOffer NotifyInflight */
    TestBuf tbuf;
    constexpr uint64_t TEST_SEQ = 42;
    constexpr uint32_t SEG_LEN = 4065; /* > UBS_SMALL_DATA_MAX: 必须走 offer 路径 */
    BuildReadOffer(tbuf, 1, TEST_SEQ, 0, 72, SEG_LEN);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* 1 段 READ WR 已构建并 post,remote_sge 取自 offer 的 UbsSeg 与
     * get_remote_fields 提取的 mp/token 三元组 */
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(fake_.post_buf_count, 1u);
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(fake_.posts[0]->qbuf_ext);
    EXPECT_EQ(pro->opcode, UMQ_OPC_READ);
    EXPECT_EQ(pro->remote_sge.addr, TEST_REMOTE_ADDR_BASE);
    EXPECT_EQ(pro->remote_sge.length, SEG_LEN);
    EXPECT_EQ(pro->remote_sge.mempool_id, 7u);
    EXPECT_EQ(pro->remote_sge.token_id, 8u);
    EXPECT_EQ(pro->remote_sge.token_value, 9u);
    EXPECT_NE(pro->user_ctx, 0ULL); /* 指向稳定 UbsBigQbufSlot */
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_NE(umqSock_->GetBigdataState(), nullptr);

    /* 交付 CQE 完成事务, 避免 ctx 滞留 active_io(清理前主动 finalize) */
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u);                    /* offer RX buf 由 FinalizeIo 释放 */
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false; /* 恢复默认, 不污染后续用例 */
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferGenCheck_ReadRangeExtended8B)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    constexpr uint64_t TEST_SEQ = 7;
    constexpr uint64_t TEST_GEN = 0x12345678;
    constexpr uint32_t SEG_LEN = 4065;
    BuildReadOffer(tbuf, 1, TEST_SEQ, TEST_GEN, 72, SEG_LEN);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* gen check 开启: READ 范围前移 8B 拉回头房, 长度 +8 */
    ASSERT_EQ(fake_.posts.size(), 1u);
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(fake_.posts[0]->qbuf_ext);
    EXPECT_EQ(pro->remote_sge.length, SEG_LEN + 8);
    EXPECT_EQ(pro->remote_sge.addr, TEST_REMOTE_ADDR_BASE - 8);
    /* READ dest buf 容量按扩展后长度申请(真实池语义 total_data_size) */
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    EXPECT_EQ(dests[0]->total_data_size, SEG_LEN + 8);

    /* 池 buf 全零 ≠ expect_gen → CQE 后静默丢弃(gen mismatch 路径, 见下组) */
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(fake_.free_count, 2u);
    EXPECT_TRUE(umqSock_->rxQueue->Empty());
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferSegIdxMismatch_AbortSent)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    constexpr uint64_t TEST_SEQ = 42;
    BuildReadOffer(tbuf, 1, TEST_SEQ, 0, 72, 4065);
    /* 破坏 1:1 索引不变量: HandleRxControl 门禁不查 idx, ParseReadOffer 拒 */
    auto *segs = reinterpret_cast<UbsSeg *>(tbuf.data + UBS_CTRL_HDR_SIZE);
    segs[0].mempool_info_idx = 5;

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* malformed offer → READ_ABORT(seq 回显 offer 的 seq) + offer RX buf 释放 */
    ASSERT_EQ(fake_.posts.size(), 1u);
    auto *abort_hdr = FindBufWithType(fake_.posts[0], UBS_READ_ABORT);
    ASSERT_NE(abort_hdr, nullptr);
    EXPECT_EQ(reinterpret_cast<const UbsCtrlHdr *>(abort_hdr->buf_data)->seq, TEST_SEQ);
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferMempoolStateErr_AbortSent)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.mempool_state_ret = UMQ_REMOTE_MEMPOOL_STATE_ERR;
    TestBuf tbuf;
    constexpr uint64_t TEST_SEQ = 5;
    BuildReadOffer(tbuf, 1, TEST_SEQ, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_NE(FindBufWithType(fake_.posts[0], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 1u); /* offer RX buf */
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferNeedImport_ImportsThenPosts)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.mempool_state_ret = UMQ_REMOTE_MEMPOOL_STATE_NEED_IMPORT;
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 5, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* NEED_IMPORT → umq_mempool_info_set 逐段导入后继续 post READ */
    EXPECT_EQ(fake_.info_set_count, 1u);
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[0]->qbuf_ext)->opcode, UMQ_OPC_READ);
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferNeedReimport_ImportsThenPosts)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.mempool_state_ret = UMQ_REMOTE_MEMPOOL_STATE_NEED_REIMPORT;
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 6, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* NEED_REIMPORT(version grew)→ 与 NEED_IMPORT 同路径: umq_mempool_info_set
     * 逐段导入后继续 post READ(生产四态: REUSE 跳过/ERR ABORT/其余走 info_set) */
    EXPECT_EQ(fake_.info_set_count, 1u);
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[0]->qbuf_ext)->opcode, UMQ_OPC_READ);
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferImportFail_AbortSent)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.mempool_state_ret = UMQ_REMOTE_MEMPOOL_STATE_NEED_IMPORT;
    fake_.info_set_ret = -1;
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 5, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    EXPECT_EQ(fake_.info_set_count, 1u);
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_NE(FindBufWithType(fake_.posts[0], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferReadBufAllocFail_CleanupNoPost)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.alloc_fail_after = 0; /* READ dest 第一个 alloc 即失败; ABORT 的 ctrl alloc 也失败 */
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 9, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* alloc 失败 → 清理已分配槽位 + 释放 offer, ABORT 因 alloc 失败发不出 */
    EXPECT_EQ(fake_.posts.size(), 0u);
    EXPECT_EQ(fake_.free_count, 1u); /* offer RX buf */
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferGetRemoteFieldsFail_AbortSent)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.get_remote_fields_ret = -1;
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 11, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_NE(FindBufWithType(fake_.posts[0], UBS_READ_ABORT), nullptr);
    /* 修复后(issue 01): 完整清理 dest + offer_rx_buf, ctx 经 DeleteIoUctx 释放 */
    EXPECT_EQ(fake_.free_count, 2u);
}

/* 多段 offer 中途 get_remote_fields 失败(第 2 段): 已入队段全部释放(issue 01) */
TEST_F(UbsBigdataTest, HandleRxControl_OfferGetRemoteFieldsFailMidChain_QueuedSegsFreed)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.get_remote_fields_ret = -1;
    fake_.get_remote_fields_fail_after = 2; /* 第 1 段成功, 第 2 段起失败 */
    TestBuf tbuf;
    BuildReadOffer(tbuf, 2, 12, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* 第 1 段 READ WR 已构建入队, 第 2 段失败: 无 READ post, 全部清理 */
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_NE(FindBufWithType(fake_.posts[0], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 3u); /* 已入队 dest + 当前 dest + offer */
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferPostEagain_NothingSubmitted_DeferredNoAbort)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0; /* bad==head: 未提交任何 WR(批2 小结 mock 语义结论) */
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 21, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* 无任何提交的 EAGAIN → ctx 入 pending_reads: 不 ABORT、不释放、不断链,
     * 等 RetryPendingReads(下个 TX 完成周期)重投 */
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[0]->qbuf_ext)->opcode, UMQ_OPC_READ);
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
    /* TearDown 的 CleanupSocketState 会排空 pending_reads 并释放 buf */
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferPostPartialSubmit_DeferredThenRetryCompletes)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 1; /* 2 段链: 第 1 段被接受, 第 2 段 bad */
    TestBuf tbuf;
    BuildReadOffer(tbuf, 2, 31, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* 部分提交 → ctx 入 pending_reads: 不 ABORT、不断链, 等 RetryPendingReads 续投 */
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    fake_.post_ret = 0;
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 2u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    EXPECT_TRUE(SimulateReadCqe(dests[1], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    ASSERT_EQ(fake_.posts.size(), 3u);
    EXPECT_NE(FindBufWithType(fake_.posts[2], UBS_READ_DONE), nullptr);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u); /* offer RX buf */
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferPostOtherError_NothingSubmitted_AbortAndClose)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -EPIPE;  /* 非可重试错误, 且无任何提交 → 立即 finalize-failed */
    fake_.post_bad_index = 0; /* bad==head: 未提交任何 WR(与真实 UMQ 语义一致) */
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 41, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* wr_total=0 → FinalizeIo 立即运行: ABORT + 断链(硬件/协议错误语义) */
    ASSERT_EQ(fake_.posts.size(), 2u); /* READ 尝试 + ABORT 尝试 */
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(umqSock_->State(), SOCK_STAT_CLOSE);
    EXPECT_EQ(fake_.free_count, 3u); /* dest + ABORT ctrl(发送失败释放) + offer */
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferPostEagain_PendingFull_DroppedAndClosed)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0; /* bad==head: 每次 post 都未提交任何 WR */
    TestBuf tbuf;
    /* 填满 pending_reads(TEST_PENDING_READ_MAX=UBS_BIG_PENDING_READ_MAX=2048),
     * 第 2049 个 offer 入队失败 → 按"队列满 + 无 READ 提交"丢弃, 回落
     * finalize-as-failed */
    for (size_t i = 0; i < TEST_PENDING_READ_MAX; ++i) {
        BuildReadOffer(tbuf, 1, static_cast<uint64_t>(51 + i), 0, 72, 4065);
        EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    }
    BuildReadOffer(tbuf, 1, 200, 0, 72, 4065);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    EXPECT_EQ(umqSock_->State(), SOCK_STAT_CLOSE);
    ASSERT_EQ(fake_.posts.size(), TEST_PENDING_READ_MAX + 2u); /* 2048 次 READ 尝试 + 1 次 ABORT */
    EXPECT_NE(FindBufWithType(fake_.posts[TEST_PENDING_READ_MAX + 1], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 2u); /* 第 2049 个 dest + 第 2049 个 offer */
}

/* 控制帧槽位预留(UBSOCKET_BIG_CTRL_RESERVED_SLOTS): 额度不足整链时 READ
 * 完全不 post,直接入 pending_reads; RetryPendingReads 同受额度门限让路;
 * 额度恢复后续投成功。 */
TEST_F(UbsBigdataTest, HandleRxControl_CtrlReserve_BudgetInsufficient_OfferDeferredWithoutPost)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK);
    GlobalSetting::UBS_BIG_CTRL_RESERVED_SLOTS = 2;
    DataTxOps *tx_ops = umqSock_->GetTxOps();
    ASSERT_NE(tx_ops, nullptr);
    tx_ops->tx_queue_avail_num_.store(2, std::memory_order_relaxed); /* avail == 预留 → 额度 0 */

    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 71, 0, 72, 4065);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));

    /* 额度 0 → umq_post 未被调用,ctx 入 pending_reads(不 ABORT、不断链) */
    EXPECT_EQ(fake_.posts.size(), 0u);
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    /* 额度未恢复: 重试轮同样让路,仍不 post */
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 0u);

    /* 额度恢复(CQE 回补): 续投成功 */
    tx_ops->tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH, std::memory_order_relaxed);
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[0]->qbuf_ext)->opcode, UMQ_OPC_READ);
    /* READ post 记账: TX_DEPTH - 1 */
    EXPECT_EQ(tx_ops->tx_queue_avail_num_.load(), GlobalSetting::UBS_TX_DEPTH - 1);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_NullQbuf_ReturnsFalse)
{
    uint32_t span = 77;
    int deferred_fd = 5;
    EXPECT_FALSE(UbsBigdata::HandleTxCompletion(umqSock_.Get(), nullptr, &span, &deferred_fd));
    EXPECT_EQ(span, 0u);
    EXPECT_EQ(deferred_fd, -1);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_NoSlot_FreesBuf)
{
    MountReceiveMocks();
    TestBuf tbuf;
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(tbuf.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_READ;
    tbuf.qbuf.status = 0;

    uint32_t span = 99;
    int deferred_fd = -1;
    EXPECT_TRUE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &tbuf.qbuf, &span, &deferred_fd));

    /* user_ctx 无槽位指向 → 无法归属事务, 直接释放 CQE buf */
    EXPECT_EQ(fake_.free_count, 1u);
    EXPECT_EQ(span, 0u);
    EXPECT_EQ(deferred_fd, -1);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_SingleWr_Success_DeliversAndDone)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK);
    umqSock_->SetPeerRpcTimeoutMs(500); /* peer 超时 > 0 → ctx 设置 deadline 分支 */
    TestBuf tbuf;
    constexpr uint64_t TEST_SEQ = 60;
    constexpr uint32_t FIRST_SN = 0x12345;
    BuildReadOffer(tbuf, 1, TEST_SEQ, 0, 72, 4065);
    reinterpret_cast<umq_buf_pro_t *>(tbuf.qbuf.qbuf_ext)->imm.user_data = FIRST_SN;

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    ASSERT_EQ(fake_.posts.size(), 1u);

    /* READ post 已对 tx_queue_avail_num_ 记账(fetch_sub 1), CQE 由调用方
     * 经 deferred_progress_fd 回补 fetch_add */
    DataTxOps *tx_ops = umqSock_->GetTxOps();
    ASSERT_NE(tx_ops, nullptr);
    EXPECT_EQ(tx_ops->tx_queue_avail_num_.load(), GlobalSetting::UBS_TX_DEPTH - 1);

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    EXPECT_EQ(deferred_fd, TEST_FD);

    /* FinalizeIo: DONE 已 post(seq 回显), offer RX buf 释放, READ buf 入 rxQueue */
    ASSERT_EQ(fake_.posts.size(), 2u);
    auto *done = FindBufWithType(fake_.posts[1], UBS_READ_DONE);
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(reinterpret_cast<const UbsCtrlHdr *>(done->buf_data)->seq, TEST_SEQ);
    EXPECT_EQ(fake_.free_count, 1u);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(dests[0]->qbuf_ext);
    EXPECT_EQ(pro->imm.user_data, FIRST_SN); /* 整链盖一个 first_sn */
    EXPECT_EQ(dests[0]->io_direction, UMQ_IO_RX);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_StatusError_AbortAndClose)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 70, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);

    /* READ 以错误状态完成 → failed=true → FinalizeIo failed 路径:
     * ABORT + 断链(硬件/协议错误语义) */
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 12, &span, &deferred_fd)); /* status:12 = SQ full */
    EXPECT_EQ(span, 1u);
    EXPECT_EQ(deferred_fd, TEST_FD);
    EXPECT_EQ(umqSock_->State(), SOCK_STAT_CLOSE);
    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 2u); /* dest + offer */
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_RnrNoop_ThenFinalCqeFinalizes)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 80, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);

    /* status=99 软反压通知: 不累计完成计数、不释放, 等 bondp 重试后的最终 CQE */
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_FALSE(SimulateReadCqe(dests[0], UMQ_BUF_RNR_RETRY_CNT_EXC, &span, &deferred_fd));
    EXPECT_EQ(span, 0u);
    EXPECT_EQ(deferred_fd, -1);
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_TRUE(umqSock_->rxQueue->Empty());

    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_DONE), nullptr);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_TwoWr_SecondCqeFinalizes)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    BuildReadOffer(tbuf, 2, 90, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(fake_.post_buf_count, 2u); /* 2 段链一次 post */

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 2u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    /* 中间 CQE: completed(1) != wr_total(2) → 不 finalize, 不交付 */
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_TRUE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 0u);

    EXPECT_TRUE(SimulateReadCqe(dests[1], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_DONE), nullptr);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_GenMismatch_DiscardNoAbort)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    constexpr uint64_t TEST_GEN = 0xABCD1234;
    BuildReadOffer(tbuf, 1, 100, TEST_GEN, 72, 4065);
    UbsBigdata::GenCheckStats before = GenStats();

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(dests[0]->qbuf_ext)->remote_sge.length, 4065u + 8);

    /* 池 buf 全零(≠ expect_gen)= 发送方已超时的陈旧 READ → 静默丢弃:
     * 不 ABORT(发送方已超时)、不断链(非硬件错误) */
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
    ASSERT_EQ(fake_.posts.size(), 1u); /* 无 DONE/ABORT */
    EXPECT_TRUE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 2u); /* dest + offer */
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.gen_mismatch - before.gen_mismatch, 1u);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_GenMatch_StripsHeadroomAndDelivers)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    UbsBigdata::GenCheckStats before = GenStats();
    TestBuf tbuf;
    constexpr uint64_t TEST_GEN = 0xABCD1234;
    BuildReadOffer(tbuf, 1, 110, TEST_GEN, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    /* 发送方在 dest buf 头 8B 写入 gen(池 buf 默认全零, 预填匹配值) */
    *reinterpret_cast<uint64_t *>(dests[0]->buf_data) = TEST_GEN;

    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    /* 8B 头房已剥离, 交付内容为纯 payload */
    EXPECT_EQ(dests[0]->data_size, 4065u);
    EXPECT_EQ(dests[0]->total_data_size, 4065u);
    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_DONE), nullptr);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u);
    /* gen_mismatch 是全局累计计数: 必须用 delta 而非绝对值断言 */
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.gen_mismatch - before.gen_mismatch, 0u);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_AfterCleanupState_DestroyingPathFreesSilently)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 140, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);

    /* socket 清理: 在途 READ 仅标记 failed, 由后续终端 CQE 释放(destroying 分支:
     * 不做 state 访问, 不发 DONE/ABORT, 不触碰 socket 状态) */
    UbsBigdata::CleanupSocketState(umqSock_.Get());
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(fake_.free_count, 2u); /* dest + offer */
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_CtrlSqe_Valid_FreesAndSpans)
{
    PrepareSendSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    tbuf.SetCtrlHdr(UBS_READ_DONE, 0, 0, 0, 42);
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(tbuf.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    pro->imm_data = p::mark_big_ctrl(0);

    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &tbuf.qbuf, &span, &deferred_fd));

    /* 控制 SEND 完成: 释放 ctrl buf, span=1, deferred_progress_fd=fd */
    EXPECT_EQ(fake_.free_count, 1u);
    EXPECT_EQ(span, 1u);
    EXPECT_EQ(deferred_fd, TEST_FD);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_CtrlSqe_BufDataNull_ReturnsFalse)
{
    PrepareSendSocket();
    TestBuf tbuf;
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(tbuf.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    pro->imm_data = p::mark_big_ctrl(0);
    tbuf.qbuf.buf_data = nullptr;

    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_FALSE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &tbuf.qbuf, &span, &deferred_fd));
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_EQ(span, 0u);
    EXPECT_EQ(deferred_fd, -1);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_CtrlSqe_Rnr_ReturnsFalse)
{
    PrepareSendSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    tbuf.SetCtrlHdr(UBS_READ_ABORT, 0, 0, 0, 3);
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(tbuf.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    pro->imm_data = p::mark_big_ctrl(0);
    tbuf.qbuf.status = UMQ_BUF_RNR_RETRY_CNT_EXC;

    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_FALSE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &tbuf.qbuf, &span, &deferred_fd));
    EXPECT_EQ(fake_.free_count, 0u); /* RNR 中不能提前 free, 防 bondp 复用 double free */
    EXPECT_EQ(span, 0u);
    EXPECT_EQ(deferred_fd, -1);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_SendCqe_NoPending_ReturnsFalse)
{
    PrepareSendSocket();
    MountReceiveMocks();
    /* 先经 READ_DONE 控制消息惰性创建 bigdata state(普通 SEND 不建 state) */
    TestBuf tbuf;
    tbuf.SetCtrlHdr(UBS_READ_DONE, 0, 0, 0, 1);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    EXPECT_NE(umqSock_->GetBigdataState(), nullptr);
    EXPECT_EQ(fake_.free_count, 1u); /* READ_DONE 消费 */

    /* 普通数据 SEND 完成(opcode SEND_IMM + bit20 清除): 无 pending → 不触发
     * retry, 返回 false 交给 ProcessTxCqe 处理 */
    TestBuf send;
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(send.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    send.qbuf.buf_data = reinterpret_cast<char *>(send.data);
    send.qbuf.data_size = 100;
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_FALSE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &send.qbuf, &span, &deferred_fd));
    EXPECT_EQ(span, 0u);
    EXPECT_EQ(deferred_fd, -1);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_SendCqe_PendingReads_RetriesAndFinalizes)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 130, 0, 72, 4065);
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0; /* bad==head: 未提交任何 WR(批2 小结 mock 语义结论) */
    EXPECT_TRUE(
        UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf)); /* ctx 入 pending_reads */
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(fake_.free_count, 0u);

    /* 普通 SEND 完成 → 检测到 pending_reads 非空 → RetryPendingReads 重投成功 */
    fake_.post_ret = 0;
    TestBuf send;
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(send.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    send.qbuf.buf_data = reinterpret_cast<char *>(send.data);
    send.qbuf.data_size = 100;
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_FALSE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &send.qbuf, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 2u); /* 重投的 READ */
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[1]->qbuf_ext)->opcode, UMQ_OPC_READ);

    /* 重投的 READ 到达终端 CQE → finalize 成功 */
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 3u);
    EXPECT_NE(FindBufWithType(fake_.posts[2], UBS_READ_DONE), nullptr);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_SendCqe_PendingReads_PersistentEagain_StaysQueued)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK);
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 150, 0, 72, 4065);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf)); /* ctx 入 pending */

    /* SEND CQE → 重投仍 EAGAIN(bad==head, 未提交) → ctx 原样放回队首等下一轮 */
    uint32_t span = 0;
    int deferred_fd = -1;
    TestBuf send;
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(send.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    send.qbuf.buf_data = reinterpret_cast<char *>(send.data);
    send.qbuf.data_size = 100;
    EXPECT_FALSE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &send.qbuf, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 2u); /* 初投 READ + 重投尝试 */
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE); /* 无最终化, 不 ABORT 不断链 */

    /* 下一轮 SEND CQE 重投成功 → 事务继续, 等待其 READ CQE */
    fake_.post_ret = 0;
    EXPECT_FALSE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &send.qbuf, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 3u);
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[2]->qbuf_ext)->opcode, UMQ_OPC_READ);

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 4u);
    EXPECT_NE(FindBufWithType(fake_.posts[3], UBS_READ_DONE), nullptr);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u);
}

TEST_F(UbsBigdataTest, HandleTxCompletion_SendCqe_PendingReads_OtherError_OnlyCurrentFinalizes)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    TestBuf tbuf;
    /* 2 个事务因 EAGAIN 挂起 */
    for (uint64_t seq = 160; seq <= 161; ++seq) {
        BuildReadOffer(tbuf, 1, seq, 0, 72, 4065);
        EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    }
    ASSERT_EQ(fake_.posts.size(), 2u);

    /* SEND CQE → 重投遇非可重试错误(-EPIPE, bad==head) → 仅当前 ctx 标 failed;
     * 其余 pending 保留, 待 SQ 恢复后重投 */
    fake_.post_ret = -EPIPE;
    uint32_t span = 0;
    int deferred_fd = -1;
    TestBuf send;
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(send.qbuf.qbuf_ext);
    memset(pro, 0, sizeof(*pro));
    pro->opcode = UMQ_OPC_SEND_IMM;
    send.qbuf.buf_data = reinterpret_cast<char *>(send.data);
    send.qbuf.data_size = 100;
    EXPECT_FALSE(
        UbsBigdata::HandleTxCompletion(umqSock_.Get(), &send.qbuf, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 3u); /* 初投2 + 重投尝试1 */
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE); /* 第一个仍在等终端 CQE */
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_))); /* 第二个仍在队列 */

    /* 第二个 ctx 在 socket 关闭前仍可重投成功 */
    fake_.post_ret = 0;
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    ASSERT_EQ(fake_.posts.size(), 4u);
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 2u);
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(umqSock_->State(), SOCK_STAT_CLOSE);
    ASSERT_EQ(fake_.posts.size(), 5u);
    EXPECT_NE(FindBufWithType(fake_.posts[4], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 2u);
    EXPECT_TRUE(SimulateReadCqe(dests[1], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 6u);
    EXPECT_NE(FindBufWithType(fake_.posts[5], UBS_READ_DONE), nullptr);
    EXPECT_EQ(fake_.free_count, 3u); /* dest(160) + offer(160) + offer(161) */
}

TEST_F(UbsBigdataTest, HandleRxControl_OfferPostOtherError_BadNullAssumesAllSubmitted)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -EPIPE;
    /* post_bad_index=-1(默认): UMQ 未回传 bad → 代码按"全部已提交"处理
     * (submitted_wrs=wr_slots.size()), 等终端 CQE 而非立即 finalize */
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 41, 0, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);

    fake_.post_ret = 0;
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(umqSock_->State(), SOCK_STAT_CLOSE);
    EXPECT_EQ(fake_.free_count, 2u); /* dest + offer */
}

TEST_F(UbsBigdataTest, HandleRxControl_SocketClosed_ReturnsFalse)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    umqSock_->State(SOCK_STAT_CLOSE);
    TestBuf tbuf;
    tbuf.SetCtrlHdr(UBS_READ_DONE, 0, 0, 0, 1);

    EXPECT_FALSE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    EXPECT_EQ(fake_.free_count, 0u); /* 未消费未释放 */
}

TEST_F(UbsBigdataTest, HandleTxCompletion_ReadCqe_GenCheckDataSizeUnder8_Discards)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    UbsBigdata::GenCheckStats before = GenStats();
    TestBuf tbuf;
    constexpr uint64_t TEST_GEN = 0xDEADBEEF;
    BuildReadOffer(tbuf, 1, 111, TEST_GEN, 72, 4065);

    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    /* 发送方未能写回头房(短读): data_size < 8 → gen 校验视为不匹配。
     * FindReadWrTail 要求 data_size == total_data_size 链校验才过,
     * 两个字段必须同步改。 */
    dests[0]->data_size = 4;
    dests[0]->total_data_size = 4;

    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(span, 1u);
    ASSERT_EQ(fake_.posts.size(), 1u); /* 无 DONE/ABORT: 静默丢弃 */
    EXPECT_TRUE(umqSock_->rxQueue->Empty());
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
    EXPECT_EQ(fake_.free_count, 2u); /* dest + offer */
    /* data_size<8 分支不计入 gen_mismatch 统计(仅 got!=expect 分支计数) */
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.gen_mismatch - before.gen_mismatch, 0u);
}

/* ================================================================== */
/* §5 超时/回压/清理组(批 3)                                            */
/* 覆盖: SweepExpiredForSocket 两阶段、SweepExpiredCtxs、               */
/* RetryPendingReadsForSocket、DrainDeferredControls(经                 */
/* HandleFlowControlUpdate)、NeedsPollerAttention、CleanupSocketState、 */
/* GetGenCheckStats 计数增减。                                         */
/* 确定性超时方法: UBS_BIG_PIN_TIMEOUT_MARGIN_MS=0 + 1ms 超时 +         */
/* SleepPastDeadline(5ms)→ 已过期; 100ms 超时不 sleep → 未过期;         */
/* UBS_GRACE_MS=0/1000 控制 stage-2 是否触发。                          */
/* ================================================================== */

/* ---------------- §5.1 SweepExpiredForSocket 两阶段 ---------------- */

TEST_F(UbsBigdataTest, SweepExpiredForSocket_NullSock_Returns)
{
    UbsBigdata::SweepExpiredForSocket(SocketPtr());
}

TEST_F(UbsBigdataTest, SweepExpiredForSocket_NoState_Returns)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    EXPECT_EQ(umqSock_->GetBigdataState(), nullptr);

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 0u);
}

/* 特性关闭时 sweep 整体跳过: 即使有带 deadline 的 pinned 条目也不清头房 */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_FeatureOff_SkipsEvenWithDeadline)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    PostPinOffer(block, 5000, 1); /* deadline = now + 1ms */
    const uint64_t gen = *reinterpret_cast<const volatile uint64_t *>(rawHead);
    EXPECT_NE(gen, 0u);
    SleepPastDeadline();
    UbsBigdata::GenCheckStats before = GenStats();

    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = false;
    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_));

    EXPECT_EQ(*reinterpret_cast<const volatile uint64_t *>(rawHead), gen); /* 头房未被清 */
    EXPECT_EQ(block.nshared.load(), 2);                                    /* pin 未释放 */
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.pin_timeout - before.pin_timeout, 0u);
}

/* 无 rpc_timeout 时 30s pin-deadline fallback: 立即 sweep 未到期 → stage-1 不触发 */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_NoDeadlines_FastPathSkips)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    EXPECT_NE(umqSock_->GetBigdataState(), nullptr);
    UbsBigdata::GenCheckStats before = GenStats();

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_));

    EXPECT_EQ(block.nshared.load(), 2); /* 30s fallback 未到期 */
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.pin_timeout - before.pin_timeout, 0u);
}

/* deadline 未到达(相邻值): stage-1 不触发, 头房 gen 保留 */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_PinDeadlineNotYetReached_NoStage1)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    PostPinOffer(block, 5000, 100); /* deadline = now + 100ms(未到达) */
    const uint64_t gen = *reinterpret_cast<const volatile uint64_t *>(rawHead);
    UbsBigdata::GenCheckStats before = GenStats();

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_)); /* 立即 sweep */

    EXPECT_EQ(*reinterpret_cast<const volatile uint64_t *>(rawHead), gen);
    EXPECT_EQ(block.nshared.load(), 2);
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.pin_timeout - before.pin_timeout, 0u);
}

/* stage-1: 清头房 gen(晚到 READ 校验失败)+ 计数 +1, blocks 仍被 pin */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_PinStage1_HeadroomClearedCounterUp)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    PostPinOffer(block, 5000, 1);
    const uint64_t gen = *reinterpret_cast<const volatile uint64_t *>(rawHead);
    EXPECT_NE(gen, 0u);
    SleepPastDeadline();
    UbsBigdata::GenCheckStats before = GenStats();

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_));

    EXPECT_EQ(*reinterpret_cast<const volatile uint64_t *>(rawHead), 0u); /* 头房 gen 清零 */
    EXPECT_EQ(block.nshared.load(), 2);                                   /* 仍在 grace 期, blocks 未释放 */
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.pin_timeout - before.pin_timeout, 1u);
}

/* stage-2 未到: grace 未过 → blocks 保持 pin, 不触发第二次 sweep */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_PinStage2_GraceNotElapsed_BlocksStay)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_GRACE_MS = 1000;
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    PostPinOffer(block, 5000, 1);
    SleepPastDeadline();

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_)); /* stage-1 */
    EXPECT_EQ(*reinterpret_cast<const volatile uint64_t *>(rawHead), 0u);
    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_)); /* grace 未到 */

    EXPECT_EQ(block.nshared.load(), 2); /* blocks 仍被 pin */
    EXPECT_EQ(fake_.dealloc_count, 0u);
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
}

/* stage-2 触发: grace=0 → 第二次 sweep 即 DecRef + erase; 引用归零 →
 * ::ubsocket_iobuf_deallocate 被调; 计数器归零 → 不再需要 poller 注意 */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_PinStage2_GraceElapsed_BlocksReleased)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_GRACE_MS = 0;
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000, 0); /* init_nshared=0 → DecRef 归零 → deallocate */
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    PostPinOffer(block, 5000, 1);
    EXPECT_EQ(block.nshared.load(), 1); /* pin 持有 1 个引用 */
    SleepPastDeadline();
    UbsBigdata::GenCheckStats before = GenStats();
    const size_t deallocBefore = fake_.dealloc_count;

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_)); /* stage-1 */
    EXPECT_EQ(*reinterpret_cast<const volatile uint64_t *>(rawHead), 0u);
    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_)); /* stage-2 */

    EXPECT_EQ(block.nshared.load(), 0);
    EXPECT_EQ(fake_.dealloc_count - deallocBefore, 1u); /* 归零 → ::ubsocket_iobuf_deallocate */
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.pin_timeout - before.pin_timeout, 1u);
}

/* ---------------- §5.2 SweepExpiredCtxs ---------------- */

/* pending ctx 过期: 直接释放 dest + offer, rx_ctx_timeout +1, 队列清空 */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_PendingCtxExpired_FreedAndCounterUp)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 0;
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(171, 1); /* peer 超时 1ms → ctx 带 deadline; EAGAIN → 未提交入 pending_reads */
    ASSERT_EQ(fake_.posts.size(), 1u);
    EXPECT_EQ(fake_.free_count, 0u);
    SleepPastDeadline();
    UbsBigdata::GenCheckStats before = GenStats();

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_));

    EXPECT_EQ(fake_.free_count, 2u); /* dest + offer */
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.rx_ctx_timeout - before.rx_ctx_timeout, 1u);
}

/* deadline 未到达(相邻值): ctx 留在队列, 计数不动 */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_PendingCtxNotYetExpired_StaysQueued)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 0;
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(172, 100); /* deadline = now + 100ms(未到达) */
    UbsBigdata::GenCheckStats before = GenStats();

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_)); /* 立即 sweep */

    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_))); /* 仍在队 */
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.rx_ctx_timeout - before.rx_ctx_timeout, 0u);
}

/* active ctx 过期: 仅标 failed(不释放); 双重 sweep 不重复计数; 终端 CQE →
 * failed 路径: 释放 dest + offer + ABORT + 断链 */
TEST_F(UbsBigdataTest, SweepExpiredForSocket_ActiveCtxExpired_MarkedFailedNoFree)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 0;
    OfferIntoActive(173, 1); /* peer 超时 1ms; post 成功 → active_io 带 deadline */
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    ASSERT_EQ(fake_.posts.size(), 1u);
    SleepPastDeadline();
    UbsBigdata::GenCheckStats before = GenStats();

    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.free_count, 0u); /* 在途 WR: 只标 failed, 不释放 */
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
    UbsBigdata::GenCheckStats after = GenStats();
    EXPECT_EQ(after.rx_ctx_timeout - before.rx_ctx_timeout, 1u);

    /* 已标 failed 的 ctx 不再重复计数 */
    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    UbsBigdata::GenCheckStats after2 = GenStats();
    EXPECT_EQ(after2.rx_ctx_timeout - after.rx_ctx_timeout, 0u);

    /* 终端 CQE → failed 路径: free slots + ABORT + 断链 */
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(umqSock_->State(), SOCK_STAT_CLOSE);
    EXPECT_EQ(fake_.free_count, 2u); /* dest + offer */
}

/* ---------------- §5.3 RetryPendingReadsForSocket ---------------- */

TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_NullSock_Returns)
{
    UbsBigdata::RetryPendingReadsForSocket(SocketPtr());
    EXPECT_EQ(fake_.posts.size(), 0u);
}

TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_NoState_Returns)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 0u);
}

/* 已关闭的 socket: 不重投, 队列保持 */
TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_ClosedSocket_Returns)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(181, 0);
    ASSERT_EQ(fake_.posts.size(), 1u);

    umqSock_->State(SOCK_STAT_CLOSE);
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 1u); /* 已关闭: 不重投 */
}

/* retiring 的 socket: 不重投, 队列保持 */
TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_RetiringSocket_Returns)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(182, 0);
    ASSERT_EQ(fake_.posts.size(), 1u);

    umqSock_->retiring_.store(true);
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 1u); /* retiring: 不重投 */
}

/* 空 pending 队列: 惰性创建的 state 下空转, 无 post */
TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_EmptyPending_Returns)
{
    PrepareSendSocket();
    MountSendMocks();
    TestBuf done;
    done.SetCtrlHdr(UBS_READ_DONE, 0, 0, 0, 1);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &done.qbuf));
    EXPECT_NE(umqSock_->GetBigdataState(), nullptr);
    EXPECT_EQ(fake_.posts.size(), 0u);

    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 0u); /* 空队: 不 post */
}

/* 重投成功: READ 再 post(记账 fetch_sub), FinishPosting(false) 等终端 CQE */
TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_PendingNonEmpty_Success_RepostsAndFinalizes)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK); /* tx_ops 记账 */
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(183, 0);
    ASSERT_EQ(fake_.posts.size(), 1u);

    fake_.post_ret = 0;
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));

    ASSERT_EQ(fake_.posts.size(), 2u);
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[1]->qbuf_ext)->opcode, UMQ_OPC_READ);
    EXPECT_EQ(fake_.free_count, 0u);                                                         /* 等终端 CQE */
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_))); /* 已出队 */

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 3u);
    EXPECT_NE(FindBufWithType(fake_.posts[2], UBS_READ_DONE), nullptr);
    EXPECT_FALSE(umqSock_->rxQueue->Empty());
    EXPECT_EQ(fake_.free_count, 1u); /* offer */
}

/* 持续 EAGAIN(bad==head): 回到队首, 下一轮成功后再出队 */
TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_PendingNonEmpty_PersistentEagain_StaysQueued)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(184, 0);
    ASSERT_EQ(fake_.posts.size(), 1u);

    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    ASSERT_EQ(fake_.posts.size(), 2u); /* 重投尝试失败 */
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_))); /* 仍在队首 */

    fake_.post_ret = 0;
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    ASSERT_EQ(fake_.posts.size(), 3u);
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
}

/* 非可重试错误: 仅当前 ctx finalize-as-failed; 其余 pending 保留可重投 */
TEST_F(UbsBigdataTest, RetryPendingReadsForSocket_PendingNonEmpty_OtherError_OnlyCurrentFinalizes)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(185, 0);
    OfferIntoPending(186, 0);
    ASSERT_EQ(fake_.posts.size(), 2u);

    fake_.post_ret = -EPIPE;
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    ASSERT_EQ(fake_.posts.size(), 3u); /* 2 初投 + 1 重投尝试 */
    EXPECT_EQ(fake_.free_count, 0u);   /* 第一个仍在等终端 CQE */
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_))); /* 第二个仍在队列 */

    fake_.post_ret = 0;
    UbsBigdata::RetryPendingReadsForSocket(RefConvert<UmqSocket, Socket>(umqSock_));
    ASSERT_EQ(fake_.posts.size(), 4u);
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 2u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    EXPECT_EQ(umqSock_->State(), SOCK_STAT_CLOSE);
    ASSERT_EQ(fake_.posts.size(), 5u);
    EXPECT_NE(FindBufWithType(fake_.posts[4], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 2u);
    EXPECT_TRUE(SimulateReadCqe(dests[1], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 6u);
    EXPECT_NE(FindBufWithType(fake_.posts[5], UBS_READ_DONE), nullptr);
    EXPECT_EQ(fake_.free_count, 3u); /* dest(185) + offer(185) + offer(186) */
}

/* ---------------- §5.4 HandleFlowControlUpdate ---------------- */

TEST_F(UbsBigdataTest, HandleFlowControlUpdate_NoState_NoOp)
{
    PrepareSendSocket();
    MountSendMocks();
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 0u);
}

/* deferred ABORT 重投成功: 记账 + MarkActive; 队列排空后再次 update 无操作 */
TEST_F(UbsBigdataTest, HandleFlowControlUpdate_DeferredRepostSuccess_Drained)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    ASSERT_EQ(SocketBase::GenerateSocketCommOps(RefConvert<UmqSocket, Socket>(umqSock_)), UBS_OK); /* tx_ops 记账 */
    OfferImportFailIntoDeferred(191);
    ASSERT_EQ(fake_.posts.size(), 1u); /* ABORT 尝试(EAGAIN) */
    EXPECT_EQ(fake_.free_count, 1u);   /* offer RX buf 释放 */

    fake_.post_ret = 0;
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));

    ASSERT_EQ(fake_.posts.size(), 2u); /* deferred ABORT 重投成功 */
    EXPECT_NE(FindBufWithType(fake_.posts[1], UBS_READ_ABORT), nullptr);
    EXPECT_EQ(fake_.free_count, 1u); /* 重投的 ctrl 归 UMQ 所有, 不释放 */

    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 2u); /* 队空: 无新 post */
}

/* 持续 EAGAIN: ctrl 原样放回队首, 停止本轮 drain; 恢复后重投成功 */
TEST_F(UbsBigdataTest, HandleFlowControlUpdate_DeferredStillPaused_StaysAtHead)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    OfferImportFailIntoDeferred(192);
    ASSERT_EQ(fake_.posts.size(), 1u);

    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 2u); /* 每轮一次重投尝试 */
    EXPECT_EQ(fake_.free_count, 1u);   /* 仅 offer, ctrl 未丢 */
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 3u);

    fake_.post_ret = 0;
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 4u); /* 恢复后重投成功 */
}

/* 非可重试错误: ctrl 直接丢弃(FreeQbufChain), 继续 drain 后续 */
TEST_F(UbsBigdataTest, HandleFlowControlUpdate_DeferredOtherError_Dropped)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    OfferImportFailIntoDeferred(193);
    ASSERT_EQ(fake_.posts.size(), 1u);

    fake_.post_ret = -EPIPE;
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));

    EXPECT_EQ(fake_.posts.size(), 2u);                                            /* 重投尝试失败 */
    EXPECT_EQ(fake_.free_count, 2u);                                              /* offer + 丢弃的 ctrl */
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_)); /* 队空 → 无操作 */
    EXPECT_EQ(fake_.posts.size(), 2u);
}

/* pending_reads 在 FC update 时被重投 */
TEST_F(UbsBigdataTest, HandleFlowControlUpdate_PendingReads_Retried)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(194, 0);
    ASSERT_EQ(fake_.posts.size(), 1u);

    fake_.post_ret = 0;
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));

    ASSERT_EQ(fake_.posts.size(), 2u); /* READ 重投 */
    EXPECT_EQ(reinterpret_cast<umq_buf_pro_t *>(fake_.posts[1]->qbuf_ext)->opcode, UMQ_OPC_READ);
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd));
    ASSERT_EQ(fake_.posts.size(), 3u);
    EXPECT_NE(FindBufWithType(fake_.posts[2], UBS_READ_DONE), nullptr);
}

/* retiring: deferred 照常 drain, 但跳过 RetryPendingReads */
TEST_F(UbsBigdataTest, HandleFlowControlUpdate_Retiring_DrainsButSkipsRetry)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(195, 0);
    ASSERT_EQ(fake_.posts.size(), 1u);

    umqSock_->retiring_.store(true);
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), 1u);                                                      /* retiring: 跳过重投 */
    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_))); /* 仍在队 */
}

/* 边界 B1: deferred_ctrl 满 TEST_DEFERRED_CTRL_MAX(下一笔触发)丢最旧 */
TEST_F(UbsBigdataTest, HandleFlowControlUpdate_DeferredQueueFull_DropsOldest)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    const uint64_t seq0 = 200;
    const uint64_t nfill = TEST_DEFERRED_CTRL_MAX + 1; /* 多 1 个触发丢最旧 */
    for (uint64_t i = 0; i < nfill; ++i) {
        OfferImportFailIntoDeferred(seq0 + i);
    }
    ASSERT_EQ(fake_.posts.size(), nfill);
    EXPECT_EQ(fake_.free_count, nfill + 1u); /* nfill offer + 1 个被丢的最旧 ctrl */

    fake_.post_ret = 0;
    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), nfill + TEST_DEFERRED_CTRL_MAX); /* nfill 尝试 + 2048 重投 */
    EXPECT_EQ(fake_.free_count, nfill + 1u);

    UbsBigdata::HandleFlowControlUpdate(RefConvert<UmqSocket, Socket>(umqSock_));
    EXPECT_EQ(fake_.posts.size(), nfill + TEST_DEFERRED_CTRL_MAX); /* 队空 */
}

/* ---------------- §5.5 NeedsPollerAttention ---------------- */

TEST_F(UbsBigdataTest, NeedsPollerAttention_NullSock_ReturnsFalse)
{
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(SocketPtr()));
}

TEST_F(UbsBigdataTest, NeedsPollerAttention_NoState_ReturnsFalse)
{
    PrepareSendSocket();
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
}

/* pinned deadline 在册 → true; READ_DONE 正常消费释放后 → false */
TEST_F(UbsBigdataTest, NeedsPollerAttention_PinnedDeadline_True_ThenFalseAfterDone)
{
    PrepareSendSocket();
    MountSendMocks();
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    PostPinOffer(block, 5000, 100);

    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    auto *ctrl = reinterpret_cast<UbsCtrlHdr *>(fake_.posts[0]->buf_data);
    TestBuf done;
    done.SetCtrlHdr(UBS_READ_DONE, 0, 0, 0, ctrl->seq);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &done.qbuf));
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
}

/* ctx deadline 在册 → true; CleanupSocketState 后 state 从 socket 摘除 →
 * false(剩余 ctx 由 CQE 线程经自己的 state 引用静默收尾) */
TEST_F(UbsBigdataTest, NeedsPollerAttention_CtxDeadline_True_ThenFalseAfterCleanup)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    OfferIntoActive(210, 1); /* ctx 带 deadline, post 成功 → active_io */

    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    UbsBigdata::CleanupSocketState(umqSock_.Get());
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 1u);
    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[0], 0, &span, &deferred_fd)); /* destroying 路径静默释放 */
    EXPECT_EQ(fake_.free_count, 2u);                                /* dest + offer */
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
}

/* 无 deadline、仅 pending_reads 非空 → true; 清理排空后 → false */
TEST_F(UbsBigdataTest, NeedsPollerAttention_PendingReadsOnly_True_UntilDrained)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    fake_.post_ret = -UMQ_ERR_EAGAIN;
    fake_.post_bad_index = 0;
    OfferIntoPending(211, 0); /* 无 deadline: 计数器为 0, 仅 pending_reads 非空 */

    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));

    UbsBigdata::CleanupSocketState(umqSock_.Get());
    EXPECT_FALSE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
    EXPECT_EQ(fake_.free_count, 2u); /* pending ctx 的 dest + offer */
}

/* gen check 开、无 rpc_timeout: 30s pin-deadline fallback 仍记账 → NeedsPollerAttention true */
TEST_F(UbsBigdataTest, NeedsPollerAttention_NoTimeoutFallback_HasDeadline)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true; /* 无超时 → 30s pin-deadline fallback */
    uint8_t raw[5000];
    Block block(reinterpret_cast<char *>(raw), sizeof(raw));
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_NE(umqSock_->GetBigdataState(), nullptr);

    EXPECT_TRUE(UbsBigdata::NeedsPollerAttention(RefConvert<UmqSocket, Socket>(umqSock_)));
}

/* ---------------- §5.6 CleanupSocketState ---------------- */

TEST_F(UbsBigdataTest, CleanupSocketState_NullSock_NoOp)
{
    UbsBigdata::CleanupSocketState(nullptr);
    EXPECT_EQ(fake_.free_count, 0u);
    EXPECT_EQ(fake_.dealloc_count, 0u);
}

TEST_F(UbsBigdataTest, CleanupSocketState_NoState_NoOp)
{
    PrepareSendSocket();
    MountSendMocks();
    EXPECT_EQ(umqSock_->GetBigdataState(), nullptr);
    UbsBigdata::CleanupSocketState(umqSock_.Get());
    EXPECT_EQ(fake_.free_count, 0u);
}

/* 全容器混合: pinned DecRef、deferred 释放、pending 释放并删 ctx、active
 * 仅标 failed; 在途 CQE → destroying 路径静默释放(dest + offer), 无 post */
TEST_F(UbsBigdataTest, CleanupSocketState_FullState_ReleasesPinnedDeferredPending_KeepsActive)
{
    PrepareReceiveSocket();
    MountReceiveMocks();
    /* 1) pinned: 发送侧 offer(gen on, 本地超时 1ms) */
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    PostPinOffer(block, 5000, 1);
    EXPECT_EQ(block.nshared.load(), 2);
    EXPECT_EQ(fake_.posts.size(), 1u);
    /* 2) deferred_ctrl: import 失败 + ABORT EAGAIN(helper 自设旋钮) */
    OfferImportFailIntoDeferred(220);
    /* 3) pending_reads: READ post EAGAIN(bad==head, 未提交) */
    fake_.mempool_state_ret = UMQ_REMOTE_MEMPOOL_STATE_REUSE;
    fake_.info_set_ret = 0;
    TestBuf tbuf;
    BuildReadOffer(tbuf, 1, 221, 0, 72, 4065);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    /* 4) active_io: READ post 成功 */
    fake_.post_ret = 0;
    BuildReadOffer(tbuf, 1, 222, 0, 72, 4065);
    EXPECT_TRUE(UbsBigdata::HandleRxControl(RefConvert<UmqSocket, Socket>(umqSock_), &tbuf.qbuf));
    std::vector<umq_buf_t *> dests = FindReadDestBufs(fake_);
    ASSERT_EQ(dests.size(), 2u);       /* pending + active */
    ASSERT_EQ(fake_.posts.size(), 4u); /* offer + ABORT 尝试 + READ EAGAIN 尝试 + READ 成功 */
    EXPECT_EQ(fake_.free_count, 1u);   /* import 失败的 offer */

    UbsBigdata::CleanupSocketState(umqSock_.Get());

    EXPECT_EQ(block.nshared.load(), 1); /* pinned DecRef */
    EXPECT_EQ(fake_.free_count, 4u);    /* 1 import-fail offer + 1 deferred ctrl + pending dest + pending offer */
    EXPECT_EQ(umqSock_->GetBigdataState(), nullptr);

    uint32_t span = 0;
    int deferred_fd = -1;
    EXPECT_TRUE(SimulateReadCqe(dests[1], 0, &span, &deferred_fd)); /* active ctx destroying 静默收尾 */
    EXPECT_EQ(span, 1u);
    ASSERT_EQ(fake_.posts.size(), 4u); /* 无新 post */
    EXPECT_EQ(fake_.free_count, 6u);   /* + active dest + active offer */
    EXPECT_NE(umqSock_->State(), SOCK_STAT_CLOSE);
}

/* 幂等: 二次调用无 state → no-op, 不重复 DecRef */
TEST_F(UbsBigdataTest, CleanupSocketState_AfterCleanup_Idempotent)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 0;
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    umqSock_->SetLocalRpcTimeoutMs(1);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(block.nshared.load(), 2);

    UbsBigdata::CleanupSocketState(umqSock_.Get());
    EXPECT_EQ(block.nshared.load(), 1);
    EXPECT_EQ(umqSock_->GetBigdataState(), nullptr);

    UbsBigdata::CleanupSocketState(umqSock_.Get()); /* 二次调用: no-op */
    EXPECT_EQ(block.nshared.load(), 1);
}

/* ---------------- §5.7 GetGenCheckStats ---------------- */

/* 六字段计数增减一致性: offer_total/gen_fallback 随 offer 形态, pin_timeout
 * 随 stage-1(含 gen=0 条目也带 deadline), 其余保持 0, pin_alive_max_ms 非递减 */
TEST_F(UbsBigdataTest, GetGenCheckStats_Snapshot_AllSixFieldsConsistent)
{
    PrepareSendSocket();
    MountSendMocks();
    GlobalSetting::UBS_READ_GEN_CHECK_ENABLED = true;
    GlobalSetting::UBS_BIG_PIN_TIMEOUT_MARGIN_MS = 0;
    umqSock_->SetLocalRpcTimeoutMs(1);
    uint8_t rawHead[8 + 5000];
    Block block(reinterpret_cast<char *>(rawHead + 8), 5000);
    block.flags |= IOBUF_BLOCK_FLAGS_GEN_HEADROOM;
    fake_.RegisterBacking(block.data, block.cap, 0x77);
    UbsBigdata::GenCheckStats before = GenStats();
    PostPinOffer(block, 5000, 1); /* (a) 带超时 offer: offer_total+1, 条目带 deadline */
    /* (b) sliced fallback offer: offer_total+1, gen_fallback+1; 仍带 deadline 可 sweep */
    uint8_t sliced[5000];
    fake_.RegisterBacking(reinterpret_cast<char *>(sliced), sizeof(sliced), 0x77);
    ubs_segment_t segs[1] = {};
    segs[0].block = &block;
    segs[0].start_pos = sliced;
    segs[0].len = 5000;
    ubs_data_list_t list = MakeList(segs, 1);
    ssize_t out = -123;
    errno = 0;
    EXPECT_TRUE(UbsBigdata::TrySenderPost(TEST_FD, &list, &out));
    EXPECT_EQ(out, 1);
    SleepPastDeadline();
    UbsBigdata::SweepExpiredForSocket(RefConvert<UmqSocket, Socket>(umqSock_)); /* (c) stage-1: 两笔均超时 */
    UbsBigdata::GenCheckStats after = GenStats();

    EXPECT_EQ(after.offer_total - before.offer_total, 2u);
    EXPECT_EQ(after.gen_fallback - before.gen_fallback, 1u);
    EXPECT_EQ(after.pin_timeout - before.pin_timeout, 2u);
    EXPECT_EQ(after.gen_mismatch - before.gen_mismatch, 0u);
    EXPECT_EQ(after.rx_ctx_timeout - before.rx_ctx_timeout, 0u);
    EXPECT_GE(after.pin_alive_max_ms, before.pin_alive_max_ms);
}
} // namespace
