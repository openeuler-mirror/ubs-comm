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

#include "umq_socket.h"

#include <netinet/in.h>
#include <sys/epoll.h>

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_defines.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_bigdata.h"
#include "core/ubsocket_core_types.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket.h"
#include "core/umq/umq_buffer_receive_queue.h"
#include "core/umq/umq_data_plane.h"
#include "core/umq/umq_eid_table.h"
#include "core/umq/umq_setting.h"
#include "core/umq/umq_tp_tx_epoll_runner_ops.h"
#include "profiling/statistics/cli_message.h"
#include "profiling/statistics/statistics_statsmgr.h"
#include "umq_dfx_api.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const int TEST_FD = 42;
static const uint64_t TEST_UMQ_HANDLE = 12345;
static const uint64_t TEST_MAIN_HANDLE = 8888;
static const uint64_t TEST_UMQ_HANDLE_2 = 23456;

static void SetLibcApiPtrsToNull()
{
    LibcApi::shutdown_ptr = nullptr;
    LibcApi::close_ptr = nullptr;
}

/* ---- ::umq_create mock: 记录 handle 序列 ---- */
static uint64_t g_createSeq = 0;
static uint64_t g_createReturn = TEST_UMQ_HANDLE;

static uint64_t MockUmqCreate(umq_create_option_t *option)
{
    (void)option;
    return g_createReturn;
}

/* ---- ::umq_create mock: 首次创建成功后抢占 main eid（模拟竞态败者路径） ---- */
static uint64_t g_createSeqForRace = 0;

static uint64_t MockUmqCreateThenInsertMain(umq_create_option_t *option)
{
    (void)option;
    ++g_createSeqForRace;
    if (g_createSeqForRace == 1) {
        UmqEidTable::Instance().Add({}, RM_TP, TEST_MAIN_HANDLE);
    }
    return g_createReturn;
}

/* ---- ::umq_destroy mock ---- */
static int g_destroyRet = UMQ_SUCCESS;

static int MockUmqDestroy(uint64_t umqh)
{
    (void)umqh;
    return g_destroyRet;
}

/* ---- ::umq_unbind mock ---- */
static int g_unbindRet = UMQ_SUCCESS;

static int MockUmqUnbind(uint64_t umqh)
{
    (void)umqh;
    return g_unbindRet;
}

/* ---- ::umq_interrupt_fd_get mock ---- */
static int g_interruptFd = 100;

static int MockUmqInterruptFdGet(uint64_t umqh, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    return g_interruptFd;
}

/* ---- ::umq_state_get mock ---- */
static umq_state_t g_stateGetRet = QUEUE_STATE_READY;

static umq_state_t MockUmqStateGet(uint64_t umqh)
{
    (void)umqh;
    return g_stateGetRet;
}

/* ---- ::umq_dev_add mock ---- */
static int g_devAddRet = 0;

static int MockUmqDevAdd(umq_trans_info_t *trans_info)
{
    (void)trans_info;
    return g_devAddRet;
}

/* ---- ::umq_stats_* mock ---- */
static int g_statsGetRet = 0;

static int MockStatsFlowControlGet(uint64_t umqh, umq_flow_control_stats_t *stats)
{
    (void)umqh;
    (void)stats;
    return g_statsGetRet;
}

static int MockStatsQbufPoolGet(uint64_t umqh, umq_qbuf_pool_stats_t *stats)
{
    (void)umqh;
    (void)stats;
    return g_statsGetRet;
}

static int MockInfoGet(uint64_t umqh, umq_info_t *info)
{
    (void)umqh;
    (void)info;
    return g_statsGetRet;
}

static int MockStatsIoGet(uint64_t umqh, umq_packet_stats_t *stats)
{
    (void)umqh;
    (void)stats;
    return g_statsGetRet;
}

static int MockPerfStatsGet(umq_perf_stats_t *stats)
{
    (void)stats;
    return g_statsGetRet;
}

static int MockTpPerfStop(umq_trans_mode_t trans_mode)
{
    (void)trans_mode;
    return g_statsGetRet;
}

static int MockTpPerfInfoGet(umq_trans_mode_t trans_mode, char *perf_buf, uint32_t *length)
{
    (void)trans_mode;
    (void)perf_buf;
    (void)length;
    return g_statsGetRet;
}

static int MockTpPerfStart(umq_trans_mode_t trans_mode)
{
    (void)trans_mode;
    return g_statsGetRet;
}
} // namespace

class UmqSocketTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_MONITOR_ENABLE = false;
        GlobalSetting::UBS_RNR_BACKPRESSURE_ENABLED = true;
        GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 0;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
        UmqSetting::UMQ_TP_TYPE = POOL;
        UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
        UmqSetting::UMQ_DEV_IP.clear();
        UmqSetting::UMQ_DEV_NAME.clear();
        UmqSetting::UMQ_IS_BONDING = false;
        UmqSetting::UMQ_LOCAL_EID = {};
        UmqSetting::UMQ_EID_INDEX = 0;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        g_createReturn = TEST_UMQ_HANDLE;
        g_createSeq = 0;
        g_createSeqForRace = 0;
        g_destroyRet = UMQ_SUCCESS;
        g_unbindRet = UMQ_SUCCESS;
        g_interruptFd = 100;
        g_stateGetRet = QUEUE_STATE_READY;
        g_devAddRet = 0;
        g_statsGetRet = 0;
    }

    void TearDown() override
    {
        sock_.umq_handle_ = UMQ_INVALID_HANDLE;
        sock_.umq_is_bind_remote_ = false;
        sock_.retiring_.store(false);
        sock_.retire_drained_ = false;
        UmqEidTable::Instance().RemoveMainUmq(TEST_MAIN_HANDLE);
        GlobalMockObject::verify();
        SetLibcApiPtrsToNull();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        errno = 0;
    }

    /* 安装数据面条目（ReinitTxOps 构造整条目 + ReinitRxOps 取址），
     * 并镜像 GenerateSocketCommOps 装配通用层壳 txw/rxw（DataTx/DataRx
     * 构造忽略 sock 参数，仅记录 ops 指针，故传空 SocketPtr 即可）。 */
    void InstallDataPlane()
    {
        ASSERT_NE(sock_.ReinitTxOps(), nullptr);
        ASSERT_NE(sock_.ReinitRxOps(), nullptr);
        DataPlaneEntry *entry = DataPlaneTable::Live(sock_.raw_socket_);
        ASSERT_NE(entry, nullptr);
        SocketPtr nullSock;
        entry->txw = DataTx(nullSock, &entry->tx);
        entry->rxw = DataRx(nullSock, &entry->rx);
    }

    UmqSocket sock_{TEST_FD};
};

// ==================== GetAndPopQbuf errno 契约 ====================

/* issue#33（0830 753×753 core）：rxQueue 空指针分支曾裸返 -1，线程残留 errno（可为 0）
 * 经 ubs_poll → DoUbsNativeRead → OnUbNativeMessages 透传进 brpc
 * Controller::SetFailed 的 CHECK(error_code != 0)，整进程 abort。
 * 契约：本函数任何 <0 返回都必须携带非零 errno；空队列语义 = EAGAIN（已注册
 * 未就绪的暂态，与 PollRegisteredSocketWithoutQueueReturnsEagain 一致——
 * 此前靠 MSG_PEEK 的 EAGAIN 残留碰巧兑现，本测试把它钉成显式契约）。 */
TEST_F(UmqSocketTest, GetAndPopQbuf_NullRxQueue_SetsExplicitEagain)
{
    umq_buf_t *buf = nullptr;
    ASSERT_EQ(sock_.rxQueue, nullptr); /* 新建 socket 未走建链，rxQueue 天然为空 */
    errno = 0;                         /* 复刻现场：残留 errno 恰为 0 */
    EXPECT_EQ(sock_.GetAndPopQbuf(&buf, 1), -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

// ==================== ShouldRegisterTxEvent ====================

TEST_F(UmqSocketTest, ShouldRegisterTxEvent_Single_ValidHandle_ReturnsTrue)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    EXPECT_TRUE(sock_.ShouldRegisterTxEvent());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, ShouldRegisterTxEvent_Single_InvalidHandle_ReturnsFalse)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    sock_.umq_handle_ = UMQ_INVALID_HANDLE;
    EXPECT_FALSE(sock_.ShouldRegisterTxEvent());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, ShouldRegisterTxEvent_Pool_ValidHandle_ReturnsFalse)
{
    UmqSetting::UMQ_TP_TYPE = POOL;
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    EXPECT_FALSE(sock_.ShouldRegisterTxEvent());
    GlobalMockObject::verify();
}

// ==================== ProcessEpollEvent ====================

TEST_F(UmqSocketTest, ProcessEpollEvent_SocketOut_ReturnsOk)
{
    InstallDataPlane();
    struct epoll_event evt {};
    EpollEvent epollEvent(EPOLL_EVENT_UB_SOCKET_OUT, TEST_FD, evt);
    evt.data.ptr = &epollEvent;
    EXPECT_EQ(sock_.ProcessEpollEvent(evt), UBS_OK);
    DataTxOps *txOps = sock_.GetUmqTxOps();
    ASSERT_NE(txOps, nullptr);
    EXPECT_EQ(txOps->epoll_event_num_.load(std::memory_order_relaxed), 1u);
    EXPECT_TRUE(txOps->get_and_ack_event_);
    GlobalMockObject::verify();
}

// ==================== GetOrCreateCold / GetCold / GetUsedPorts ====================

TEST_F(UmqSocketTest, GetOrCreateCold_NoExt_CreatesCold)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(sock_.GetCold(), cold);
    EXPECT_EQ(cold->fc_event_fd, -1);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetOrCreateCold_SecondCall_ReturnsSame)
{
    UmqSocketCold *cold1 = sock_.GetOrCreateCold();
    ASSERT_NE(cold1, nullptr);
    UmqSocketCold *cold2 = sock_.GetOrCreateCold();
    EXPECT_EQ(cold1, cold2);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetCold_NoExt_ReturnsNull)
{
    EXPECT_EQ(sock_.GetCold(), nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetUsedPorts_NoCold_ReturnsNullZero)
{
    auto [ports, num] = sock_.GetUsedPorts();
    EXPECT_EQ(ports, nullptr);
    EXPECT_EQ(num, 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetUsedPorts_WithCold_ReturnsPorts)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    umq_port_id_t port = {};
    cold->used_ports.reset(new umq_port_id_t[2]{port, port});
    cold->used_ports_num = 2;
    auto [ports, num] = sock_.GetUsedPorts();
    ASSERT_NE(ports, nullptr);
    EXPECT_EQ(num, 2u);
    GlobalMockObject::verify();
}

// ==================== CreateLocalUmq ====================

TEST_F(UmqSocketTest, CreateLocalUmq_AlreadyCreated_ReturnsError)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_DevIpSet_ReturnsDegradableError)
{
    UmqSetting::UMQ_DEV_IP = "10.0.0.1";
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    errno = 0;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType),
              static_cast<int32_t>(UBS_SET_DEV_INFO | UBS_DEGRADABLE_MASK));
    EXPECT_EQ(errno, ENOTSUP);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_DevNameSet_NonBonding_Success)
{
    UmqSetting::UMQ_DEV_NAME = "bonding_dev_0";
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_OK);
    EXPECT_EQ(sock_.umq_handle_, TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_DevNameSet_BondingBackup_Success)
{
    UmqSetting::UMQ_DEV_NAME = "bonding_dev_0";
    UmqSetting::UMQ_LOCAL_EID = {};
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_OK);
    EXPECT_EQ(sock_.umq_handle_, TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_NoDev_DefaultBondingDev0_Success)
{
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_OK);
    EXPECT_EQ(sock_.umq_handle_, TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_CreateFails_ReturnsCreateError)
{
    MOCKER_CPP(::umq_create).stubs().will(returnValue(static_cast<uint64_t>(UMQ_INVALID_HANDLE)));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    errno = EIO;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType),
              static_cast<int32_t>(UBS_UMQ_CREATE | UBS_RETRYABLE_MASK | UBS_DEGRADABLE_MASK));
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_UsedPortsNonZero_StoresUsedPorts)
{
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_port_id_t ports[2] = {};
    ports[0].value = 1;
    ports[1].value = 2;
    usedPorts.port = ports;
    usedPorts.num = 2;
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_OK);
    UmqSocketCold *cold = sock_.GetCold();
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(cold->used_ports_num, 2u);
    ASSERT_NE(cold->used_ports, nullptr);
    EXPECT_EQ(cold->used_ports[0].value, 1u);
    EXPECT_EQ(cold->used_ports[1].value, 2u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_SingleTp_InterruptFdGetFail_ReturnsError)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(-1));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_SingleTp_RearmInterruptFail_ReturnsError)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(returnValue(-1));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_SingleTp_Success)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(returnValue(0));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_OK);
    EXPECT_EQ(sock_.umq_handle_, TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

// ==================== CreateSubUmq ====================

TEST_F(UmqSocketTest, CreateSubUmq_NoShareJfr_Success)
{
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.CreateSubUmq(&cfg, &localEid), TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateSubUmq_NoShareJfr_CreateFails)
{
    MOCKER_CPP(::umq_create).stubs().will(returnValue(static_cast<uint64_t>(UMQ_INVALID_HANDLE)));
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.CreateSubUmq(&cfg, &localEid), UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateSubUmq_ShareJfr_MainFromTable_Success)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    UmqEidTable::Instance().Add({}, RM_TP, TEST_MAIN_HANDLE);
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.CreateSubUmq(&cfg, &localEid), TEST_UMQ_HANDLE);
    EXPECT_EQ(sock_.share_umq_handle_, TEST_MAIN_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateSubUmq_ShareJfr_MainListEmpty_ReturnsInvalid)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    /* 先放入一个 entry 使 Get 命中，再清空其列表不可行——改用不同 eid 让 Get 不命中：
     * Get 不命中时会创建 main，故此处直接 mock umq_create 失败以覆盖 main 创建失败路径 */
    MOCKER_CPP(::umq_create).stubs().will(returnValue(static_cast<uint64_t>(UMQ_INVALID_HANDLE)));
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.CreateSubUmq(&cfg, &localEid), UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateSubUmq_ShareJfr_SubCreateFails_ReturnsInvalid)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    MOCKER_CPP(::umq_create).stubs().will(returnValue(static_cast<uint64_t>(UMQ_INVALID_HANDLE)));
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.CreateSubUmq(&cfg, &localEid), UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

// ==================== GetOrCreateMainUmq ====================

TEST_F(UmqSocketTest, GetOrCreateMainUmq_MainFromTable_ReturnsHandle)
{
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    UmqEidTable::Instance().Add({}, RM_TP, TEST_MAIN_HANDLE);
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.GetOrCreateMainUmq(&cfg, &localEid), TEST_MAIN_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetOrCreateMainUmq_CreateFails_ReturnsInvalid)
{
    MOCKER_CPP(::umq_create).stubs().will(returnValue(static_cast<uint64_t>(UMQ_INVALID_HANDLE)));
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.GetOrCreateMainUmq(&cfg, &localEid), UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetOrCreateMainUmq_RaceLose_DestroysNewReturnsExisting)
{
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreateThenInsertMain));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    umq_create_option_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    umq_eid_t localEid{};
    EXPECT_EQ(sock_.GetOrCreateMainUmq(&cfg, &localEid), TEST_MAIN_HANDLE);
    GlobalMockObject::verify();
}

// ==================== UpdateRxQueueAvailNum ====================

TEST_F(UmqSocketTest, UpdateRxQueueAvailNum_StateReady_SetsRxAvail)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    InstallDataPlane();
    MOCKER_CPP(::umq_state_get).stubs().will(invoke(&MockUmqStateGet));
    EXPECT_EQ(sock_.UpdateRxQueueAvailNum(), 0);
    DataRxOps *rxOps = sock_.GetUmqRxOps();
    ASSERT_NE(rxOps, nullptr);
    EXPECT_EQ(rxOps->rx_queue_avail_num_, GlobalSetting::UBS_RX_DEPTH);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UpdateRxQueueAvailNum_StateNotReady_ReturnsError)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    InstallDataPlane();
    g_stateGetRet = QUEUE_STATE_ERR;
    MOCKER_CPP(::umq_state_get).stubs().will(invoke(&MockUmqStateGet));
    errno = EINVAL;
    EXPECT_EQ(sock_.UpdateRxQueueAvailNum(), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UpdateRxQueueAvailNum_StateReady_NoOps_ReturnsError)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_state_get).stubs().will(invoke(&MockUmqStateGet));
    EXPECT_EQ(sock_.UpdateRxQueueAvailNum(), UBS_ERROR);
    GlobalMockObject::verify();
}

// ==================== UnbindAndFlushRemoteUmq ====================

TEST_F(UmqSocketTest, UnbindAndFlushRemoteUmq_NotBound_Returns)
{
    sock_.umq_is_bind_remote_ = false;
    sock_.UnbindAndFlushRemoteUmq(&sock_);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnbindAndFlushRemoteUmq_Bound_Success)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    sock_.umq_is_bind_remote_ = true;
    InstallDataPlane();
    MOCKER_CPP(::umq_ack_interrupt).stubs();
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    MOCKER_CPP(&DataTxOps::FlushTx).stubs();
    MOCKER_CPP(&DataRxOps::FlushRx).stubs();
    sock_.UnbindAndFlushRemoteUmq(&sock_);
    EXPECT_FALSE(sock_.umq_is_bind_remote_);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnbindAndFlushRemoteUmq_Bound_AckEventNumPositive)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    sock_.umq_is_bind_remote_ = true;
    InstallDataPlane();
    DataTxOps *txOps = sock_.GetUmqTxOps();
    DataRxOps *rxOps = sock_.GetUmqRxOps();
    ASSERT_NE(txOps, nullptr);
    ASSERT_NE(rxOps, nullptr);
    txOps->ack_event_num_ = 3;
    rxOps->ack_event_num_ = 2;
    MOCKER_CPP(::umq_ack_interrupt).stubs();
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    MOCKER_CPP(&DataTxOps::FlushTx).stubs();
    MOCKER_CPP(&DataRxOps::FlushRx).stubs();
    sock_.UnbindAndFlushRemoteUmq(&sock_);
    EXPECT_EQ(txOps->ack_event_num_, 0u);
    EXPECT_EQ(rxOps->ack_event_num_, 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnbindAndFlushRemoteUmq_Bound_UnbindFails)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    sock_.umq_is_bind_remote_ = true;
    InstallDataPlane();
    g_unbindRet = UMQ_FAIL;
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    MOCKER_CPP(&DataTxOps::FlushTx).stubs();
    MOCKER_CPP(&DataRxOps::FlushRx).stubs();
    sock_.UnbindAndFlushRemoteUmq(&sock_);
    EXPECT_FALSE(sock_.umq_is_bind_remote_);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnbindAndFlushRemoteUmq_Bound_NoOps_SkipsOpsCleanup)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    sock_.umq_is_bind_remote_ = true;
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    sock_.UnbindAndFlushRemoteUmq(&sock_);
    EXPECT_FALSE(sock_.umq_is_bind_remote_);
    GlobalMockObject::verify();
}

// ==================== DestroyLocalUmq ====================

TEST_F(UmqSocketTest, DestroyLocalUmq_InvalidHandle_Returns)
{
    sock_.umq_handle_ = UMQ_INVALID_HANDLE;
    sock_.DestroyLocalUmq();
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, DestroyLocalUmq_Success_ResetsHandle)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    sock_.DestroyLocalUmq();
    EXPECT_EQ(sock_.umq_handle_, UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, DestroyLocalUmq_RetryThenSuccess)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_destroy).stubs()
        .will(returnValue(UMQ_FAIL))
        .then(returnValue(UMQ_FAIL))
        .then(returnValue(UMQ_SUCCESS));
    MOCKER_CPP(::usleep).stubs().will(returnValue(0));
    sock_.DestroyLocalUmq();
    EXPECT_EQ(sock_.umq_handle_, UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, DestroyLocalUmq_AllRetriesFail_ResetsHandle)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_destroy).stubs().will(returnValue(UMQ_FAIL));
    MOCKER_CPP(::usleep).stubs().will(returnValue(0));
    sock_.DestroyLocalUmq();
    EXPECT_EQ(sock_.umq_handle_, UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

// ==================== AddTxEvent ====================

TEST_F(UmqSocketTest, AddTxEvent_InterruptFdFail_ReturnsMinusOne)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(-1));
    struct epoll_event event {};
    EXPECT_EQ(sock_.AddTxEvent(nullptr, 1, &event), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, AddTxEvent_EpollCtlFail_ReturnsMinusOne)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    MOCKER_CPP(::epoll_ctl).stubs().will(returnValue(-1));
    struct epoll_event event {};
    errno = EPERM;
    /* 失败路径会访问 sock->raw_socket_，必须传有效 socket（adopt 避免引用计数） */
    SocketPtr sockPtr(static_cast<Socket *>(&sock_), SocketPtr::adopt_ref);
    EXPECT_EQ(sock_.AddTxEvent(sockPtr, 1, &event), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, AddTxEvent_Success_ReturnsZero)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    MOCKER_CPP(::epoll_ctl).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_rearm_interrupt).stubs().will(returnValue(0));
    struct epoll_event event {};
    EXPECT_EQ(sock_.AddTxEvent(nullptr, 1, &event), 0);
    GlobalMockObject::verify();
}

// ==================== DelTxEvent ====================

TEST_F(UmqSocketTest, DelTxEvent_InvalidHandle_ReturnsZero)
{
    sock_.umq_handle_ = UMQ_INVALID_HANDLE;
    EXPECT_EQ(sock_.DelTxEvent(nullptr, 1), 0);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, DelTxEvent_InterruptFdFail_ReturnsMinusOne)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(-1));
    EXPECT_EQ(sock_.DelTxEvent(nullptr, 1), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, DelTxEvent_EpollCtlFail_Uninit_ReturnsMinusOne)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    MOCKER_CPP(::epoll_ctl).stubs().will(returnValue(-1));
    MOCKER_CPP(::umq_uninit).stubs();
    errno = EPERM;
    EXPECT_EQ(sock_.DelTxEvent(nullptr, 1), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, DelTxEvent_Success_ReturnsZero)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    MOCKER_CPP(::epoll_ctl).stubs().will(returnValue(0));
    EXPECT_EQ(sock_.DelTxEvent(nullptr, 1), 0);
    GlobalMockObject::verify();
}

// ==================== GetTxFd ====================

TEST_F(UmqSocketTest, GetTxFd_Success_ReturnsFd)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    EXPECT_EQ(sock_.GetTxFd(), 100);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetTxFd_InterruptFdFail_ReturnsMinusOne)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(-1));
    errno = EINVAL;
    EXPECT_EQ(sock_.GetTxFd(), -1);
    GlobalMockObject::verify();
}

// ==================== RNR 反压 ====================

TEST_F(UmqSocketTest, OnRnrEnter_CreatesCold_SetsCountAndStart)
{
    sock_.OnRnrEnter();
    UmqSocketCold *cold = sock_.GetCold();
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(cold->rnr_enter_cnt.load(), 1u);
    EXPECT_NE(cold->rnr_block_start_ns.load(), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, OnRnrRecover_NoCold_Returns)
{
    sock_.OnRnrRecover();
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, OnRnrRecover_WithCold_UpdatesStats)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_block_start_ns.store(ubsocket_get_timeNs() - 5000000, std::memory_order_relaxed); /* 5ms 前 */
    sock_.OnRnrRecover();
    EXPECT_EQ(cold->rnr_recover_cnt.load(), 1u);
    EXPECT_EQ(cold->rnr_latency_cnt.load(), 1u);
    EXPECT_GE(cold->rnr_latency_min_us.load(), 1u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, OnRnrRecover_StartZero_NoLatency)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_block_start_ns.store(0, std::memory_order_relaxed);
    sock_.OnRnrRecover();
    EXPECT_EQ(cold->rnr_recover_cnt.load(), 1u);
    EXPECT_EQ(cold->rnr_latency_cnt.load(), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, OnRnrRecover_PrintThreshold_ResetsCounters)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_latency_cnt.store(UMQ_RNR_LATENCY_PRINT_CNT - 1, std::memory_order_relaxed);
    cold->rnr_latency_sum_us.store(1000, std::memory_order_relaxed);
    cold->rnr_block_start_ns.store(ubsocket_get_timeNs() - 5000000, std::memory_order_relaxed);
    sock_.OnRnrRecover();
    EXPECT_EQ(cold->rnr_latency_cnt.load(), 0u);
    EXPECT_EQ(cold->rnr_latency_sum_us.load(), 0u);
    EXPECT_EQ(cold->rnr_block_start_ns.load(), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, OnRnrTimeout_NoCold_Returns)
{
    sock_.OnRnrTimeout();
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, OnRnrTimeout_WithCold_Increments)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    sock_.OnRnrTimeout();
    EXPECT_EQ(cold->rnr_timeout_cnt.load(), 1u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, IsRnrBlockFatal_NotBlocked_ReturnsFalse)
{
    sock_.SetRnrBlocked(false);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 100;
    EXPECT_FALSE(sock_.IsRnrBlockFatal());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, IsRnrBlockFatal_TimeoutZero_ReturnsFalse)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 0;
    EXPECT_FALSE(sock_.IsRnrBlockFatal());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, IsRnrBlockFatal_NoCold_ReturnsFalse)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 100;
    EXPECT_FALSE(sock_.IsRnrBlockFatal());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, IsRnrBlockFatal_StartZero_ReturnsFalse)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 100;
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_block_start_ns.store(0, std::memory_order_relaxed);
    EXPECT_FALSE(sock_.IsRnrBlockFatal());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, IsRnrBlockFatal_ElapsedLess_ReturnsFalse)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 1000000; /* 大超时，当前 elapsed 不足 */
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_block_start_ns.store(ubsocket_get_timeNs() - 1000000, std::memory_order_relaxed); /* 1ms 前 */
    EXPECT_FALSE(sock_.IsRnrBlockFatal());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, IsRnrBlockFatal_ElapsedExceeded_ReturnsTrue)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 1; /* 1ms 超时，当前 elapsed(≥1ms) 满足 */
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_block_start_ns.store(ubsocket_get_timeNs() - 2000000, std::memory_order_relaxed); /* 2ms 前 */
    EXPECT_TRUE(sock_.IsRnrBlockFatal());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, TryRnrBlockFatal_NotFatal_ReturnsFalse)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 0; /* 超时禁用 → 不 fatal */
    EXPECT_FALSE(sock_.TryRnrBlockFatal());
    EXPECT_TRUE(sock_.IsRnrBlocked());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, TryRnrBlockFatal_FatalSuccess_ClearsAndTimesOut)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 1;
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_block_start_ns.store(ubsocket_get_timeNs() - 2000000, std::memory_order_relaxed);
    EXPECT_TRUE(sock_.TryRnrBlockFatal());
    EXPECT_FALSE(sock_.IsRnrBlocked());
    EXPECT_EQ(cold->rnr_timeout_cnt.load(), 1u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CanNotifyWritable_RnrBlocked_ReturnsFalse)
{
    sock_.SetRnrBlocked(true);
    EXPECT_FALSE(sock_.CanNotifyWritable());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CanNotifyWritable_NotBlocked_ReturnsTrue)
{
    sock_.SetRnrBlocked(false);
    EXPECT_TRUE(sock_.CanNotifyWritable());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, FatalIfWriteBlocked_ReturnsTryRnrBlockFatal)
{
    sock_.SetRnrBlocked(true);
    GlobalSetting::UBS_RNR_FATAL_TIMEOUT_MS = 1;
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->rnr_block_start_ns.store(ubsocket_get_timeNs() - 2000000, std::memory_order_relaxed);
    EXPECT_TRUE(sock_.FatalIfWriteBlocked());
    GlobalMockObject::verify();
}

// ==================== AddQbuf / GetAndPopQbuf / FlushRxQueue ====================

TEST_F(UmqSocketTest, AddQbuf_NoRxQueue_FreesBuf)
{
    umq_buf_t qbuf {};
    MOCKER_CPP(::umq_buf_free).stubs();
    EXPECT_EQ(sock_.AddQbuf(&qbuf), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, AddQbuf_EnqueueOk_ReturnsOk)
{
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    umq_buf_t qbuf {};
    EXPECT_EQ(sock_.AddQbuf(&qbuf), UBS_OK);
    /* 弹出清空队列，避免 ~UmqBufferReceiveQueue 遍历残留的栈上 buffer */
    umq_buf_t *out = nullptr;
    EXPECT_EQ(sock_.GetAndPopQbuf(&out, 1), 1);
    EXPECT_EQ(out, &qbuf);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetAndPopQbuf_NoRxQueue_ReturnsMinusOne)
{
    umq_buf_t *buf = nullptr;
    EXPECT_EQ(sock_.GetAndPopQbuf(&buf, 1), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetAndPopQbuf_EmptyQueue_ReturnsZero)
{
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    umq_buf_t *buf = nullptr;
    EXPECT_EQ(sock_.GetAndPopQbuf(&buf, 1), 0);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetAndPopQbuf_ShutdownQueue_ReturnsError)
{
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    sock_.FlushRxQueue();
    umq_buf_t *buf = nullptr;
    errno = 0;
    EXPECT_EQ(sock_.GetAndPopQbuf(&buf, 1), UBS_ERROR);
    EXPECT_NE(errno, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, FlushRxQueue_NoRxQueue_Returns)
{
    sock_.FlushRxQueue();
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, FlushRxQueue_WithRxQueue_Shutdown)
{
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    sock_.FlushRxQueue();
    /* Shutdown 后入队应被拒绝 */
    umq_buf_t qbuf {};
    EXPECT_EQ(sock_.AddQbuf(&qbuf), UBS_ERROR);
    GlobalMockObject::verify();
}

// ==================== CheckDevAdd ====================

TEST_F(UmqSocketTest, CheckDevAdd_AlreadyRegistered_ReturnsOk)
{
    umq_eid_t eid{};
    eid.raw[0] = 0x01;
    EidRegistry::Instance().RegisterEid(eid);
    EXPECT_EQ(sock_.CheckDevAdd(eid), UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CheckDevAdd_DevAddOk_Registers)
{
    umq_eid_t eid{};
    eid.raw[0] = 0x02;
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockUmqDevAdd));
    EXPECT_EQ(sock_.CheckDevAdd(eid), UBS_OK);
    EXPECT_TRUE(EidRegistry::Instance().IsRegisteredEid(eid));
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CheckDevAdd_DevAddEexist_Registers)
{
    umq_eid_t eid{};
    eid.raw[0] = 0x03;
    g_devAddRet = -UMQ_ERR_EEXIST;
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockUmqDevAdd));
    EXPECT_EQ(sock_.CheckDevAdd(eid), UBS_OK);
    EXPECT_TRUE(EidRegistry::Instance().IsRegisteredEid(eid));
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CheckDevAdd_DevAddFails_ReturnsError)
{
    umq_eid_t eid{};
    eid.raw[0] = 0x04;
    g_devAddRet = -UMQ_ERR_ENOMEM;
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockUmqDevAdd));
    errno = EINVAL;
    EXPECT_EQ(sock_.CheckDevAdd(eid), UBS_UMQ_ERROR);
    EXPECT_FALSE(EidRegistry::Instance().IsRegisteredEid(eid));
    GlobalMockObject::verify();
}

// ==================== CLI 数据 ====================

TEST_F(UmqSocketTest, GetSocketCLIData_NoExt_ZeroEids)
{
    Statistics::CLISocketData data{};
    sock_.GetSocketCLIData(&data);
    bool allZero = true;
    for (size_t i = 0; i < UMQ_EID_SIZE; ++i) {
        if (data.localEid[i] != 0 || data.remoteEid[i] != 0) {
            allZero = false;
            break;
        }
    }
    EXPECT_TRUE(allZero);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketCLIData_WithEids_NonBondingCopies)
{
    SocketExt *ext = sock_.EnsureExt();
    ASSERT_NE(ext, nullptr);
    PeerEidTable::Entry *entry = PeerEidTable::Instance().Acquire({}, {}, {}, {});
    ASSERT_NE(entry, nullptr);
    entry->conn_eid.raw[0] = 0x11;
    entry->peer_eid.raw[0] = 0x22;
    ext->eids_entry.store(entry, std::memory_order_release);
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    Statistics::CLISocketData data{};
    sock_.GetSocketCLIData(&data);
    EXPECT_EQ(data.localEid[0], 0x11u);
    EXPECT_EQ(data.remoteEid[0], 0x22u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketCLIData_UseBondingEid)
{
    SocketExt *ext = sock_.EnsureExt();
    ASSERT_NE(ext, nullptr);
    PeerEidTable::Entry *entry = PeerEidTable::Instance().Acquire({}, {}, {}, {});
    ASSERT_NE(entry, nullptr);
    entry->bonding_eid.raw[0] = 0x33;
    entry->peer_bonding_eid.raw[0] = 0x44;
    ext->eids_entry.store(entry, std::memory_order_release);
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    Statistics::CLISocketData data{};
    sock_.GetSocketCLIData(&data);
    EXPECT_EQ(data.localEid[0], 0x33u);
    EXPECT_EQ(data.remoteEid[0], 0x44u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketFlowControlData_Success)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_stats_flow_control_get).stubs().will(invoke(&MockStatsFlowControlGet));
    Statistics::CLIFlowControlData data{};
    sock_.GetSocketFlowControlData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketFlowControlData_Fail_LogsWarn)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    g_statsGetRet = -1;
    MOCKER_CPP(::umq_stats_flow_control_get).stubs().will(invoke(&MockStatsFlowControlGet));
    Statistics::CLIFlowControlData data{};
    sock_.GetSocketFlowControlData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketQbufPoolData_Success)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_stats_qbuf_pool_get).stubs().will(invoke(&MockStatsQbufPoolGet));
    Statistics::CLIQbufPoolData data{};
    sock_.GetSocketQbufPoolData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketUmqInfoData_Success)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_info_get).stubs().will(invoke(&MockInfoGet));
    Statistics::CLIUmqInfoData data{};
    sock_.GetSocketUmqInfoData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketIoPacketData_Success)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_stats_io_get).stubs().will(invoke(&MockStatsIoGet));
    Statistics::CLIIoPacketData data{};
    sock_.GetSocketIoPacketData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketUmqPerfData_Success)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_stats_perf_get).stubs().will(invoke(&MockPerfStatsGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(invoke(&MockTpPerfStop));
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&MockTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(invoke(&MockTpPerfStart));
    Statistics::CLIUmqPerfData data{};
    sock_.GetSocketUmqPerfData(&data);
    GlobalMockObject::verify();
}

// ==================== OnEstablished ====================

TEST_F(UmqSocketTest, OnEstablished_NoHs_Returns)
{
    sock_.OnEstablished();
    GlobalMockObject::verify();
}

// ==================== ReinitTxOps / ReinitRxOps / GetUmqTxOps / GetUmqRxOps ====================

TEST_F(UmqSocketTest, ReinitTxOps_FreshEntry_ReturnsTxOps)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    UmqTxOps *txOps = sock_.ReinitTxOps();
    ASSERT_NE(txOps, nullptr);
    UmqRxOps *rxOps = sock_.ReinitRxOps();
    ASSERT_NE(rxOps, nullptr);
    EXPECT_EQ(sock_.GetUmqTxOps(), txOps);
    EXPECT_EQ(sock_.GetUmqRxOps(), rxOps);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, ReinitTxOps_OwnedByThis_Recreates)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    UmqTxOps *txOps1 = sock_.ReinitTxOps();
    ASSERT_NE(txOps1, nullptr);
    sock_.ReinitRxOps();
    UmqTxOps *txOps2 = sock_.ReinitTxOps();
    ASSERT_NE(txOps2, nullptr);
    /* 条目原地重建，地址不变 */
    EXPECT_EQ(txOps1, txOps2);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, ReinitTxOps_OwnedByOther_ReturnsNull)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    {
        UmqSocket other{TEST_FD};
        other.umq_handle_ = TEST_UMQ_HANDLE_2;
        other.ReinitTxOps();
        EXPECT_EQ(sock_.ReinitTxOps(), nullptr);
        /* other 在此析构（verify() 重置 mock 之前），否则会调用真实 umq_destroy */
    }
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, ReinitRxOps_NoEntry_ReturnsNull)
{
    EXPECT_EQ(sock_.ReinitRxOps(), nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, ReinitRxOps_OwnedByOther_ReturnsNull)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    {
        UmqSocket other{TEST_FD};
        other.umq_handle_ = TEST_UMQ_HANDLE_2;
        other.ReinitTxOps();
        EXPECT_EQ(sock_.ReinitRxOps(), nullptr);
    }
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetUmqTxOps_NoEntry_ReturnsNull)
{
    EXPECT_EQ(sock_.GetUmqTxOps(), nullptr);
    EXPECT_EQ(sock_.GetUmqRxOps(), nullptr);
    GlobalMockObject::verify();
}

// ==================== RegisterFcTxEvent / UnregisterFcTxEvent ====================

TEST_F(UmqSocketTest, RegisterFcTxEvent_FlowControlOff_ReturnsZero)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    EXPECT_EQ(sock_.RegisterFcTxEvent(), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RegisterFcTxEvent_InvalidHandle_ReturnsZero)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    sock_.umq_handle_ = UMQ_INVALID_HANDLE;
    EXPECT_EQ(sock_.RegisterFcTxEvent(), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RegisterFcTxEvent_InterruptFdFail_ReturnsError)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(-1));
    EXPECT_EQ(sock_.RegisterFcTxEvent(), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RegisterFcTxEvent_AddEpollEventFail_ReturnsError)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    using TxRunner = EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>;
    TxRunner &runner = TxRunner::Instance();
    MOCKER_CPP_VIRTUAL(runner, &TxRunner::AddEpollEvent).stubs().will(returnValue(-1));
    MOCKER_CPP_VIRTUAL(runner, &TxRunner::DelEpollEvent).stubs().will(returnValue(0));
    errno = EPERM;
    EXPECT_EQ(sock_.RegisterFcTxEvent(), UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RegisterFcTxEvent_Success_SetsFd)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(100));
    using TxRunner = EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>;
    TxRunner &runner = TxRunner::Instance();
    MOCKER_CPP_VIRTUAL(runner, &TxRunner::AddEpollEvent).stubs().will(returnValue(0));
    EXPECT_EQ(sock_.RegisterFcTxEvent(), 0u);
    UmqSocketCold *cold = sock_.GetCold();
    ASSERT_NE(cold, nullptr);
    EXPECT_EQ(cold->fc_event_fd, 100);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnregisterFcTxEvent_NoCold_Returns)
{
    sock_.UnregisterFcTxEvent();
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnregisterFcTxEvent_WithFd_DelEvent)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->fc_event_fd = 100;
    using TxRunner = EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>;
    TxRunner &runner = TxRunner::Instance();
    MOCKER_CPP_VIRTUAL(runner, &TxRunner::DelEpollEvent).stubs().will(returnValue(0));
    sock_.UnregisterFcTxEvent();
    EXPECT_EQ(cold->fc_event_fd, -1);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnregisterFcTxEvent_DelEventFail_LogsError)
{
    UmqSocketCold *cold = sock_.GetOrCreateCold();
    ASSERT_NE(cold, nullptr);
    cold->fc_event_fd = 100;
    using TxRunner = EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>;
    TxRunner &runner = TxRunner::Instance();
    MOCKER_CPP_VIRTUAL(runner, &TxRunner::DelEpollEvent).stubs().will(returnValue(-1));
    sock_.UnregisterFcTxEvent();
    EXPECT_EQ(cold->fc_event_fd, -1);
    GlobalMockObject::verify();
}

// ==================== RetireStep ====================

TEST_F(UmqSocketTest, RetireStep_InvalidHandle_SetsRetiringTrue)
{
    sock_.umq_handle_ = UMQ_INVALID_HANDLE;
    EXPECT_TRUE(sock_.RetireStep());
    EXPECT_TRUE(sock_.IsRetiring());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RetireStep_NoOps_SetsRetiringTrue)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    EXPECT_TRUE(sock_.RetireStep());
    EXPECT_TRUE(sock_.IsRetiring());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RetireStep_FirstCall_NotBound_NoDrain)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    InstallDataPlane();
    sock_.umq_is_bind_remote_ = false;
    EXPECT_TRUE(sock_.RetireStep());
    EXPECT_TRUE(sock_.IsRetiring());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RetireStep_Bound_Pool_NoDrain)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    InstallDataPlane();
    sock_.umq_is_bind_remote_ = true;
    UmqSetting::UMQ_TP_TYPE = POOL;
    MOCKER_CPP(::umq_ack_interrupt).stubs();
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    EXPECT_TRUE(sock_.RetireStep());
    EXPECT_FALSE(sock_.umq_is_bind_remote_);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RetireStep_Bound_Single_DrainSuccess)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    InstallDataPlane();
    sock_.umq_is_bind_remote_ = true;
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    DataTxOps *txOps = sock_.GetUmqTxOps();
    ASSERT_NE(txOps, nullptr);
    txOps->tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH, std::memory_order_relaxed);
    MOCKER_CPP(::umq_ack_interrupt).stubs();
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    MOCKER_CPP(&DataTxOps::ForceDrainTx).stubs();
    EXPECT_TRUE(sock_.RetireStep());
    EXPECT_TRUE(sock_.retire_drained_);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RetireStep_Bound_Single_DrainNotDone)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    InstallDataPlane();
    sock_.umq_is_bind_remote_ = true;
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    DataTxOps *txOps = sock_.GetUmqTxOps();
    ASSERT_NE(txOps, nullptr);
    txOps->tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH - 1, std::memory_order_relaxed);
    MOCKER_CPP(::umq_ack_interrupt).stubs();
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    MOCKER_CPP(&DataTxOps::ForceDrainTx).stubs();
    EXPECT_FALSE(sock_.RetireStep());
    EXPECT_FALSE(sock_.retire_drained_);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RetireStep_SecondCall_AlreadyRetiring_SkipsFirstBlock)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    InstallDataPlane();
    sock_.umq_is_bind_remote_ = true;
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    sock_.retiring_.store(true, std::memory_order_release);
    DataTxOps *txOps = sock_.GetUmqTxOps();
    ASSERT_NE(txOps, nullptr);
    txOps->tx_queue_avail_num_.store(GlobalSetting::UBS_TX_DEPTH, std::memory_order_relaxed);
    MOCKER_CPP(&DataTxOps::ForceDrainTx).stubs();
    EXPECT_TRUE(sock_.RetireStep());
    EXPECT_TRUE(sock_.retire_drained_);
    GlobalMockObject::verify();
}

// ==================== UnInitialize ====================

TEST_F(UmqSocketTest, UnInitialize_InvalidHandle_Returns)
{
    sock_.umq_handle_ = UMQ_INVALID_HANDLE;
    sock_.UnInitialize();
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnInitialize_NotRetiring_FullTeardown)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    sock_.umq_is_bind_remote_ = true;
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    InstallDataPlane();
    MOCKER_CPP(::umq_ack_interrupt).stubs();
    MOCKER_CPP(::umq_unbind).stubs().will(invoke(&MockUmqUnbind));
    MOCKER_CPP(&DataTxOps::FlushTx).stubs();
    MOCKER_CPP(&DataRxOps::FlushRx).stubs();
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    sock_.UnInitialize();
    EXPECT_EQ(sock_.umq_handle_, UMQ_INVALID_HANDLE);
    EXPECT_EQ(sock_.rxQueue, nullptr);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnInitialize_RetiringNotDrained_SkipsFlushStillDestroys)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    sock_.umq_is_bind_remote_ = true;
    sock_.retiring_.store(true, std::memory_order_release);
    sock_.retire_drained_ = false;
    InstallDataPlane();
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    sock_.UnInitialize();
    EXPECT_EQ(sock_.umq_handle_, UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, UnInitialize_RetiringDrained_OnlyDestroy)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    sock_.umq_is_bind_remote_ = true;
    sock_.retiring_.store(true, std::memory_order_release);
    sock_.retire_drained_ = true;
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    sock_.UnInitialize();
    EXPECT_EQ(sock_.umq_handle_, UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

// ==================== 覆盖率补强：CLI stats 失败路径 ====================

TEST_F(UmqSocketTest, GetSocketQbufPoolData_Fail_LogsWarn)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    g_statsGetRet = -1;
    MOCKER_CPP(::umq_stats_qbuf_pool_get).stubs().will(invoke(&MockStatsQbufPoolGet));
    Statistics::CLIQbufPoolData data{};
    sock_.GetSocketQbufPoolData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketUmqInfoData_Fail_LogsWarn)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    g_statsGetRet = -1;
    MOCKER_CPP(::umq_info_get).stubs().will(invoke(&MockInfoGet));
    Statistics::CLIUmqInfoData data{};
    sock_.GetSocketUmqInfoData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketIoPacketData_Fail_LogsWarn)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    g_statsGetRet = -1;
    MOCKER_CPP(::umq_stats_io_get).stubs().will(invoke(&MockStatsIoGet));
    Statistics::CLIIoPacketData data{};
    sock_.GetSocketIoPacketData(&data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, GetSocketUmqPerfData_Fail_LogsWarn)
{
    sock_.umq_handle_ = TEST_UMQ_HANDLE;
    g_statsGetRet = -1;
    MOCKER_CPP(::umq_stats_perf_get).stubs().will(invoke(&MockPerfStatsGet));
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(invoke(&MockTpPerfStop));
    MOCKER_CPP(::umq_stats_tp_perf_info_get).stubs().will(invoke(&MockTpPerfInfoGet));
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(invoke(&MockTpPerfStart));
    Statistics::CLIUmqPerfData data{};
    sock_.GetSocketUmqPerfData(&data);
    GlobalMockObject::verify();
}

// ==================== 覆盖率补强：CreateLocalUmq 剩余分支 ====================

TEST_F(UmqSocketTest, CreateLocalUmq_RegisterFcFails_ReturnsError)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    g_interruptFd = -1;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockUmqInterruptFdGet));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));
    umq_eid_t connEid{};
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType),
              static_cast<int32_t>(UBS_ERROR | UBS_DEGRADABLE_MASK));
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, CreateLocalUmq_BondingNoDevName_UsesConnEid)
{
    UmqSetting::UMQ_IS_BONDING = true;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    umq_eid_t connEid{};
    connEid.raw[0] = 0x55;
    umq_used_ports_t usedPorts{};
    umq_topo_type_t topoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    EXPECT_EQ(sock_.CreateLocalUmq(&connEid, usedPorts, topoType), UBS_OK);
    EXPECT_EQ(sock_.umq_handle_, TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

// ==================== 覆盖率补强：share-JFR 加锁入队 ====================

TEST_F(UmqSocketTest, AddQbuf_ShareJfrEnabled_Enqueues)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    umq_buf_t qbuf {};
    EXPECT_EQ(sock_.AddQbuf(&qbuf), UBS_OK);
    /* 弹出清空队列，避免 ~UmqBufferReceiveQueue 遍历残留的栈上 buffer */
    umq_buf_t *out = nullptr;
    EXPECT_EQ(sock_.GetAndPopQbuf(&out, 1), 1);
    EXPECT_EQ(out, &qbuf);
    GlobalMockObject::verify();
}

// ==================== 覆盖率补强：RxQueueEmpty ====================

TEST_F(UmqSocketTest, RxQueueEmpty_NoQueue_ReturnsTrue)
{
    EXPECT_TRUE(sock_.RxQueueEmpty());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RxQueueEmpty_EmptyQueue_ReturnsTrue)
{
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    EXPECT_TRUE(sock_.RxQueueEmpty());
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, RxQueueEmpty_NonEmptyQueue_ReturnsFalse)
{
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    umq_buf_t qbuf {};
    EXPECT_EQ(sock_.AddQbuf(&qbuf), UBS_OK);
    EXPECT_FALSE(sock_.RxQueueEmpty());
    /* 弹出清空队列 */
    umq_buf_t *out = nullptr;
    EXPECT_EQ(sock_.GetAndPopQbuf(&out, 1), 1);
    GlobalMockObject::verify();
}

// ==================== 覆盖率补强：SetAddedEpollFd ====================

TEST_F(UmqSocketTest, SetAddedEpollFd_NullFd_NoQueue_NoCrash)
{
    epoll_data_t data{};
    sock_.SetAddedEpollFd(nullptr, data);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketTest, SetAddedEpollFd_PendingQueue_NotifiesDropped)
{
    sock_.rxQueue.reset(new UmqBufferReceiveQueue());
    ASSERT_NE(sock_.rxQueue, nullptr);
    umq_buf_t qbuf {};
    EXPECT_EQ(sock_.AddQbuf(&qbuf), UBS_OK);
    epoll_data_t data{};
    /* fd 为 nullptr：NotifyReadable 内部 ep 为空，仅告警不崩溃 */
    sock_.SetAddedEpollFd(nullptr, data);
    /* 弹出清空队列 */
    umq_buf_t *out = nullptr;
    EXPECT_EQ(sock_.GetAndPopQbuf(&out, 1), 1);
    GlobalMockObject::verify();
}
