/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_transport_pool.h"

#include <sys/timerfd.h>

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_tx_cqe_poller.h"
#include "umq_setting.h"
#include "under_api/dl_libc_api.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const uint64_t TEST_UMQ_HANDLE = 0x12345678;
static const uint64_t TEST_OTHER_UMQ_HANDLE = 0x87654321;
static const int TEST_FD = 42;
static const int TEST_TIMER_FD = 43;
static const uint32_t TEST_TP_IDX = 3;
static const uint32_t TEST_TP_IDX_OLD = 100;
static const int TEST_POOL_SIZE = 3;
static const uint32_t TEST_CHIP_ID = 1;
static const uint32_t TEST_OTHER_CHIP_ID = 2;
static const uint32_t TEST_DIE_ID = 0;
static const uint32_t TEST_PORT_ID = 10;

/* ---------- mock 共享状态(g_ 前缀) ---------- */

static int g_closeCnt = 0;
static int g_routeListRet = 0;
static uint32_t g_mockRouteNum = 0;
static umq_route_t g_mockRoutes[UMQ_MAX_ROUTES]{};
static int g_tpCreateCnt = 0;
static bool g_failNextTpCreate = false;
static uint32_t g_lastCreateFlag = 0;
static uint8_t g_lastUsedPortsNum = 0;
static std::vector<uint32_t> g_lastPortValues;
static int g_fdGetRet = TEST_FD;
static int g_modifyCnt = 0;
static bool g_failNextModify = false;
static int g_destroyCnt = 0;
static bool g_failNextDestroy = false;
static int g_eventFdGetCnt = 0;

/* ---------- mock 静态函数(CamelCase,签名与真实 API 完全一致) ---------- */

static int FakeClose(int fd)
{
    (void)fd;
    ++g_closeCnt;
    return 0;
}

static int MockGetRouteList(const umq_route_key_t *route_key, umq_trans_mode_t trans_mode, umq_route_list_t *route_list)
{
    (void)route_key;
    (void)trans_mode;
    route_list->route_num = g_mockRouteNum;
    for (uint32_t i = 0; i < g_mockRouteNum; ++i) {
        route_list->routes[i] = g_mockRoutes[i];
    }
    return g_routeListRet;
}

static uint32_t MockTpResourceCreate(uint64_t umqh, umq_tp_resource_create_option_t *option)
{
    (void)umqh;
    ++g_tpCreateCnt;
    g_lastCreateFlag = option->create_flag;
    g_lastUsedPortsNum = option->used_ports.num;
    g_lastPortValues.clear();
    if (option->used_ports.port != nullptr) {
        for (uint8_t i = 0; i < option->used_ports.num; ++i) {
            g_lastPortValues.push_back(option->used_ports.port[i].value);
        }
    }
    if (g_failNextTpCreate) {
        g_failNextTpCreate = false;
        return UINT32_MAX;
    }
    return static_cast<uint32_t>(g_tpCreateCnt);
}

static int MockInterruptFdGet(uint64_t umqh, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    return g_fdGetRet;
}

static int MockResourceModify(uint64_t umqh, uint32_t tp_handle_idx)
{
    (void)umqh;
    (void)tp_handle_idx;
    ++g_modifyCnt;
    if (g_failNextModify) {
        g_failNextModify = false;
        return -1;
    }
    return 0;
}

static int MockResourceDestroy(uint64_t umqh, uint32_t tp_handle_idx)
{
    (void)umqh;
    (void)tp_handle_idx;
    ++g_destroyCnt;
    if (g_failNextDestroy) {
        g_failNextDestroy = false;
        return -1;
    }
    return 0;
}

static int MockEventFdGet(uint64_t umqh)
{
    (void)umqh;
    ++g_eventFdGetCnt;
    return TEST_FD;
}

/* ---------- 工具函数 ---------- */

static umq_port_id_t MakePort(uint32_t chip, uint32_t die, uint32_t port)
{
    umq_port_id_t p{};
    p.bs.chip_id = chip;
    p.bs.die_id = die;
    p.bs.port_idx = port;
    return p;
}

static void SetRouteList(uint32_t routeNum, const umq_route_t *routes)
{
    g_mockRouteNum = routeNum;
    for (uint32_t i = 0; i < routeNum; ++i) {
        g_mockRoutes[i] = routes[i];
    }
    /* 直接调用 CreateOneTp 读成员 route_list_tp_(非 mock 数据),同步写入保证两种路径一致 */
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.route_list_tp_.route_num = routeNum;
    for (uint32_t i = 0; i < routeNum; ++i) {
        pool.route_list_tp_.routes[i] = routes[i];
    }
}

static void SetSingleRoute(umq_port_id_t srcPort)
{
    umq_route_t route{};
    route.src_port = srcPort;
    SetRouteList(1, &route);
}

static void ResetPoolState()
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool.clear();
    pool.rr_num_ = 0;
    pool.aff_rr_num_ = 0;
    pool.route_list_tp_.route_num = 0;
    TxCqePoller::Instance().orphan_main_umqs_.clear();
}

class UmqTransportPoolTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = false;
        GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        UmqSetting::UMQ_UB_TP_MODE = UMQ_TM_RM;
        UmqSetting::UMQ_TP_TYPE = POOL;
        UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
        UmqSetting::UMQ_TP_POOL_SIZE = static_cast<uint32_t>(TEST_POOL_SIZE);
        UmqSetting::UMQ_DEV_SCHEDULE_POLICY = CPU_AFFINITY_PRIORITY;
        UmqSetting::UMQ_ALL_SOCKET_IDS = {0};
        UmqSetting::UMQ_PROCESS_SOCKET_ID = 0;
        UmqSetting::UMQ_LOCAL_EID = {};
        g_closeCnt = 0;
        g_routeListRet = 0;
        g_mockRouteNum = 0;
        g_tpCreateCnt = 0;
        g_failNextTpCreate = false;
        g_lastCreateFlag = 0;
        g_lastUsedPortsNum = 0;
        g_lastPortValues.clear();
        g_fdGetRet = TEST_FD;
        g_modifyCnt = 0;
        g_failNextModify = false;
        g_destroyCnt = 0;
        g_failNextDestroy = false;
        g_eventFdGetCnt = 0;
        LibcApi::close_ptr = FakeClose;
        ResetPoolState();
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        ResetPoolState();
        LibcApi::close_ptr = nullptr;
        errno = 0;
    }
};

/* ==================== WarmUp ==================== */

using TpTxRunner = EpollRunner<EpollRunnerType::TRANSPORT_POOL_TX_RUNNER>;
using TpEventRunner = EpollRunner<EpollRunnerType::TRANSPORT_POOL_EVENT_RUNNER>;

static void MockTpTxRunnerStart(int startRet)
{
    MOCKER_CPP_VIRTUAL(TpTxRunner::Instance(), &TpTxRunner::Start).stubs().will(returnValue(startRet));
}

static void MockTpEventRunner(int startRet, int addEpollRet)
{
    MOCKER_CPP_VIRTUAL(TpEventRunner::Instance(), &TpEventRunner::Start).stubs().will(returnValue(startRet));
    MOCKER_CPP_VIRTUAL(TpEventRunner::Instance(), &TpEventRunner::AddEpollEvent).stubs().will(returnValue(addEpollRet));
}

TEST_F(UmqTransportPoolTest, WarmUp_NotRmMode_ReturnsOk)
{
    MockTpTxRunnerStart(0);
    UmqSetting::UMQ_UB_TP_MODE = UMQ_TM_RC;
    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, WarmUp_NotPoolType_ReturnsOk)
{
    MockTpTxRunnerStart(0);
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, WarmUp_TxRunnerStartFail_ReturnsStartResult)
{
    MockTpTxRunnerStart(-1);
    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(static_cast<int32_t>(ret), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, WarmUp_AlreadyWarmedUp_ReturnsOk)
{
    MockTpTxRunnerStart(0);
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][TEST_TP_IDX] = {TEST_FD};
    ock::ubs::Result ret = pool.WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    /* 已预热去重:不再重新创建 pool */
    EXPECT_EQ(pool.PoolSize(TEST_UMQ_HANDLE), 1u);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, WarmUp_GetRouteListFail_ReturnsError)
{
    MockTpTxRunnerStart(0);
    /* 必须挂载 mock: 否则走 adapter 后端真实 ::umq_get_route_list(未初始化句柄打真实库,断言依赖库副作用) */
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    g_routeListRet = -1;
    errno = EINVAL;
    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    /* GetRouteList 失败路径经 Convert(CONNECT) 保留 savedErrno */
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

static void MockWarmUpSuccessPath(bool unifiedEnabled)
{
    MockTpTxRunnerStart(0);
    GlobalSetting::UBS_TX_UNIFIED_POLL_ENABLED = unifiedEnabled;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP(::umq_transport_pool_eventfd_get).stubs().will(invoke(&MockEventFdGet));
    if (!unifiedEnabled) {
        MOCKER_CPP(::timerfd_create).stubs().will(returnValue(TEST_TIMER_FD));
        MOCKER_CPP(::timerfd_settime).stubs().will(returnValue(0));
        MOCKER_CPP_VIRTUAL(TpTxRunner::Instance(), &TpTxRunner::AddEpollEvent).stubs().will(returnValue(0));
    }
    MockTpEventRunner(0, 0);
    SetSingleRoute(MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID));
}

TEST_F(UmqTransportPoolTest, WarmUp_TimerPath_Success_ReturnsOk)
{
    MockWarmUpSuccessPath(false);
    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), static_cast<size_t>(TEST_POOL_SIZE));
    /* timer 路径: timer_destroyer/event_cleaner 均 Deactivate,不触发 close */
    EXPECT_EQ(g_closeCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, WarmUp_UnifiedEnabled_RegistersOrphanSweep_ReturnsOk)
{
    MockWarmUpSuccessPath(true);
    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    const std::vector<uint64_t> &orphans = TxCqePoller::Instance().orphan_main_umqs_;
    EXPECT_NE(std::find(orphans.begin(), orphans.end(), TEST_UMQ_HANDLE), orphans.end());
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, WarmUp_AddTimerEventFail_CleansPool_ReturnsError)
{
    MockTpTxRunnerStart(0);
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP(::umq_transport_pool_resource_modify).stubs().will(invoke(&MockResourceModify));
    MOCKER_CPP(::umq_transport_pool_resource_destroy).stubs().will(invoke(&MockResourceDestroy));
    MOCKER_CPP(::timerfd_create).stubs().will(returnValue(-1));
    SetSingleRoute(MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID));

    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    /* 失败后 Clean(): 池被清空,且 modify/destroy 真实执行 */
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), 0u);
    EXPECT_EQ(g_modifyCnt, TEST_POOL_SIZE);
    EXPECT_EQ(g_destroyCnt, TEST_POOL_SIZE);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, WarmUp_AddTransportEpollEventFail_CleansPool_ReturnsError)
{
    MockTpTxRunnerStart(0);
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP(::umq_transport_pool_resource_modify).stubs().will(invoke(&MockResourceModify));
    MOCKER_CPP(::umq_transport_pool_resource_destroy).stubs().will(invoke(&MockResourceDestroy));
    MOCKER_CPP(::timerfd_create).stubs().will(returnValue(TEST_TIMER_FD));
    MOCKER_CPP(::timerfd_settime).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(TpTxRunner::Instance(), &TpTxRunner::AddEpollEvent).stubs().will(returnValue(0));
    /* EVENT runner AddEpollEvent 失败 → AddTransportEpollEvent 失败 → Clean() */
    MOCKER_CPP_VIRTUAL(TpEventRunner::Instance(), &TpEventRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(TpEventRunner::Instance(), &TpEventRunner::AddEpollEvent).stubs().will(returnValue(-1));
    MOCKER_CPP(::umq_transport_pool_eventfd_get).stubs().will(invoke(&MockEventFdGet));
    SetSingleRoute(MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID));

    ock::ubs::Result ret = UmqTransportPool::Instance().WarmUp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), 0u);
    EXPECT_EQ(g_modifyCnt, TEST_POOL_SIZE);
    EXPECT_EQ(g_destroyCnt, TEST_POOL_SIZE);
    GlobalMockObject::verify();
}

/* ==================== CreatePool ==================== */

TEST_F(UmqTransportPoolTest, CreatePool_AllTpCreateSuccess_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));

    ock::ubs::Result ret = UmqTransportPool::Instance().CreatePool(TEST_UMQ_HANDLE, TEST_POOL_SIZE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_tpCreateCnt, TEST_POOL_SIZE);
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), static_cast<size_t>(TEST_POOL_SIZE));
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreatePool_PartialTpCreateFail_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    g_failNextTpCreate = true;

    ock::ubs::Result ret = UmqTransportPool::Instance().CreatePool(TEST_UMQ_HANDLE, TEST_POOL_SIZE);
    EXPECT_EQ(ret, UBS_OK);
    /* 单个失败只记日志,循环继续,成功计数为 pool_size - 1 */
    EXPECT_EQ(g_tpCreateCnt, TEST_POOL_SIZE);
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), static_cast<size_t>(TEST_POOL_SIZE - 1));
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreatePool_ZeroSize_ReturnsOk)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    ock::ubs::Result ret = UmqTransportPool::Instance().CreatePool(TEST_UMQ_HANDLE, 0);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), 0u);
    GlobalMockObject::verify();
}

/* ==================== CreateOneTp ==================== */

TEST_F(UmqTransportPoolTest, CreateOneTp_NotBondingBackup_CreatesWithoutPorts)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));

    ock::ubs::Result ret = UmqTransportPool::Instance().CreateOneTp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_lastCreateFlag & UMQ_TP_CREATE_FLAG_USED_PORTS, 0u);
    EXPECT_EQ(g_lastUsedPortsNum, 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreateOneTp_Affinity_NoRoute_ReturnsError)
{
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    /* route_num=0 → chipId_list 空 → GetTargetChipId 越界 → UINT32_MAX → 回退 rr 分支 → all_ports 空 */
    ock::ubs::Result ret = UmqTransportPool::Instance().CreateOneTp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_tpCreateCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreateOneTp_Affinity_RotatesUsedPorts)
{
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    umq_route_t routes[3]{};
    routes[0].src_port = MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID);
    routes[1].src_port = MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID + 1);
    routes[2].src_port = MakePort(TEST_OTHER_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID + 2);
    SetRouteList(3, routes);

    UmqTransportPool &pool = UmqTransportPool::Instance();
    EXPECT_EQ(pool.CreateOneTp(TEST_UMQ_HANDLE), UBS_OK);
    const std::vector<uint32_t> firstRound = g_lastPortValues;
    EXPECT_EQ(firstRound.size(), 3u);
    /* aff_rr_num_ 轮转: 第二次创建 aff 区起始位置后移 */
    EXPECT_EQ(pool.CreateOneTp(TEST_UMQ_HANDLE), UBS_OK);
    const std::vector<uint32_t> secondRound = g_lastPortValues;
    EXPECT_EQ(secondRound.size(), 3u);
    EXPECT_NE(firstRound, secondRound);
    /* 首元素即主路,两次轮转后首元素互异 */
    EXPECT_EQ(firstRound[0], routes[0].src_port.value);
    EXPECT_EQ(secondRound[0], routes[1].src_port.value);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreateOneTp_Affinity_PortDedup_PreservesOrder)
{
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    umq_route_t routes[3]{};
    routes[0].src_port = MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID);
    routes[1].src_port = MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID); /* 重复 port */
    routes[2].src_port = MakePort(TEST_OTHER_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID + 1);
    SetRouteList(3, routes);

    UmqTransportPool &pool = UmqTransportPool::Instance();
    EXPECT_EQ(pool.CreateOneTp(TEST_UMQ_HANDLE), UBS_OK);
    /* 去重保序: 重复 port 只保留首次出现,顺序不变 */
    EXPECT_EQ(g_lastPortValues.size(), 2u);
    EXPECT_EQ(g_lastPortValues[0], routes[0].src_port.value);
    EXPECT_EQ(g_lastPortValues[1], routes[2].src_port.value);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreateOneTp_RoundRobin_EmptyRoute_ReturnsError)
{
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = ROUND_ROBIN;
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    /* route_num=0 → all_ports 空 → 错误 */
    ock::ubs::Result ret = UmqTransportPool::Instance().CreateOneTp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_tpCreateCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreateOneTp_RoundRobin_RotatesUsedPorts)
{
    UmqSetting::UMQ_DEV_SCHEDULE_POLICY = ROUND_ROBIN;
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    umq_route_t routes[3]{};
    routes[0].src_port = MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID);
    routes[1].src_port = MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID + 1);
    routes[2].src_port = MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID + 2);
    SetRouteList(3, routes);

    UmqTransportPool &pool = UmqTransportPool::Instance();
    EXPECT_EQ(pool.CreateOneTp(TEST_UMQ_HANDLE), UBS_OK);
    const std::vector<uint32_t> firstRound = g_lastPortValues;
    EXPECT_EQ(firstRound.size(), 3u);
    EXPECT_EQ(pool.CreateOneTp(TEST_UMQ_HANDLE), UBS_OK);
    const std::vector<uint32_t> secondRound = g_lastPortValues;
    EXPECT_EQ(secondRound.size(), 3u);
    /* rr_num_ 轮转: 主路依次后移 */
    EXPECT_EQ(firstRound[0], routes[0].src_port.value);
    EXPECT_EQ(secondRound[0], routes[1].src_port.value);
    EXPECT_NE(firstRound, secondRound);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreateOneTp_TpResourceCreateFail_ReturnsError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    g_failNextTpCreate = true;

    ock::ubs::Result ret = UmqTransportPool::Instance().CreateOneTp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    /* tp_idx==UINT32_MAX → 错误,不查询 interrupt fd */
    EXPECT_EQ(UmqTransportPool::Instance().PoolSize(TEST_UMQ_HANDLE), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, CreateOneTp_InterruptFdGetFail_ReturnsError)
{
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    g_fdGetRet = -1;

    ock::ubs::Result ret = UmqTransportPool::Instance().CreateOneTp(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    /* fd<UBS_OK 失败时 tp 条目已由 operator[] 插入(空 fd_vec) */
    UmqTransportPool &pool = UmqTransportPool::Instance();
    EXPECT_EQ(pool.umq_tp_pool[TEST_UMQ_HANDLE].size(), 1u);
    GlobalMockObject::verify();
}

/* ==================== RebuildTp ==================== */

TEST_F(UmqTransportPoolTest, RebuildTp_EmptyPool_ReturnsError)
{
    ock::ubs::Result ret = UmqTransportPool::Instance().RebuildTp(TEST_UMQ_HANDLE, TEST_TP_IDX);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, RebuildTp_UmqNotExist_ReturnsError)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_OTHER_UMQ_HANDLE][TEST_TP_IDX] = {TEST_FD};
    ock::ubs::Result ret = pool.RebuildTp(TEST_UMQ_HANDLE, TEST_TP_IDX);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, RebuildTp_TpIdxNotExist_ReturnsError)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][TEST_TP_IDX] = {TEST_FD};
    ock::ubs::Result ret = pool.RebuildTp(TEST_UMQ_HANDLE, TEST_TP_IDX + 1);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

static void MockRebuildTpSharedPath()
{
    MOCKER_CPP(::umq_transport_pool_resource_modify).stubs().will(invoke(&MockResourceModify));
    MOCKER_CPP(::umq_transport_pool_resource_destroy).stubs().will(invoke(&MockResourceDestroy));
    MOCKER_CPP_VIRTUAL(TpTxRunner::Instance(), &TpTxRunner::DelEpollEvent).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_transport_pool_resource_create).stubs().will(invoke(&MockTpResourceCreate));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
}

TEST_F(UmqTransportPoolTest, RebuildTp_ModifyFail_ReturnsError)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][TEST_TP_IDX] = {TEST_FD};
    MockRebuildTpSharedPath();
    g_failNextModify = true;

    ock::ubs::Result ret = pool.RebuildTp(TEST_UMQ_HANDLE, TEST_TP_IDX);
    EXPECT_EQ(ret, UBS_ERROR);
    /* modify 失败 → 原条目保留 */
    EXPECT_EQ(pool.umq_tp_pool[TEST_UMQ_HANDLE].count(TEST_TP_IDX), 1u);
    EXPECT_EQ(g_destroyCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, RebuildTp_DestroyFail_ReturnsError)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][TEST_TP_IDX] = {TEST_FD};
    MockRebuildTpSharedPath();
    g_failNextDestroy = true;

    ock::ubs::Result ret = pool.RebuildTp(TEST_UMQ_HANDLE, TEST_TP_IDX);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_modifyCnt, 1);
    /* destroy 失败 → 原条目保留 */
    EXPECT_EQ(pool.umq_tp_pool[TEST_UMQ_HANDLE].count(TEST_TP_IDX), 1u);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, RebuildTp_Success_RecreatesTp)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][TEST_TP_IDX_OLD] = {TEST_FD};
    /* CreateOneTp 重建走真实 route 选择,SetSingleRoute 同步写成员 route_list_tp_ */
    SetSingleRoute(MakePort(TEST_CHIP_ID, TEST_DIE_ID, TEST_PORT_ID));
    MockRebuildTpSharedPath();

    ock::ubs::Result ret = pool.RebuildTp(TEST_UMQ_HANDLE, TEST_TP_IDX_OLD);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_modifyCnt, 1);
    EXPECT_EQ(g_destroyCnt, 1);
    /* 旧条目删除,新条目由 CreateOneTp 重建 */
    EXPECT_EQ(pool.umq_tp_pool[TEST_UMQ_HANDLE].count(TEST_TP_IDX_OLD), 0u);
    EXPECT_EQ(pool.PoolSize(TEST_UMQ_HANDLE), 1u);
    GlobalMockObject::verify();
}

/* ==================== Clean ==================== */

TEST_F(UmqTransportPoolTest, Clean_EmptyPool_ReturnsOk)
{
    ock::ubs::Result ret = UmqTransportPool::Instance().Clean();
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_modifyCnt, 0);
    EXPECT_EQ(g_destroyCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, Clean_ModifyFail_ContinuesNextTp)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][1] = {TEST_FD};
    pool.umq_tp_pool[TEST_UMQ_HANDLE][2] = {TEST_FD};
    MOCKER_CPP(::umq_transport_pool_resource_modify).stubs().will(invoke(&MockResourceModify));
    MOCKER_CPP(::umq_transport_pool_resource_destroy).stubs().will(invoke(&MockResourceDestroy));
    g_failNextModify = true;

    ock::ubs::Result ret = pool.Clean();
    EXPECT_EQ(ret, UBS_OK);
    /* modify<0 的条目跳过 destroy 但循环继续: 两个条目都尝试 modify,destroy 只 1 次 */
    EXPECT_EQ(g_modifyCnt, 2);
    EXPECT_EQ(g_destroyCnt, 1);
    EXPECT_TRUE(pool.umq_tp_pool.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, Clean_DestroyFail_LogsAndContinues)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][1] = {TEST_FD};
    pool.umq_tp_pool[TEST_UMQ_HANDLE][2] = {TEST_FD};
    MOCKER_CPP(::umq_transport_pool_resource_modify).stubs().will(invoke(&MockResourceModify));
    MOCKER_CPP(::umq_transport_pool_resource_destroy).stubs().will(invoke(&MockResourceDestroy));
    g_failNextDestroy = true;

    ock::ubs::Result ret = pool.Clean();
    EXPECT_EQ(ret, UBS_OK);
    /* destroy<0 只记日志,后续条目继续处理 */
    EXPECT_EQ(g_modifyCnt, 2);
    EXPECT_EQ(g_destroyCnt, 2);
    EXPECT_TRUE(pool.umq_tp_pool.empty());
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, Clean_Success_ClearsPool)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][1] = {TEST_FD};
    pool.umq_tp_pool[TEST_UMQ_HANDLE][2] = {TEST_FD};
    MOCKER_CPP(::umq_transport_pool_resource_modify).stubs().will(invoke(&MockResourceModify));
    MOCKER_CPP(::umq_transport_pool_resource_destroy).stubs().will(invoke(&MockResourceDestroy));

    ock::ubs::Result ret = pool.Clean();
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_modifyCnt, 2);
    EXPECT_EQ(g_destroyCnt, 2);
    EXPECT_TRUE(pool.umq_tp_pool.empty());
    GlobalMockObject::verify();
}

/* ==================== PoolSize ==================== */

TEST_F(UmqTransportPoolTest, PoolSize_NoSuchUmq_ReturnsZero)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_OTHER_UMQ_HANDLE][1] = {TEST_FD};
    EXPECT_EQ(pool.PoolSize(TEST_UMQ_HANDLE), 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, PoolSize_ExistingUmq_ReturnsTpCount)
{
    UmqTransportPool &pool = UmqTransportPool::Instance();
    pool.umq_tp_pool[TEST_UMQ_HANDLE][1] = {TEST_FD};
    pool.umq_tp_pool[TEST_UMQ_HANDLE][2] = {TEST_FD};
    EXPECT_EQ(pool.PoolSize(TEST_UMQ_HANDLE), 2u);
    GlobalMockObject::verify();
}

/* ==================== AddTimerEvent ==================== */

TEST_F(UmqTransportPoolTest, AddTimerEvent_TimerfdCreateFail_ReturnsError)
{
    MOCKER_CPP(::timerfd_create).stubs().will(returnValue(-1));
    ock::ubs::Result ret = UmqTransportPool::Instance().AddTimerEvent(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    /* timerfd_create 失败在 ScopeExit 创建之前,close 不被调 */
    EXPECT_EQ(g_closeCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, AddTimerEvent_TimerfdSetTimeFail_ReturnsMinusOne)
{
    MOCKER_CPP(::timerfd_create).stubs().will(returnValue(TEST_TIMER_FD));
    MOCKER_CPP(::timerfd_settime).stubs().will(returnValue(-1));
    ock::ubs::Result ret = UmqTransportPool::Instance().AddTimerEvent(TEST_UMQ_HANDLE);
    EXPECT_EQ(static_cast<int32_t>(ret), -1);
    /* ScopeExit 清理: close(timer_fd) 执行一次 */
    EXPECT_EQ(g_closeCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, AddTimerEvent_AddEpollEventFail_ReturnsError)
{
    MOCKER_CPP(::timerfd_create).stubs().will(returnValue(TEST_TIMER_FD));
    MOCKER_CPP(::timerfd_settime).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(TpTxRunner::Instance(), &TpTxRunner::AddEpollEvent).stubs().will(returnValue(-1));
    ock::ubs::Result ret = UmqTransportPool::Instance().AddTimerEvent(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    /* 双重清理: close(timer_fd) + delete TxEpollEvent */
    EXPECT_EQ(g_closeCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, AddTimerEvent_Success_ReturnsOk)
{
    MOCKER_CPP(::timerfd_create).stubs().will(returnValue(TEST_TIMER_FD));
    MOCKER_CPP(::timerfd_settime).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(TpTxRunner::Instance(), &TpTxRunner::AddEpollEvent).stubs().will(returnValue(0));
    ock::ubs::Result ret = UmqTransportPool::Instance().AddTimerEvent(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    /* 成功路径两个 ScopeExit 均 Deactivate,close 不被调 */
    EXPECT_EQ(g_closeCnt, 0);
    GlobalMockObject::verify();
}

/* ==================== AddTransportEpollEvent ==================== */

TEST_F(UmqTransportPoolTest, AddTransportEpollEvent_StartFail_ReturnsStartResult)
{
    MockTpEventRunner(-1, 0);
    ock::ubs::Result ret = UmqTransportPool::Instance().AddTransportEpollEvent(TEST_UMQ_HANDLE);
    EXPECT_EQ(static_cast<int32_t>(ret), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, AddTransportEpollEvent_AddEpollEventFail_ReturnsError)
{
    MockTpEventRunner(0, 1);
    MOCKER_CPP(::umq_transport_pool_eventfd_get).stubs().will(invoke(&MockEventFdGet));
    ock::ubs::Result ret = UmqTransportPool::Instance().AddTransportEpollEvent(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_eventFdGetCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqTransportPoolTest, AddTransportEpollEvent_Success_ReturnsOk)
{
    MockTpEventRunner(0, 0);
    MOCKER_CPP(::umq_transport_pool_eventfd_get).stubs().will(invoke(&MockEventFdGet));
    ock::ubs::Result ret = UmqTransportPool::Instance().AddTransportEpollEvent(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_eventFdGetCnt, 1);
    GlobalMockObject::verify();
}
} // namespace
