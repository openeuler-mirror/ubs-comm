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

/*
 * umq_data_tx_ops_test: src/ubsocket/csrc/core/umq/umq_data_tx_ops.cpp 的 UT。
 * 目标: 行覆盖 >= 80% / 分支覆盖 >= 50% (ADR 0001)。
 *
 * 边界清单(研究文档 §4.5 第 10 条)覆盖映射:
 *   1. WR 切分 TX_SGE_MAX=1 / io_buf_size=4064 → PostSend_WrSplitAtSgeMax_* / IoBufSizeBoundary_* / IoBufSizeAdjacent_*
 *   2. solicited/unsignaled 阈值 TX_REPORT_THRESHOLD=1 / 1MB → PostSend_Solicited* / CompleteEnable_*
 *   3. umq_post 五路错误分流 → PostSend_Eagain/ETimedout/EFLOWCTL/EMlink/ENobufs/Other/NoBadQbuf
 *   4. 全败恢复 → PostSend_EagainAllFailed_RestoresAndReturnsMinusOne
 *   5. PollTx CAS 循环 → PollTx_CasLoopRetry_* / CasMatch_*
 *   6. PollUmqTx 循环 TX_RETRIEVE_THRESHOLD=32 → PollUmqTx_Threshold_*
 *   7. DpRearmTxInterrupt 成功路径(ret==0 → errno=EAGAIN, 不走 Convert) → DpRearmTxInterrupt_RetZero_*
 *   8. Writable 反压 → Writable_*
 *   9. DoUmqTxPoll CLOS 五 status → port 冷却 → DoUmqTxPoll_Clos*
 *  10. FlushTx 超时熔断 + unsignaled 缓存释放 → FlushTx_*
 *
 * mock 约定: 全局 C API 用 MOCKER_CPP(::umq_*); 类成员/静态用 MOCKER_CPP(&Class::method);
 * LibcApi::shutdown 用 shutdown_ptr 替换。mockcpp-only, 不引入其它 mock 框架。
 */

#include "core/ubsocket_data_tx.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "common/ubsocket_port_cooldown.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_socket.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_socket.h"
#include "core/umq/umq_tx_helper.h"
#include "iobuf/ubsocket_iobuf.h"
#include "profiling/probe/probe_manager.h"
#include "profiling/statistics/tx_stat_defs.h"
#include "profiling/trace/ubsocket_trace.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {

static const int TEST_FD = 42;
static const uint64_t TEST_HANDLE = 12345;
static const uint32_t TEST_DATA_SIZE = 40;     /* 单 qbuf data_size */
static const uint32_t TEST_IO_BUF_SIZE = 4064; /* UmqSetting::GetIOBufSize() 默认值 */
static const uint32_t TEST_TX_DEPTH = 255;     /* GlobalSetting::UBS_TX_DEPTH 默认值 */

/* ---- ::umq_post mock 状态 ---- */
static int g_postRet = 0;
static umq_buf_t *g_postBadQbuf = nullptr;
static int g_postCallCount = 0;

/* ---- ::umq_buf_alloc mock 状态 ---- */
static umq_buf_t *g_allocBuf = nullptr;

/* ---- ::umq_buf_free mock 状态 ---- */
static int g_bufFreeCount = 0;
static umq_buf_t *g_freedBuf = nullptr;

/* ---- ::umq_get_cq_event / ::umq_ack_interrupt mock 状态 ---- */
static int g_getCqEventRet = 0;
static int g_ackInterruptCount = 0;
static uint32_t g_ackNevents = 0;

/* ---- ::umq_rearm_interrupt mock 状态 ---- */
static int g_rearmRet = 0;
static int g_rearmCallCount = 0;

/* ---- ::umq_data_to_head mock 状态: 按 data 指针分发 ---- */
static std::unordered_map<void *, umq_buf_t *> g_dataToHeadByData;

/* ---- LibcApi::shutdown_ptr 替换状态 ---- */
static int g_shutdownCallCount = 0;
static int g_shutdownFd = -1;

/* ---- UmqTxHelper::PollUmqTxInternal mock 状态(DoUmqTxPoll 底层) ---- */
static int g_pollInternalRet = 0;
static ops_error_code g_pollInternalErrCode = ops_error_code::OK;
static bool g_pollInternalInvokeCb = false;
static umq_buf_t *g_pollInternalQbuf = nullptr;

/* ---- DataTxOps 私有成员函数 mock 状态 ---- */
static int g_getAckRet = 0;
static int g_getAckCallCount = 0;
static int g_pollTxCount = 0;
static bool g_pollTxToEmpty = false;
static int g_pollTxOnceCount = 0;
static int g_doPollRet = 0;
static int g_doPollRetAfterFirst = 0;
static ops_error_code g_doPollErrCode = ops_error_code::OK;
static int g_doPollCount = 0;
static int g_dpRearmCount = 0;

static int MockUmqPost(uint64_t umqh, umq_buf_t *qbuf, umq_io_option_t *option, umq_buf_t **bad_qbuf)
{
    (void)umqh;
    (void)qbuf;
    (void)option;
    g_postCallCount++;
    if (bad_qbuf != nullptr) {
        *bad_qbuf = g_postBadQbuf;
    }
    return g_postRet;
}

static umq_buf_t *MockUmqBufAlloc(uint32_t request_size, uint32_t request_qbuf_num, uint64_t umqh,
                                  umq_alloc_option_t *option)
{
    (void)request_size;
    (void)request_qbuf_num;
    (void)umqh;
    (void)option;
    return g_allocBuf;
}

static void MockUmqBufFree(umq_buf_t *qbuf)
{
    g_bufFreeCount++;
    g_freedBuf = qbuf;
}

static int MockUmqGetCqEvent(uint64_t umqh, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    return g_getCqEventRet;
}

static void MockUmqAckInterrupt(uint64_t umqh, uint32_t nevents, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    g_ackInterruptCount++;
    g_ackNevents = nevents;
}

static int MockUmqRearmInterrupt(uint64_t umqh, bool solicited, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)solicited;
    (void)option;
    g_rearmCallCount++;
    return g_rearmRet;
}

static umq_buf_t *MockUmqDataToHead(void *data)
{
    auto it = g_dataToHeadByData.find(data);
    return it != g_dataToHeadByData.end() ? it->second : nullptr;
}

static int MockShutdown(int fd, int how)
{
    (void)how;
    g_shutdownCallCount++;
    g_shutdownFd = fd;
    return 0;
}

static int MockPollUmqTxInternal(UmqTxHelper::PollArgs &poll_args, UmqTxHelper::ICallback &error_cb)
{
    (void)poll_args;
    if (g_pollInternalInvokeCb) {
        error_cb.invoke(g_pollInternalQbuf);
    }
    return g_pollInternalRet;
}

static int MockGetAndAckEvent()
{
    g_getAckCallCount++;
    return g_getAckRet;
}

static int MockPollUmqTx(Socket *sock, bool poll_to_empty)
{
    (void)sock;
    g_pollTxCount++;
    g_pollTxToEmpty = poll_to_empty;
    return 0;
}

static int MockPollUmqTxOnce(Socket *sock)
{
    (void)sock;
    g_pollTxOnceCount++;
    return 0;
}

static int MockDoUmqTxPoll(Socket *sock, ops_error_code &err_code)
{
    (void)sock;
    err_code = g_doPollErrCode;
    g_doPollCount++;
    const int ret = g_doPollRet;
    g_doPollRet = g_doPollRetAfterFirst;
    return ret;
}

static int MockDpRearmTxInterrupt()
{
    g_dpRearmCount++;
    return -1;
}

/* Block 通过 placement new 构造于 buf_data - sizeof(Block) 处, 故数据区需预留 Block 头空间 */
static char *AllocBufData(uint32_t data_size)
{
    auto *raw = new char[sizeof(Block) + data_size];
    return raw + sizeof(Block);
}

class UmqDataTxOpsTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false; /* 陷阱: 默认 true, UT 必须显式关闭 */
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        GlobalSetting::UBS_MONITOR_ENABLE = true; /* 计数器可观测: post_err */
        GlobalSetting::UBS_TX_DEPTH = TEST_TX_DEPTH;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 0;
        UmqSetting::UMQ_TP_TYPE = POOL;
        LibcApi::shutdown_ptr = MockShutdown;
        ResetMockState();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        Statistics::ProbeManager::GetInstance().Stop();
        LibcApi::shutdown_ptr = nullptr;
        UmqSetting::UMQ_TP_TYPE = POOL;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = true; /* 恢复默认 */
        GlobalSetting::UBS_MONITOR_ENABLE = true;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;
        GlobalSetting::UBS_TX_DEPTH = TEST_TX_DEPTH;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 60000; /* 恢复默认 */
        errno = 0;
    }

    void ResetMockState()
    {
        g_postRet = 0;
        g_postBadQbuf = nullptr;
        g_postCallCount = 0;
        g_allocBuf = nullptr;
        g_bufFreeCount = 0;
        g_freedBuf = nullptr;
        g_getCqEventRet = 0;
        g_ackInterruptCount = 0;
        g_ackNevents = 0;
        g_rearmRet = 0;
        g_rearmCallCount = 0;
        g_dataToHeadByData.clear();
        g_shutdownCallCount = 0;
        g_shutdownFd = -1;
        g_pollInternalRet = 0;
        g_pollInternalErrCode = ops_error_code::OK;
        g_pollInternalInvokeCb = false;
        g_pollInternalQbuf = nullptr;
        g_getAckRet = 0;
        g_getAckCallCount = 0;
        g_pollTxCount = 0;
        g_pollTxToEmpty = false;
        g_pollTxOnceCount = 0;
        g_doPollRet = 0;
        g_doPollRetAfterFirst = 0;
        g_doPollErrCode = ops_error_code::OK;
        g_doPollCount = 0;
        g_dpRearmCount = 0;
        std::memset(&m_qbuf1, 0, sizeof(m_qbuf1));
        std::memset(&m_qbuf2, 0, sizeof(m_qbuf2));
        std::memset(&m_qbuf3, 0, sizeof(m_qbuf3));
        std::memset(&m_pro1, 0, sizeof(m_pro1));
        std::memset(&m_pro2, 0, sizeof(m_pro2));
        std::memset(&m_pro3, 0, sizeof(m_pro3));
        std::memset(&m_headQbuf1, 0, sizeof(m_headQbuf1));
        std::memset(&m_headQbuf2, 0, sizeof(m_headQbuf2));
        std::memset(&m_headQbuf3, 0, sizeof(m_headQbuf3));
        std::memset(&m_badQbuf, 0, sizeof(m_badQbuf));
        m_data1 = nullptr;
        m_data2 = nullptr;
        m_data3 = nullptr;
        m_block1 = nullptr;
        m_block2 = nullptr;
        m_block3 = nullptr;
    }

    DataTxOps MakeOps()
    {
        return DataTxOps(TEST_FD, TEST_HANDLE);
    }

    SocketPtr MakeSock()
    {
        UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
        return RefConvert<UmqSocket, Socket>(umqSock);
    }

    /* LoadSeqNum 定义于 UmqSocketSeq, 基类 Socket 不可见; 测试中的 socket 恒为 UmqSocket */
    uint64_t LoadSeqNumOf(const SocketPtr &sock)
    {
        return static_cast<UmqSocket *>(sock.Get())->LoadSeqNum();
    }

    /* events_/GetVersionedWritableReady 定义于 SocketBase(继承链 Socket→SocketBase→UmqSocket) */
    SocketBase *AsSocketBase(const SocketPtr &sock)
    {
        return static_cast<SocketBase *>(sock.Get());
    }

    /* 构造一个带真实 Block 头的数据区, 并让 data→head qbuf 映射就绪:
     * headQbuf.buf_data 指向 Block 本体(DataToBlock 的返回语义),
     * 数据区指针 data 是 BufferConverter 的拷贝目标。 */
    void InitDataBlock(umq_buf_t *headQbuf, Block **outBlock, char **outData, uint32_t data_size,
                       uint32_t total_size = TEST_DATA_SIZE)
    {
        (void)total_size;
        char *data = AllocBufData(data_size);
        alignas(Block) char *storage = new char[sizeof(Block)];
        Block *block = new (storage) Block(data, data_size, 2); /* nshared=2: IncRef/DecRef 不触发析构 */
        headQbuf->buf_data = reinterpret_cast<char *>(block);
        *outBlock = block;
        *outData = data;
    }

    /* 建立单个 qbuf: buf_data=data, data_size=TEST_DATA_SIZE, 注册 data→head 映射 */
    void InitQbuf(umq_buf_t *qbuf, umq_buf_pro_t *pro, char *data, umq_buf_t *headQbuf)
    {
        std::memset(qbuf, 0, sizeof(*qbuf));
        std::memset(pro, 0, sizeof(*pro));
        std::memcpy(qbuf->qbuf_ext, pro, sizeof(*pro));
        qbuf->qbuf_next = nullptr;
        qbuf->buf_data = data;
        qbuf->data_size = TEST_DATA_SIZE;
        g_dataToHeadByData[data] = headQbuf;
    }

    /* 建立 n 个 qbuf 的发送链(每个 data_size=TEST_DATA_SIZE, 带 Block), 返回首节点 */
    umq_buf_t *InitPostChain(int n)
    {
        umq_buf_t *qb[] = {&m_qbuf1, &m_qbuf2, &m_qbuf3};
        umq_buf_pro_t *pr[] = {&m_pro1, &m_pro2, &m_pro3};
        umq_buf_t *hd[] = {&m_headQbuf1, &m_headQbuf2, &m_headQbuf3};
        char **dt[] = {&m_data1, &m_data2, &m_data3};
        Block **bl[] = {&m_block1, &m_block2, &m_block3};
        for (int i = 0; i < n; ++i) {
            InitDataBlock(hd[i], bl[i], dt[i], TEST_DATA_SIZE);
            InitQbuf(qb[i], pr[i], *dt[i], hd[i]);
            if (i > 0) {
                qb[i - 1]->qbuf_next = qb[i];
            }
        }
        return qb[0];
    }

    /* PostSend 公共骨架: mock 三件套 + socket + 发送链 + 标准单数据区转换器。
     * 自定义转换器(iov/5000 切分等)的用例先调本 helper 再覆盖 cvt。 */
    umq_buf_t *PreparePostSend(DataTxOps &ops, SocketPtr &sock, ConverterPtr &cvt, int chainLen)
    {
        sock = MakeSock();
        MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
        MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
        MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
        umq_buf_t *head = InitPostChain(chainLen);
        cvt = ops.BuildBufferConverter(m_data1, TEST_DATA_SIZE);
        return head;
    }

    /* CLOS 拓扑 socket + used_ports 预置(DoUmqTxPoll 冷却用例共用)。
     * startPort 可调: PortCooldownManager 单例跨测试保留冷却条目, 各用例须错开 port 避免互撞 */
    UmqSocketPtr MakeClosSockWithPorts(uint32_t portCount, SocketPtr &sock, uint32_t startPort = 3)
    {
        UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
        sock = RefConvert<UmqSocket, Socket>(umqSock);
        umqSock->SetTopoType(UMQ_TOPO_TYPE_CLOS);
        UmqSocketCold *cold = umqSock->GetOrCreateCold();
        cold->used_ports = std::make_unique<umq_port_id_t[]>(portCount);
        cold->used_ports_num = portCount;
        for (uint32_t i = 0; i < portCount; ++i) {
            cold->used_ports[i].bs.chip_id = 1;
            cold->used_ports[i].bs.die_id = 2;
            cold->used_ports[i].bs.port_idx = startPort + i;
        }
        return umqSock;
    }

    umq_buf_t m_qbuf1{};
    umq_buf_t m_qbuf2{};
    umq_buf_t m_qbuf3{};
    umq_buf_pro_t m_pro1{};
    umq_buf_pro_t m_pro2{};
    umq_buf_pro_t m_pro3{};
    umq_buf_t m_headQbuf1{}; /* umq_data_to_head 返回的 head qbuf */
    umq_buf_t m_headQbuf2{};
    umq_buf_t m_headQbuf3{};
    umq_buf_t m_badQbuf{}; /* DoUmqTxPoll 错误回调携带的 CQE qbuf */
    char *m_data1 = nullptr;
    char *m_data2 = nullptr;
    char *m_data3 = nullptr;
    Block *m_block1 = nullptr;
    Block *m_block2 = nullptr;
    Block *m_block3 = nullptr;
};

/* ==================== 构造 / 访问器 / 工具 ==================== */

TEST_F(UmqDataTxOpsTest, OwnerAccessors_FallbackCtor_ReturnsCtorParams)
{
    DataTxOps ops = MakeOps();
    ASSERT_EQ(TEST_FD, ops.OwnerFd());
    ASSERT_EQ(TEST_HANDLE, ops.OwnerUmqh());
}

TEST_F(UmqDataTxOpsTest, OwnerAccessors_WithOwner_ReadsSocketMembers)
{
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    DataTxOps ops(TEST_FD + 1, TEST_HANDLE, umqSock.Get());
    ASSERT_EQ(TEST_FD, ops.OwnerFd());
    ASSERT_EQ(UMQ_INVALID_HANDLE, ops.OwnerUmqh());
}

TEST_F(UmqDataTxOpsTest, IOBufSize_Default_EqualsUmqSetting)
{
    DataTxOps ops = MakeOps();
    ASSERT_EQ(UmqSetting::GetIOBufSize(), ops.IOBufSize());
    ASSERT_EQ(TEST_IO_BUF_SIZE, ops.IOBufSize());
}

TEST_F(UmqDataTxOpsTest, BuildConverters_ValidInput_ReturnNonNull)
{
    DataTxOps ops = MakeOps();
    char buf[TEST_DATA_SIZE] = {0};
    struct iovec iov[1] = {{buf, TEST_DATA_SIZE}};
    ConverterPtr cvtIov = ops.BuildIovConverter(iov, 1);
    ConverterPtr cvtBuf = ops.BuildBufferConverter(buf, sizeof(buf));
    ASSERT_NE(nullptr, cvtIov.Get());
    ASSERT_NE(nullptr, cvtBuf.Get());
}

/* ==================== AllocTxBuf ==================== */

TEST_F(UmqDataTxOpsTest, AllocTxBuf_AllocFail_RearmsAndReturnsZero)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockUmqBufAlloc));
    MOCKER_CPP(&DataTxOps::DpRearmTxInterrupt).stubs().will(invoke(&MockDpRearmTxInterrupt));
    g_allocBuf = nullptr;

    uintptr_t ret = ops.AllocTxBuf(TEST_DATA_SIZE, 1);
    ASSERT_EQ(0u, ret);
    ASSERT_EQ(1, g_dpRearmCount);
}

TEST_F(UmqDataTxOpsTest, AllocTxBuf_AllocSuccess_ReturnsQbufPtr)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockUmqBufAlloc));
    MOCKER_CPP(&DataTxOps::DpRearmTxInterrupt).stubs().will(invoke(&MockDpRearmTxInterrupt));
    g_allocBuf = &m_qbuf1;

    uintptr_t ret = ops.AllocTxBuf(TEST_DATA_SIZE, 1);
    ASSERT_EQ(reinterpret_cast<uintptr_t>(&m_qbuf1), ret);
    ASSERT_EQ(0, g_dpRearmCount);
}

/* ==================== DataToBlock ==================== */

TEST_F(UmqDataTxOpsTest, DataToBlock_HeadMiss_ReturnsNull)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    ASSERT_EQ(nullptr, ops.DataToBlock(nullptr));
}

TEST_F(UmqDataTxOpsTest, DataToBlock_HeadBufDataNull_ReturnsNull)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    char data[TEST_DATA_SIZE] = {0};
    g_dataToHeadByData[data] = &m_headQbuf1; /* buf_data 保持 nullptr */
    ASSERT_EQ(nullptr, ops.DataToBlock(data));
}

TEST_F(UmqDataTxOpsTest, DataToBlock_ValidHead_ReturnsBlock)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    InitDataBlock(&m_headQbuf1, &m_block1, &m_data1, TEST_DATA_SIZE);
    g_dataToHeadByData[m_data1] = &m_headQbuf1;
    ASSERT_EQ(m_block1, ops.DataToBlock(m_data1));
}

/* ==================== PostSend: 成功路径 ==================== */

TEST_F(UmqDataTxOpsTest, PostSend_Success_SingleWrPostedAndCounted)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 1);

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 1, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE), ret);
    ASSERT_EQ(1, g_postCallCount);
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH - 1), ops.tx_queue_avail_num_.load());
    ASSERT_EQ(1u, LoadSeqNumOf(sock));
    auto *pro = reinterpret_cast<umq_buf_pro_t *>(m_qbuf1.qbuf_ext);
    ASSERT_EQ(UMQ_OPC_SEND_IMM, pro->opcode);
    ASSERT_EQ(1u, pro->flag.bs.complete_enable);  /* TX_REPORT_THRESHOLD=1: 每 WR 都 signaled */
    ASSERT_EQ(1u, pro->flag.bs.solicited_enable); /* i+1==batch */
    ASSERT_EQ(reinterpret_cast<uint64_t>(&m_qbuf1), pro->user_ctx);
    ASSERT_EQ(0u, pro->imm.user_data); /* 首个 seq_no */
    ASSERT_EQ(TEST_DATA_SIZE, m_qbuf1.total_data_size);
    ASSERT_EQ(UMQ_IO_TX, m_qbuf1.io_direction);
    /* head/tail 推进: head 越过 qbuf1, tail 落在 qbuf1 */
    ASSERT_EQ(nullptr, ops.head_buf_.first);
    ASSERT_EQ(&m_qbuf1, ops.tail_buf_.first);
}

TEST_F(UmqDataTxOpsTest, PostSend_WrSplitAtSgeMax_TwoWrPerIoBuf)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    umq_buf_t *head = InitPostChain(2);
    /* iov 5000 > io_buf_size(4064): WR1=4064(SGE 上限断), WR2=936(最后段) */
    char bigBuf[5000] = {0};
    m_qbuf1.buf_data = bigBuf;
    m_qbuf2.buf_data = bigBuf + TEST_IO_BUF_SIZE;
    g_dataToHeadByData[bigBuf] = &m_headQbuf1;
    g_dataToHeadByData[bigBuf + TEST_IO_BUF_SIZE] = &m_headQbuf2;
    struct iovec iov[1] = {{bigBuf, sizeof(bigBuf)}};
    ConverterPtr cvt = ops.BuildIovConverter(iov, 1);

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(static_cast<int>(sizeof(bigBuf)), ret);
    ASSERT_EQ(TEST_IO_BUF_SIZE, m_qbuf1.total_data_size);
    ASSERT_EQ(TEST_IO_BUF_SIZE, m_qbuf1.data_size);
    ASSERT_EQ(static_cast<uint32_t>(sizeof(bigBuf) - TEST_IO_BUF_SIZE), m_qbuf2.total_data_size);
    ASSERT_EQ(static_cast<uint32_t>(sizeof(bigBuf) - TEST_IO_BUF_SIZE), m_qbuf2.data_size);
    ASSERT_EQ(2u, LoadSeqNumOf(sock));
}

TEST_F(UmqDataTxOpsTest, PostSend_IoBufSizeBoundary_ExactFillSingleWr)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    umq_buf_t *head = InitPostChain(1);
    char buf[TEST_IO_BUF_SIZE] = {0};
    m_qbuf1.buf_data = buf;
    g_dataToHeadByData[buf] = &m_headQbuf1;
    struct iovec iov[1] = {{buf, sizeof(buf)}};
    ConverterPtr cvt = ops.BuildIovConverter(iov, 1);

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 1, cvt);

    ASSERT_EQ(static_cast<int>(TEST_IO_BUF_SIZE), ret); /* 边界值: iov==io_buf_size, 单个 WR 恰好装满 */
    ASSERT_EQ(TEST_IO_BUF_SIZE, m_qbuf1.total_data_size);
}

TEST_F(UmqDataTxOpsTest, PostSend_IoBufSizeAdjacent_SecondWrOneByte)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    umq_buf_t *head = InitPostChain(2);
    char buf[TEST_IO_BUF_SIZE + 1] = {0};
    m_qbuf1.buf_data = buf;
    m_qbuf2.buf_data = buf + TEST_IO_BUF_SIZE;
    g_dataToHeadByData[buf] = &m_headQbuf1;
    g_dataToHeadByData[buf + TEST_IO_BUF_SIZE] = &m_headQbuf2;
    struct iovec iov[1] = {{buf, sizeof(buf)}};
    ConverterPtr cvt = ops.BuildIovConverter(iov, 1);

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(static_cast<int>(TEST_IO_BUF_SIZE + 1), ret); /* 相邻值: iov==io_buf_size+1, 第二个 WR 1 字节 */
    ASSERT_EQ(TEST_IO_BUF_SIZE, m_qbuf1.total_data_size);
    ASSERT_EQ(1u, m_qbuf2.total_data_size);
}

TEST_F(UmqDataTxOpsTest, PostSend_EmptyWr_Skipped)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    umq_buf_t *head = InitPostChain(1);
    m_qbuf1.data_size = 0;
    char empty[1] = {0};
    ConverterPtr cvt = ops.BuildBufferConverter(empty, 0); /* 空 buffer: moved_total_len==0 */

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 1, cvt);

    ASSERT_EQ(0, ret); /* 空 WR 被跳过, tx_total_len 仍为 0 */
    ASSERT_EQ(1, g_postCallCount);
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH - 1), ops.tx_queue_avail_num_.load());
}

TEST_F(UmqDataTxOpsTest, PostSend_SolicitedLastWr_EnableOnFinalWr)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 3);

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 3, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE * 3), ret);
    /* 阈值 TX_REPORT_THRESHOLD=1: WR0 累积(1), WR1 累积(2), WR2 最后 WR solicited 并复位 */
    ASSERT_EQ(0u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf1.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(0u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf2.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(1u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf3.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(0u, ops.unsolicited_wr_num_);
    ASSERT_EQ(0u, ops.unsolicited_bytes_);
}

TEST_F(UmqDataTxOpsTest, PostSend_SolicitedWrNumThreshold_TriggerAt2)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 3);
    ops.unsolicited_wr_num_ = 1; /* 相邻值 1: 不触发 */

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 3, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE * 3), ret);
    /* WR0: 累积 1→2; WR1: 2>1 触发(i+1=2≠3, 归因唯一于 wr_num 阈值); WR2: 最后 WR */
    ASSERT_EQ(0u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf1.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(1u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf2.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(1u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf3.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(0u, ops.unsolicited_wr_num_);
}

TEST_F(UmqDataTxOpsTest, PostSend_SolicitedBytesThreshold_TriggerOverMax)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 3);
    ops.unsolicited_bytes_ = TX_UNSOLICITED_BYTES_MAX; /* 边界值: ==MAX 不触发, +40 后 >MAX 触发 */
    ops.unsolicited_wr_num_ = 0;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 3, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE * 3), ret);
    /* WR0: bytes==MAX 不触发累积; WR1: bytes>MAX 触发 solicited; WR2: 最后 WR solicited */
    ASSERT_EQ(0u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf1.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(1u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf2.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(1u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf3.qbuf_ext)->flag.bs.solicited_enable);
    ASSERT_EQ(0u, ops.unsolicited_wr_num_);
    ASSERT_EQ(0u, ops.unsolicited_bytes_);
}

TEST_F(UmqDataTxOpsTest, PostSend_QueueAvailOne_SolicitedEarly)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    ops.tx_queue_avail_num_.store(1); /* 队列只剩 1 个 slot: 非最后 WR 也 solicited */

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE * 2), ret);
    ASSERT_EQ(1u, reinterpret_cast<umq_buf_pro_t *>(m_qbuf1.qbuf_ext)->flag.bs.solicited_enable);
}

TEST_F(UmqDataTxOpsTest, PostSend_CompleteEnable_UserCtxTracksHead)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE * 2), ret);
    /* TX_REPORT_THRESHOLD=1: 每 WR complete_enable=1, user_ctx 指向 head, head 逐 WR 前移 */
    auto *pro1 = reinterpret_cast<umq_buf_pro_t *>(m_qbuf1.qbuf_ext);
    auto *pro2 = reinterpret_cast<umq_buf_pro_t *>(m_qbuf2.qbuf_ext);
    ASSERT_EQ(1u, pro1->flag.bs.complete_enable);
    ASSERT_EQ(reinterpret_cast<uint64_t>(&m_qbuf1), pro1->user_ctx);
    ASSERT_EQ(1u, pro2->flag.bs.complete_enable);
    ASSERT_EQ(reinterpret_cast<uint64_t>(&m_qbuf2), pro2->user_ctx);
    ASSERT_EQ(nullptr, ops.head_buf_.first);
    ASSERT_EQ(0u, ops.unsignaled_wr_num_);
}

/* ==================== PostSend: umq_post 五路错误分流 ==================== */

TEST_F(UmqDataTxOpsTest, PostSend_EagainAllFailed_RestoresAndReturnsMinusOne)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_EAGAIN;
    g_postBadQbuf = &m_qbuf1; /* 全部失败: bad_qbuf == tx_buf_list */
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EAGAIN, errno);
    ASSERT_TRUE(ops.need_fc_awake_.load());
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_EAGAIN_ALL]);
    /* 全败恢复: seq 回退, avail 不变, head/tail 回退, qbuf 释放 */
    ASSERT_EQ(0u, LoadSeqNumOf(sock));
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH), ops.tx_queue_avail_num_.load());
    ASSERT_EQ(nullptr, ops.head_buf_.first);
    ASSERT_EQ(nullptr, ops.tail_buf_.first);
    ASSERT_EQ(1, g_bufFreeCount);
    ASSERT_EQ(&m_qbuf1, g_freedBuf);
    ASSERT_EQ(0u, ops.unsolicited_wr_num_);
    ASSERT_EQ(0u, ops.unsignaled_wr_num_);
}

TEST_F(UmqDataTxOpsTest, PostSend_EagainPartial_CountsPostedWrs)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_EAGAIN;
    g_postBadQbuf = &m_qbuf2; /* 部分失败: WR0 成功, WR1 失败 */
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE), ret); /* 只统计已投递的 WR0 */
    ASSERT_EQ(EAGAIN, errno);
    ASSERT_FALSE(ops.need_fc_awake_.load());
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_EAGAIN_PART]);
    ASSERT_EQ(1u, LoadSeqNumOf(sock)); /* sn_allocated(2) - buf_num(1) */
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH - 1), ops.tx_queue_avail_num_.load());
    ASSERT_EQ(1, g_bufFreeCount);
    ASSERT_EQ(&m_qbuf2, g_freedBuf);
    ASSERT_EQ(nullptr, ops.head_buf_.first);
}

TEST_F(UmqDataTxOpsTest, PostSend_ETimedout_WithUnifiedCleanup_ReturnsMinusOne)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_ETIMEOUT;
    g_postBadQbuf = &m_qbuf1;
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    /* 超时分支保留统计后置 flagEIO，走尾部统一清理: 恢复 seq、释放 buf，再 return -1 */
    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EIO, errno);
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_ETIMEDOUT]);
    ASSERT_EQ(0u, LoadSeqNumOf(sock)); /* 统一清理回退 sn_allocated */
    /* tx_queue_avail_num_ 由 PollTx CQE 回收时恢复，post 失败路径不回补 */
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH), ops.tx_queue_avail_num_.load());
    ASSERT_EQ(1, g_bufFreeCount);
}

TEST_F(UmqDataTxOpsTest, PostSend_EFLOWCTL_ReturnsMinusOne)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_EFLOWCTL;
    g_postBadQbuf = &m_qbuf1;
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EIO, errno); /* ret==-EFLOWCTL 分支不走 errno 表, 直接 EIO */
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_EFLOWCTL]);
    ASSERT_EQ(1, g_bufFreeCount); /* 统一清理释放 bad_qbuf */
}

/* -EFLOWCTL_FATAL / -EFLOWCTL_EAGAIN 走 Convert 映射表独立表项(实测删 2 例分支 -1,
 * 专门测试 umq_errno_converter_test 未兜底, 恢复) */
TEST_F(UmqDataTxOpsTest, PostSend_EFLOWCTLFatal_ReturnsMinusOne)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_EFLOWCTL_FATAL;
    g_postBadQbuf = &m_qbuf1;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EIO, errno);
}

TEST_F(UmqDataTxOpsTest, PostSend_EFLOWCTLEagain_ReturnsMinusOne)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_EFLOWCTL_EAGAIN;
    g_postBadQbuf = &m_qbuf1;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EIO, errno);
}

TEST_F(UmqDataTxOpsTest, PostSend_EMlinkAllFailed_EnqueuesWaitQueue)
{
    DataTxOps ops = MakeOps();
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockUmqPost));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    umq_buf_t *head = InitPostChain(2);
    ConverterPtr cvt = ops.BuildBufferConverter(m_data1, TEST_DATA_SIZE);
    g_postRet = -UMQ_ERR_EMLINK;
    g_postBadQbuf = &m_qbuf1;
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EAGAIN, errno); /* EMLINK 分支最终把 errno 置为 EAGAIN */
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_EMLINK]);
    ASSERT_EQ(JettyAllocState::WAITING, umqSock->GetJettyAllocState()); /* 已入 TpWaitQueue */
    ASSERT_EQ(1, g_bufFreeCount);
    ASSERT_EQ(0u, LoadSeqNumOf(sock));
}

TEST_F(UmqDataTxOpsTest, PostSend_ENobufsAllFailed_SetsEagain)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_ENOBUFS;
    g_postBadQbuf = &m_qbuf1;
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EAGAIN, errno);
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_ENOBUFS_ALL]);
    ASSERT_EQ(1, g_bufFreeCount);
    ASSERT_EQ(&m_qbuf1, g_freedBuf);
}

TEST_F(UmqDataTxOpsTest, PostSend_ENobufsPartial_CountsPostedWrs)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_ENOBUFS;
    g_postBadQbuf = &m_qbuf2;
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE), ret);
    ASSERT_EQ(ENOBUFS, errno); /* 部分失败不改 errno */
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_ENOBUFS_PART]);
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH - 1), ops.tx_queue_avail_num_.load());
    ASSERT_EQ(1u, LoadSeqNumOf(sock));
    ASSERT_EQ(&m_qbuf2, g_freedBuf);
}

TEST_F(UmqDataTxOpsTest, PostSend_OtherError_SetsFlagEio)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_EPERM; /* 表内映射: EPERM→EIO, 落入 else 分支 */
    g_postBadQbuf = &m_qbuf1;
    errno = 0;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EIO, errno);
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_OTHER]);
    ASSERT_EQ(1, g_bufFreeCount); /* else 分支仍走恢复+释放 */
}

TEST_F(UmqDataTxOpsTest, PostSend_NoBadQbuf_ReturnsTotalBytes)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_postRet = -UMQ_ERR_EAGAIN;
    g_postBadQbuf = nullptr; /* 异常分支: bad_qbuf==nullptr */
    errno = EIO;

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(static_cast<int>(TEST_DATA_SIZE * 2), ret); /* 不做恢复, 返回累计长度 */
    ASSERT_EQ(EAGAIN, errno);
    ASSERT_EQ(1u, ops.GetTxStatCounters()->post_err[txstat::POST_ERR_NO_BADQBUF]);
    ASSERT_EQ(2u, LoadSeqNumOf(sock));                                               /* 不回退 */
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH), ops.tx_queue_avail_num_.load()); /* 不扣减 */
    ASSERT_EQ(0, g_bufFreeCount);
}

TEST_F(UmqDataTxOpsTest, PostSend_BlockLookupFail_ReturnsMinusOneEINVAL)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    ConverterPtr cvt;
    umq_buf_t *head = PreparePostSend(ops, sock, cvt, 2);
    g_dataToHeadByData.erase(m_data2); /* WR1 的 data 找不到 Block */

    int ret = ops.PostSend(sock, reinterpret_cast<uintptr_t>(head), 2, cvt);

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EINVAL, errno);
    ASSERT_EQ(0, g_postCallCount);     /* 未到达 umq_post */
    ASSERT_EQ(1u, LoadSeqNumOf(sock)); /* WR0 已 FetchAddSeqNum, 失败路径不回退 seq */
    ASSERT_EQ(0, g_bufFreeCount);
}

/* ==================== GetAndAckEvent ==================== */

TEST_F(UmqDataTxOpsTest, GetAndAckEvent_NoEvents_ReturnsZero)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).stubs().will(invoke(&MockUmqAckInterrupt));
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));
    g_getCqEventRet = 0;

    int ret = ops.GetAndAckEvent();

    ASSERT_EQ(0, ret);
    ASSERT_EQ(0, g_ackInterruptCount);
    ASSERT_EQ(0, g_rearmCallCount);
}

TEST_F(UmqDataTxOpsTest, GetAndAckEvent_ApiFail_ConvertsErrno)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    g_getCqEventRet = -UMQ_ERR_EAGAIN;
    errno = EIO;

    int ret = ops.GetAndAckEvent();

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EAGAIN, errno);
}

TEST_F(UmqDataTxOpsTest, GetAndAckEvent_PositiveEvents_AcksAndRearms)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).stubs().will(invoke(&MockUmqAckInterrupt));
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));
    g_getCqEventRet = 2;

    int ret = ops.GetAndAckEvent();

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_ackInterruptCount);
    ASSERT_EQ(2u, g_ackNevents);
    ASSERT_EQ(1, g_rearmCallCount);
    ASSERT_EQ(0u, ops.ack_event_num_);
}

TEST_F(UmqDataTxOpsTest, GetAndAckEvent_AccumulatedEvents_SumsAckNum)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).stubs().will(invoke(&MockUmqAckInterrupt));
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));
    g_getCqEventRet = 1;
    ops.ack_event_num_ = 3; /* 累积: 3+1=4 >= 1 触发 */

    int ret = ops.GetAndAckEvent();

    ASSERT_EQ(0, ret);
    ASSERT_EQ(4u, g_ackNevents);
    ASSERT_EQ(0u, ops.ack_event_num_);
}

TEST_F(UmqDataTxOpsTest, GetAndAckEvent_RearmFail_StillReturnsZero)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_get_cq_event).stubs().will(invoke(&MockUmqGetCqEvent));
    MOCKER_CPP(::umq_ack_interrupt).stubs().will(invoke(&MockUmqAckInterrupt));
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));
    g_getCqEventRet = 1;
    g_rearmRet = -UMQ_ERR_EAGAIN; /* rearm 失败仅 TODO 注释, 返回仍为 0 */

    int ret = ops.GetAndAckEvent();

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_ackInterruptCount);
    ASSERT_EQ(1, g_rearmCallCount);
}

/* ==================== PollTx ==================== */

TEST_F(UmqDataTxOpsTest, PollTx_GetAckFail_ReturnsMinusOne)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::GetAndAckEvent).stubs().will(invoke(&MockGetAndAckEvent));
    g_getAckRet = -1;
    ops.get_and_ack_event_ = true;

    int ret = ops.PollTx(sock.Get());

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(1, g_getAckCallCount);
    ASSERT_EQ(0, g_pollTxCount);
}

TEST_F(UmqDataTxOpsTest, PollTx_CasLoopRetry_SecondIterationSucceeds)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::GetAndAckEvent).stubs().will(invoke(&MockGetAndAckEvent));
    MOCKER_CPP(&DataTxOps::PollUmqTx).stubs().will(invoke(&MockPollUmqTx));
    g_getAckRet = 0;
    ops.get_and_ack_event_ = true;
    ops.epoll_event_num_.store(2);
    ops.expect_epoll_event_num_ = 1; /* 第一次 CAS 失败(2!=1), 循环重试后成功 */

    int ret = ops.PollTx(sock.Get());

    ASSERT_EQ(0, ret);
    ASSERT_EQ(2, g_getAckCallCount);
    ASSERT_EQ(2, g_pollTxCount);  /* 每次迭代都 PollUmqTx, CAS 失败重试共 2 次 */
    ASSERT_TRUE(g_pollTxToEmpty); /* get_and_ack 路径 poll_to_empty=true */
    ASSERT_FALSE(ops.get_and_ack_event_);
    ASSERT_EQ(0, ops.epoll_event_num_.load());
}

TEST_F(UmqDataTxOpsTest, PollTx_CasMatch_FirstIterationExits)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::GetAndAckEvent).stubs().will(invoke(&MockGetAndAckEvent));
    MOCKER_CPP(&DataTxOps::PollUmqTx).stubs().will(invoke(&MockPollUmqTx));
    g_getAckRet = 0;
    ops.get_and_ack_event_ = true;

    int ret = ops.PollTx(sock.Get());

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_getAckCallCount);
    ASSERT_EQ(1, g_pollTxCount);
    ASSERT_FALSE(ops.get_and_ack_event_);
}

TEST_F(UmqDataTxOpsTest, PollTx_NoAvail_PollsEmpty)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::PollUmqTx).stubs().will(invoke(&MockPollUmqTx));
    ops.tx_queue_avail_num_.store(0); /* 窗口耗尽: 走 PollUmqTx(非 to-empty) */

    int ret = ops.PollTx(sock.Get());

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_pollTxCount);
    ASSERT_FALSE(g_pollTxToEmpty);
    ASSERT_EQ(0, g_pollTxOnceCount);
}

TEST_F(UmqDataTxOpsTest, PollTx_HasAvail_PollsOnce)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::PollUmqTxOnce).stubs().will(invoke(&MockPollUmqTxOnce));
    ops.tx_queue_avail_num_.store(5); /* 有窗口: 单次轮询 */

    int ret = ops.PollTx(sock.Get());

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_pollTxOnceCount);
    ASSERT_EQ(0, g_pollTxCount);
}

/* ==================== PollUmqTx / PollUmqTxOnce / ForceDrainTx / QuickPollTx ==================== */

TEST_F(UmqDataTxOpsTest, PollUmqTx_Threshold_StopsAt32)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    g_doPollRet = 1;
    g_doPollRetAfterFirst = 1;

    int ret = ops.PollUmqTx(sock.Get(), false);

    ASSERT_EQ(0, ret);
    ASSERT_EQ(TX_RETRIEVE_THRESHOLD, g_doPollCount); /* 边界: 累积到 32 停止 */
}

TEST_F(UmqDataTxOpsTest, PollUmqTx_PollToEmpty_StopsOnZero)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    g_doPollRet = 1;
    g_doPollRetAfterFirst = 0; /* 第二次轮询为空: poll_to_empty 下首次空退出 */

    int ret = ops.PollUmqTx(sock.Get(), true);

    ASSERT_EQ(0, ret);
    ASSERT_EQ(2, g_doPollCount);
}

TEST_F(UmqDataTxOpsTest, PollUmqTx_FatalError_StopsImmediately)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    g_doPollRet = 1;
    g_doPollErrCode = ops_error_code::FATAL_ERROR;

    int ret = ops.PollUmqTx(sock.Get(), true);

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_doPollCount);
}

TEST_F(UmqDataTxOpsTest, PollUmqTx_PollMinusOne_StopsImmediately)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    g_doPollRet = -1;

    int ret = ops.PollUmqTx(sock.Get(), false);

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_doPollCount);
}

TEST_F(UmqDataTxOpsTest, PollUmqTxOnce_Positive_ReturnsCount)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    g_doPollRet = 5;

    int ret = ops.PollUmqTxOnce(sock.Get());

    ASSERT_EQ(5, ret);
    ASSERT_EQ(1, g_doPollCount);
}

TEST_F(UmqDataTxOpsTest, PollUmqTxOnce_Negative_ReturnsZero)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    g_doPollRet = -3;

    int ret = ops.PollUmqTxOnce(sock.Get());

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_doPollCount);
}

TEST_F(UmqDataTxOpsTest, ForceDrainTx_PollsToEmpty)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::PollUmqTx).stubs().will(invoke(&MockPollUmqTx));

    ops.ForceDrainTx(sock.Get());

    ASSERT_EQ(1, g_pollTxCount);
    ASSERT_TRUE(g_pollTxToEmpty);
}

TEST_F(UmqDataTxOpsTest, QuickPollTx_PollsOnce)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::PollUmqTxOnce).stubs().will(invoke(&MockPollUmqTxOnce));

    ops.QuickPollTx(sock.Get());

    ASSERT_EQ(1, g_pollTxOnceCount);
}

/* ==================== DpRearmTxInterrupt ==================== */

TEST_F(UmqDataTxOpsTest, DpRearmTxInterrupt_RetZero_SetsEagain)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));
    g_rearmRet = 0; /* 陷阱: ret==0 是"无事件需重臂"的成功路径, 返回 EAGAIN 而非 Convert */

    int ret = ops.DpRearmTxInterrupt();

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EAGAIN, errno);
    ASSERT_EQ(1, g_rearmCallCount);
}

TEST_F(UmqDataTxOpsTest, DpRearmTxInterrupt_GenericFail_ConvertsErrno)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));
    g_rearmRet = -UMQ_ERR_EPERM;
    errno = EIO; /* UMQ_FAIL + savedErrno∈{...EIO} → override 返回 EIO */

    int ret = ops.DpRearmTxInterrupt();

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EIO, errno);
}

TEST_F(UmqDataTxOpsTest, DpRearmTxInterrupt_EagainFail_ConvertsToEagain)
{
    DataTxOps ops = MakeOps();
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(invoke(&MockUmqRearmInterrupt));
    g_rearmRet = -UMQ_ERR_EAGAIN;
    errno = EIO; /* EAGAIN 不在 override 列表: 走映射表 → EAGAIN */

    int ret = ops.DpRearmTxInterrupt();

    ASSERT_EQ(-1, ret);
    ASSERT_EQ(EAGAIN, errno);
}

/* ==================== DoUmqTxPoll ==================== */

TEST_F(UmqDataTxOpsTest, DoUmqTxPoll_RnrFatal_ShutsDownAndCloses)
{
    DataTxOps ops = MakeOps();
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 1;
    umqSock->SetRnrBlocked(true);
    UmqSocketCold *cold = umqSock->GetOrCreateCold();
    cold->rnr_block_start_ns.store(1, std::memory_order_relaxed); /* 过去时间戳: elapsed 必然 >= 1ms */

    ops_error_code err_code = ops_error_code::OK;
    int ret = ops.DoUmqTxPoll(sock.Get(), err_code);

    ASSERT_EQ(0, ret);
    ASSERT_EQ(1, g_shutdownCallCount);
    ASSERT_EQ(TEST_FD, g_shutdownFd);
    ASSERT_EQ(SOCK_STAT_CLOSE, sock->State());
    ASSERT_FALSE(umqSock->IsRnrBlocked()); /* CAS 已复位 */
}

TEST_F(UmqDataTxOpsTest, DoUmqTxPoll_Normal_ReturnsPollCount)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).stubs().will(invoke(&MockPollUmqTxInternal));
    g_pollInternalRet = 5;

    ops_error_code err_code = ops_error_code::OK;
    int ret = ops.DoUmqTxPoll(sock.Get(), err_code);

    ASSERT_EQ(5, ret);
    ASSERT_EQ(0, g_shutdownCallCount);
}

TEST_F(UmqDataTxOpsTest, DoUmqTxPoll_ClosFatalStatus_CooldownsPorts)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    UmqSocketPtr umqSock = MakeClosSockWithPorts(2, sock);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).stubs().will(invoke(&MockPollUmqTxInternal));
    g_pollInternalInvokeCb = true;
    g_pollInternalQbuf = &m_badQbuf;
    m_badQbuf.status = UMQ_BUF_LOC_LEN_ERR; /* 五致命 status 之一 */
    UmqSocketCold *cold = umqSock->GetOrCreateCold();

    ops_error_code err_code = ops_error_code::OK;
    int ret = ops.DoUmqTxPoll(sock.Get(), err_code);

    ASSERT_EQ(0, ret);
    /* 错误回调: shutdown + 断链 + CLOS 下两个 port 全部冷却 */
    ASSERT_EQ(1, g_shutdownCallCount);
    ASSERT_EQ(TEST_FD, g_shutdownFd);
    ASSERT_EQ(SOCK_STAT_CLOSE, sock->State());
    ASSERT_TRUE(PortCooldownManager::IsPortInCooldown(cold->used_ports[0]));
    ASSERT_TRUE(PortCooldownManager::IsPortInCooldown(cold->used_ports[1]));
}

TEST_F(UmqDataTxOpsTest, DoUmqTxPoll_ClosNonFatalStatus_NoCooldown)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock;
    UmqSocketPtr umqSock = MakeClosSockWithPorts(1, sock, 5); /* port 5: 避开 ClosFatal 用例的 3/4 */
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).stubs().will(invoke(&MockPollUmqTxInternal));
    g_pollInternalInvokeCb = true;
    g_pollInternalQbuf = &m_badQbuf;
    m_badQbuf.status = UMQ_BUF_LOC_OPERATION_ERR; /* 不在五致命 status 列表 */
    UmqSocketCold *cold = umqSock->GetOrCreateCold();

    ops_error_code err_code = ops_error_code::OK;
    int ret = ops.DoUmqTxPoll(sock.Get(), err_code);

    ASSERT_EQ(0, ret);
    ASSERT_EQ(SOCK_STAT_CLOSE, sock->State());
    ASSERT_FALSE(PortCooldownManager::IsPortInCooldown(cold->used_ports[0]));
}

TEST_F(UmqDataTxOpsTest, DoUmqTxPoll_NonClosFatalStatus_NoCooldown)
{
    DataTxOps ops = MakeOps();
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    MOCKER_CPP(&UmqTxHelper::PollUmqTxInternal).stubs().will(invoke(&MockPollUmqTxInternal));
    g_pollInternalInvokeCb = true;
    g_pollInternalQbuf = &m_badQbuf;
    m_badQbuf.status = UMQ_FAKE_BUF_FC_ERR_FATAL; /* 致命 status 但拓扑非 CLOS */
    UmqSocketCold *cold = umqSock->GetOrCreateCold();
    cold->used_ports = std::make_unique<umq_port_id_t[]>(1);
    cold->used_ports_num = 1;
    cold->used_ports[0].bs.port_idx = 6;

    ops_error_code err_code = ops_error_code::OK;
    int ret = ops.DoUmqTxPoll(sock.Get(), err_code);

    ASSERT_EQ(0, ret);
    ASSERT_EQ(SOCK_STAT_CLOSE, sock->State());
    ASSERT_FALSE(PortCooldownManager::IsPortInCooldown(cold->used_ports[0]));
}

/* ==================== Writable ==================== */

TEST_F(UmqDataTxOpsTest, Writable_RnrBlocked_ReturnsFalse)
{
    DataTxOps ops = MakeOps();
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    umqSock->SetRnrBlocked(true); /* RNR 反压期间不可写 */

    ASSERT_FALSE(ops.Writable(sock));
}

TEST_F(UmqDataTxOpsTest, Writable_NonPoolTp_ReturnsTrue)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    UmqSetting::UMQ_TP_TYPE = SINGLE; /* 非 POOL 拓扑不做 jetty 判定 */

    ASSERT_TRUE(ops.Writable(sock));
}

TEST_F(UmqDataTxOpsTest, Writable_PoolWaiting_ReturnsFalse)
{
    DataTxOps ops = MakeOps();
    UmqSocketPtr umqSock = MakeRef<UmqSocket>(TEST_FD);
    SocketPtr sock = RefConvert<UmqSocket, Socket>(umqSock);
    ASSERT_TRUE(umqSock->TryAcquireForWaiting()); /* jetty 分配状态 → WAITING */

    ASSERT_FALSE(ops.Writable(sock));
}

TEST_F(UmqDataTxOpsTest, Writable_PoolIdle_ReturnsTrue)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock(); /* 新 socket: jetty 状态 IDLE */

    ASSERT_TRUE(ops.Writable(sock));
}

/* ==================== WakeUpTx ==================== */

TEST_F(UmqDataTxOpsTest, WakeUpTx_NoAwakeFlag_NoNotify)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    ops.need_fc_awake_.store(false);

    ops.WakeUpTx(sock.Get()); /* 无 EAGAIN-all 历史: 直接返回 */

    ASSERT_FALSE(ops.need_fc_awake_.load());
    /* 未走 NotifyWritable 路径: writable-ready 版本未递增(初始值 0x1 = version 0 + bit0) */
    ASSERT_EQ(0x1u, AsSocketBase(sock)->GetVersionedWritableReady());
}

TEST_F(UmqDataTxOpsTest, WakeUpTx_AwakeFlag_NotifiesWritable)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    ops.need_fc_awake_.store(true);

    ops.WakeUpTx(sock.Get()); /* NotifyWritable: 未注册 EPOLLOUT, SetWritableReady 后返回 0 */

    ASSERT_FALSE(ops.need_fc_awake_.load());                      /* 标志已消费 */
    ASSERT_TRUE(AsSocketBase(sock)->GetVersionedWritableReady()); /* bit0=1: writable 已被标记 */
}

TEST_F(UmqDataTxOpsTest, WakeUpTx_NotifyUnregistered_LogsAndDrops)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    ops.need_fc_awake_.store(true);
    AsSocketBase(sock)->events_.store(EPOLLOUT); /* 上层已关注 EPOLLOUT 但未注册 epoll fd: DoNotifyWritable 返回 -1 */

    ops.WakeUpTx(sock.Get()); /* -1 分支: 仅错误日志 */

    ASSERT_FALSE(ops.need_fc_awake_.load());
}

/* ==================== FlushTx ==================== */

TEST_F(UmqDataTxOpsTest, FlushTx_NoPendingWr_ReturnsEarly)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));

    ops.FlushTx(sock.Get(), 100);

    ASSERT_EQ(0, g_doPollCount); /* threshold==0: 不进轮询循环 */
}

TEST_F(UmqDataTxOpsTest, FlushTx_PollsUntilThreshold)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    ops.tx_queue_avail_num_.store(TEST_TX_DEPTH - 3); /* threshold=3 */
    g_doPollRet = 1;
    g_doPollRetAfterFirst = 1;

    ops.FlushTx(sock.Get(), 1000);

    ASSERT_EQ(3, g_doPollCount);  /* 每次 1 个 CQE, 3 次后累积达 threshold */
    ASSERT_EQ(0, g_bufFreeCount); /* 无 unsignaled 缓存: 不释放 */
}

TEST_F(UmqDataTxOpsTest, FlushTx_Timeout_YieldsAndBreaks)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    ops.tx_queue_avail_num_.store(TEST_TX_DEPTH - 1); /* threshold=1 */
    g_doPollRet = 0;                                  /* 无 CQE: 走 PollerYield 分支, 直到超时熔断 */

    ops.FlushTx(sock.Get(), 1);

    ASSERT_GE(g_doPollCount, 1);
    ASSERT_EQ(0, g_bufFreeCount);
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH - 1), ops.tx_queue_avail_num_.load()); /* 未释放未变化 */
}

TEST_F(UmqDataTxOpsTest, FlushTx_FatalError_StopsBeforeCacheRelease)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    ops.tx_queue_avail_num_.store(TEST_TX_DEPTH - 3);
    g_doPollRet = 1;
    g_doPollErrCode = ops_error_code::FATAL_ERROR;
    ops.unsignaled_wr_num_ = 1; /* 即使有缓存, FATAL 也跳过释放 */
    ops.head_buf_.first = InitPostChain(1);

    ops.FlushTx(sock.Get(), 1000);

    ASSERT_EQ(1, g_doPollCount);
    ASSERT_EQ(0, g_bufFreeCount);
}

TEST_F(UmqDataTxOpsTest, FlushTx_CacheRelease_FreesUnsignedList)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    ops.tx_queue_avail_num_.store(TEST_TX_DEPTH - 2); /* threshold=2 */
    g_doPollRet = 1;
    g_doPollRetAfterFirst = 1;
    ops.unsignaled_wr_num_ = 1;
    umq_buf_t *head = InitPostChain(2);
    m_qbuf1.total_data_size = 100; /* WR0: qbuf1(40)+qbuf2(60) */
    m_qbuf2.total_data_size = 60;
    ops.head_buf_.first = head;
    ops.tail_buf_.first = &m_qbuf2;

    ops.FlushTx(sock.Get(), 1000);

    ASSERT_EQ(2, g_doPollCount); /* 达 threshold 退出 */
    /* unsignaled 缓存释放: 释放 head 链并回补 avail */
    ASSERT_EQ(1, g_bufFreeCount);
    ASSERT_EQ(&m_qbuf1, g_freedBuf);
    ASSERT_EQ(nullptr, m_qbuf2.qbuf_next); /* last_qbuf 断链 */
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH - 1), ops.tx_queue_avail_num_.load());
}

TEST_F(UmqDataTxOpsTest, FlushTx_TimeoutWithCache_ReleasesCache)
{
    DataTxOps ops = MakeOps();
    SocketPtr sock = MakeSock();
    MOCKER_CPP(&DataTxOps::DoUmqTxPoll).stubs().will(invoke(&MockDoUmqTxPoll));
    MOCKER_CPP(::umq_data_to_head).stubs().will(invoke(&MockUmqDataToHead));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(&MockUmqBufFree));
    ops.tx_queue_avail_num_.store(TEST_TX_DEPTH - 1); /* threshold=1 */
    g_doPollRet = 0;                                  /* 无 CQE → 超时退出 */
    ops.unsignaled_wr_num_ = 1;
    umq_buf_t *head = InitPostChain(1);
    m_qbuf1.total_data_size = TEST_DATA_SIZE;
    ops.head_buf_.first = head;
    ops.tail_buf_.first = &m_qbuf1;

    ops.FlushTx(sock.Get(), 1);

    /* 超时熔断后仍执行缓存释放(代码在循环外) */
    ASSERT_EQ(1, g_bufFreeCount);
    ASSERT_EQ(&m_qbuf1, g_freedBuf);
    ASSERT_EQ(static_cast<uint16_t>(TEST_TX_DEPTH), ops.tx_queue_avail_num_.load());
}

} // namespace
