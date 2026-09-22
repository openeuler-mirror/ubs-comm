/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_backend.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_event_epoll.h"
#include "core/ubsocket_socket_helper.h"
#include "umq_conn_helper.h"
#include "umq_eid_table.h"
#include "umq_setting.h"
#include "umq_transport_pool.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const uint64_t TEST_UMQ_HANDLE = 0x12345678;
static const uint64_t TEST_UMQ_HANDLE_2 = 0x87654321;
static const int TEST_FD = 42;
static const int TEST_FD_RETRY = 43;
static const uint32_t TEST_EID_INDEX = 0;
static const uint8_t TEST_EID_RAW_BYTE = 0xAB; /* eid raw 首字节标记值(Network Order) */
static const char TEST_DEV_NAME_UDMA[] = "udma_test";
static const char TEST_DEV_NAME_OTHER[] = "udma_other";
static const char TEST_DEV_NAME_BONDING_0[] = "bonding_dev_0";
static const char TEST_DEV_NAME_BONDING_1[] = "bonding_dev_1";
/* 取值域外非法 trans_mode(枚举强转注入,触发 Init 的 default 分支) */
static const umq_trans_mode_t TEST_TRANS_MODE_INVALID = static_cast<umq_trans_mode_t>(99);
/* InitShareJfrMonitering 中 ShareJfrRunner::Start 的非零返回(触发失败分支) */
static const int TEST_START_RET = 5;

/* ---------- mock 共享状态(g_ 前缀) ---------- */

static int g_mockUmqInitRet = 0;
static int g_mockDevAddRet = 0;
static int g_mockDevAddCnt = 0;
static uint64_t g_mockCreateRet = TEST_UMQ_HANDLE;
static int g_mockUmqCreateCnt = 0;
static int g_mockDestroyCnt = 0;
static int g_mockDestroyRet = 0;        /* umq_destroy 返回值(默认成功) */
static int g_mockDevInfoListGetRet = 0; /* 0 = 成功返回 g_mockDevInfos;非 0 = 返回 nullptr */
static int g_mockDevCount = 0;
static int g_mockDevInfoListFreeCnt = 0;
static int g_mockRouteRet = 0;
static int g_mockRouteNum = 0;
static umq_route_t g_mockRoutes[UMQ_MAX_ROUTES]{};
static umq_topo_type_t g_mockTopoType = UMQ_TOPO_TYPE_CLOS;
static int g_mockDevInfoGetRet = 0;
static int g_mockFdGetRet = TEST_FD;      /* 第 1 次 umq_interrupt_fd_get 返回值 */
static int g_mockFdGetRetryRet = TEST_FD; /* 第 2 次(FC retry fd) */
static int g_mockFdGetCnt = 0;
static int g_mockStatsPerfStartRet = 0;
static int g_mockStatsPerfResetRet = 0;
static int g_mockStatsTpPerfStartRet = 0;
static int g_mockStatsTpPerfStopCnt = 0;
static int g_mockUmqUninitCnt = 0;
static int g_mockCurrentProcessSocketIdRet = 0;
static std::vector<uint32_t> g_mockSocketIds = {0};
static umq_dev_info_t g_mockDevInfos[2]{};
static umq_eid_t g_mockEid{};

/* ---------- mock 静态函数(CamelCase,签名与真实 API 完全一致) ---------- */

static void MockUmqUninit(void)
{
    ++g_mockUmqUninitCnt;
}

static uint64_t MockUmqCreate(umq_create_option_t *option)
{
    (void)option;
    ++g_mockUmqCreateCnt;
    return g_mockCreateRet;
}

static int MockUmqDestroy(uint64_t umqh)
{
    (void)umqh;
    ++g_mockDestroyCnt;
    return g_mockDestroyRet;
}

static umq_dev_info_t *MockDevInfoListGet(umq_trans_mode_t umq_trans_mode, int *dev_num)
{
    (void)umq_trans_mode;
    *dev_num = g_mockDevCount;
    return g_mockDevInfoListGetRet == 0 && g_mockDevCount > 0 ? g_mockDevInfos : nullptr;
}

static void MockDevInfoListFree(umq_trans_mode_t umq_trans_mode, umq_dev_info_t *umq_dev_info)
{
    (void)umq_trans_mode;
    (void)umq_dev_info;
    ++g_mockDevInfoListFreeCnt;
}

static int MockDevAdd(umq_trans_info_t *trans_info)
{
    (void)trans_info;
    ++g_mockDevAddCnt;
    return g_mockDevAddRet;
}

static int MockGetRouteList(const umq_route_key_t *route_key, umq_trans_mode_t umq_trans_mode,
                            umq_route_list_t *route_list)
{
    (void)route_key;
    (void)umq_trans_mode;
    route_list->topo_type = g_mockTopoType;
    route_list->route_num = g_mockRouteNum;
    for (int i = 0; i < g_mockRouteNum; ++i) {
        route_list->routes[i] = g_mockRoutes[i];
    }
    return g_mockRouteRet;
}

static int MockDevInfoGet(char *dev_name, umq_trans_mode_t umq_trans_mode, umq_dev_info_t *dev_info)
{
    (void)dev_name;
    (void)umq_trans_mode;
    if (g_mockDevInfoGetRet != 0) {
        return g_mockDevInfoGetRet;
    }
    *dev_info = g_mockDevInfos[0];
    return 0;
}

static int MockInterruptFdGet(uint64_t umqh, umq_interrupt_option_t *option)
{
    (void)umqh;
    (void)option;
    ++g_mockFdGetCnt;
    return g_mockFdGetCnt == 1 ? g_mockFdGetRet : g_mockFdGetRetryRet;
}

static int MockStatsTpPerfStop(umq_trans_mode_t trans_mode)
{
    (void)trans_mode;
    ++g_mockStatsTpPerfStopCnt;
    return 0;
}

static int MockGetCurrentProcessSocketId()
{
    return g_mockCurrentProcessSocketIdRet;
}

static std::vector<uint32_t> MockSocketIdsViaNumaSysfs()
{
    return g_mockSocketIds;
}

/* ---------- 工具函数 ---------- */

static void SetDevInfo(int index, const char *name, uint32_t eidCnt, uint32_t eidIdx, uint8_t rawByte)
{
    umq_dev_info_t &info = g_mockDevInfos[index];
    memset(&info, 0, sizeof(info));
    if (name != nullptr) {
        strncpy(info.dev_name, name, UMQ_DEV_NAME_SIZE - 1);
        info.dev_name[UMQ_DEV_NAME_SIZE - 1] = '\0';
    }
    info.ub.eid_cnt = eidCnt;
    for (uint32_t i = 0; i < eidCnt; ++i) {
        info.ub.eid_list[i].eid_index = eidIdx + i;
        info.ub.eid_list[i].eid.raw[0] = rawByte;
    }
}

static umq_port_id_t MakePort(uint32_t chip, uint32_t die, uint32_t port)
{
    umq_port_id_t p{};
    p.bs.chip_id = chip;
    p.bs.die_id = die;
    p.bs.port_idx = port;
    return p;
}

static void SetSingleRoute(umq_port_id_t srcPort)
{
    g_mockRouteNum = 1;
    umq_route_t &route = g_mockRoutes[0];
    memset(&route, 0, sizeof(route));
    route.src_port = srcPort;
}

/* 挂载 socket id 相关 mock:Init 全链用例必需(真实 sysfs 读取不确定);
 * SocketConnHelper 两方法均为 static,按 SKILL.md §5 用 MOCKER 而非 MOCKER_CPP */
static void MountSocketIdMocks()
{
    MOCKER(&SocketConnHelper::GetCurrentProcessSocketId).stubs().will(returnValue(g_mockCurrentProcessSocketIdRet));
    MOCKER(&SocketConnHelper::GetSocketIdsViaNumaSysfs).stubs().will(invoke(&MockSocketIdsViaNumaSysfs));
}

/* 单 udma 设备准备:SetDevInfo + devCount + DEV_NAME 三件套 */
static void PrepareSingleUdmaDev()
{
    SetDevInfo(0, TEST_DEV_NAME_UDMA, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    g_mockDevCount = 1;
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_UDMA;
}

/* bonding 双设备准备(bonding_dev_1 + bonding_dev_0) */
static void PrepareBondingDevs()
{
    SetDevInfo(0, TEST_DEV_NAME_BONDING_1, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    SetDevInfo(1, TEST_DEV_NAME_BONDING_0, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    g_mockDevCount = 2;
}

/* Init 主链通用挂载:umq_init 成功 + 设备列表 + dev_add + socket id */
static void MountInitChain()
{
    MOCKER_CPP(::umq_init).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_dev_info_list_get).stubs().will(invoke(&MockDevInfoListGet));
    MOCKER_CPP(::umq_dev_info_list_free).stubs().will(invoke(&MockDevInfoListFree));
    MOCKER_CPP(::umq_dev_add).stubs().will(returnValue(0));
    MountSocketIdMocks();
}

/* perf 三 API 挂载(注入值走 g_mockStats*Ret,用例先设后调) */
static void MountPerfMocks()
{
    MOCKER_CPP(::umq_stats_perf_start).stubs().will(returnValue(g_mockStatsPerfStartRet));
    MOCKER_CPP(::umq_stats_perf_reset).stubs().will(returnValue(g_mockStatsPerfResetRet));
    MOCKER_CPP(::umq_stats_tp_perf_start).stubs().will(returnValue(g_mockStatsTpPerfStartRet));
}

/* 设备列表 get/free 对(AddUbDev/FindDevName/FindDevEid 通用) */
static void MountDevListMocks()
{
    MOCKER_CPP(::umq_dev_info_list_get).stubs().will(invoke(&MockDevInfoListGet));
    MOCKER_CPP(::umq_dev_info_list_free).stubs().will(invoke(&MockDevInfoListFree));
}

class UmqBackendTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_BACKUP_LINK_ENABLED = false;
        GlobalSetting::UBS_PROF_ENABLE = false;
        GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::RAW_DEVICE;
        UmqBackend::UMQ_INITED = false;
        UmqBackend::used_ports_.clear();
        UmqBackend::topo_type_ = UMQ_TOPO_TYPE_CLOS;
        UmqSetting::UMQ_DEV_NAME = "";
        UmqSetting::UMQ_DEV_IP = "";
        UmqSetting::UMQ_LOCAL_EID = {};
        UmqSetting::UMQ_IS_BONDING = false;
        UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
        UmqSetting::UMQ_TRANS_MODE = UMQ_TRANS_MODE_UB;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        UmqSetting::UMQ_EID_INDEX = TEST_EID_INDEX;
        UmqSetting::UMQ_ALL_SOCKET_IDS = {0};
        UmqSetting::UMQ_PROCESS_SOCKET_ID = 0;
        UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 2048;
        UmqSetting::UMQ_TP_TYPE = POOL;
        UmqEidTable::Instance().Clean();
        UmqTransportPool::Instance().Clean();
        g_mockUmqInitRet = 0;
        g_mockDevAddRet = 0;
        g_mockDevAddCnt = 0;
        g_mockCreateRet = TEST_UMQ_HANDLE;
        g_mockUmqCreateCnt = 0;
        g_mockDestroyCnt = 0;
        g_mockDestroyRet = 0;
        g_mockDevInfoListGetRet = 0;
        g_mockDevCount = 0;
        g_mockDevInfoListFreeCnt = 0;
        g_mockRouteRet = 0;
        g_mockRouteNum = 0;
        g_mockTopoType = UMQ_TOPO_TYPE_CLOS;
        g_mockDevInfoGetRet = 0;
        g_mockFdGetRet = TEST_FD;
        g_mockFdGetRetryRet = TEST_FD;
        g_mockFdGetCnt = 0;
        g_mockStatsPerfStartRet = 0;
        g_mockStatsPerfResetRet = 0;
        g_mockStatsTpPerfStartRet = 0;
        g_mockStatsTpPerfStopCnt = 0;
        g_mockUmqUninitCnt = 0;
        g_mockCurrentProcessSocketIdRet = 0;
        g_mockSocketIds = {0};
        memset(g_mockDevInfos, 0, sizeof(g_mockDevInfos));
        memset(g_mockRoutes, 0, sizeof(g_mockRoutes));
        g_mockEid = {};
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        UmqEidTable::Instance().Clean();
        UmqTransportPool::Instance().Clean();
        UmqBackend::UMQ_INITED = false;
        errno = 0;
    }
};

using ShareJfrRunner = EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER>;

/* ==================== Init ==================== */

TEST_F(UmqBackendTest, Init_AlreadyInited_ReturnsOk)
{
    UmqBackend::UMQ_INITED = true;
    MOCKER_CPP(::umq_init).expects(exactly(0));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_UmqSettingFail_ReturnsInvalidParam)
{
    /* LoadEnv 对非法 env 只警告不阻断(GetEnvAndValidate 失败即跳过);
     * 直接置静态成员 0 触发 VerifySetting 校验失败 → UBS_INVALID_PARAM */
    UmqSetting::UMQ_MEM_POOL_MAX_SIZE_MB = 0;
    MOCKER_CPP(::umq_init).expects(exactly(0));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_UmqInitFail_MapsErrnoToEinval)
{
    g_mockUmqInitRet = UMQ_FAIL;
    MOCKER_CPP(::umq_init).stubs().will(returnValue(g_mockUmqInitRet));

    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_UnsupportedTransMode_ReturnsError)
{
    UmqSetting::UMQ_TRANS_MODE = TEST_TRANS_MODE_INVALID;
    MOCKER_CPP(::umq_init).stubs().will(returnValue(0));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_TransModeIb_LogsAndContinues)
{
    UmqSetting::UMQ_TRANS_MODE = UMQ_TRANS_MODE_IB;
    MOCKER_CPP(::umq_init).stubs().will(returnValue(0));
    MountSocketIdMocks();

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_TRUE(UmqBackend::UMQ_INITED);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_DevAddFail_ReturnsError)
{
    g_mockDevInfoListGetRet = -1; /* FindDevName 失败 → AddUbDev 失败 */
    MOCKER_CPP(::umq_init).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_dev_info_list_get).stubs().will(invoke(&MockDevInfoListGet));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_SocketIdsEmpty_ReturnsError)
{
    PrepareSingleUdmaDev();
    g_mockSocketIds.clear();
    MountInitChain();

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_ProcessSocketIdInvalid_ReturnsError)
{
    PrepareSingleUdmaDev();
    g_mockCurrentProcessSocketIdRet = -1;
    MountInitChain();

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_PerfStartFail_ReturnsUmqCreate)
{
    g_mockStatsPerfStartRet = UMQ_FAIL;
    PrepareSingleUdmaDev();
    GlobalSetting::UBS_PROF_ENABLE = true;
    MountInitChain();
    MountPerfMocks();

    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_UMQ_CREATE);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_PerfResetFail_ReturnsUmqCreate)
{
    g_mockStatsPerfResetRet = UMQ_FAIL;
    PrepareSingleUdmaDev();
    GlobalSetting::UBS_PROF_ENABLE = true;
    MountInitChain();
    MountPerfMocks();

    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_UMQ_CREATE);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_TpPerfStartFail_ReturnsUmqCreate)
{
    g_mockStatsTpPerfStartRet = UMQ_FAIL;
    PrepareSingleUdmaDev();
    GlobalSetting::UBS_PROF_ENABLE = true;
    MountInitChain();
    MountPerfMocks();

    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_UMQ_CREATE);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_PerfAllSuccess_ReturnsOk)
{
    PrepareSingleUdmaDev();
    GlobalSetting::UBS_PROF_ENABLE = true;
    MountInitChain();
    MountPerfMocks();

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_TRUE(UmqBackend::UMQ_INITED);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_RawDeviceSuccess_ReturnsOk)
{
    PrepareSingleUdmaDev();
    MountInitChain();

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_TRUE(UmqBackend::UMQ_INITED);
    EXPECT_EQ(g_mockUmqCreateCnt, 0); /* RAW_DEVICE 不创建主 umq */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_BondingRouteSuccess_ReturnsOk)
{
    /* 三目 :118 分支:UMQ_IS_BONDING && !UBS_BACKUP_LINK_ENABLED → BONDING_ROUTE;
     * bonding 设备名跳过设备查找,直接 EID 模式 + IS_BONDING */
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_BONDING_1;
    MOCKER_CPP(::umq_init).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_dev_add).stubs().will(returnValue(0));
    MountSocketIdMocks();

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_TRUE(UmqBackend::UMQ_INITED);
    EXPECT_TRUE(UmqSetting::UMQ_IS_BONDING);
    EXPECT_EQ(GlobalSetting::LINK_SELECTION_POLICY, LinkSelectionPolicy::BONDING_ROUTE);
    EXPECT_EQ(g_mockUmqCreateCnt, 0); /* BONDING_ROUTE 不创建主 umq、不 prefill */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_BondingBackupSuccess_ReturnsOk)
{
    PrepareBondingDevs();
    SetSingleRoute(MakePort(1, 0, 1));
    g_mockDevInfoGetRet = 0; /* GetDevEid 走真实路径,dev_info mock 填充 */
    GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    UmqSetting::UMQ_DEV_NAME = "";
    MountInitChain();
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGet));
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::AddEpollEvent).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP(&UmqTransportPool::WarmUp).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_TRUE(UmqBackend::UMQ_INITED);
    EXPECT_TRUE(UmqSetting::UMQ_IS_BONDING);
    EXPECT_EQ(g_mockUmqCreateCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_Bonding_CreateMainUmqFail_ReturnsUmqCreate)
{
    PrepareBondingDevs();
    g_mockRouteRet = UMQ_FAIL; /* GetRouteList 失败 → CreateShareMainUmq 返回 INVALID */
    GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
    MountInitChain();
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy)); /* UmqCleanup 兜底 */
    MOCKER_CPP(::umq_uninit).stubs().will(invoke(&MockUmqUninit));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_UMQ_CREATE);
    EXPECT_EQ(g_mockUmqUninitCnt, 1); /* UmqCleanup 执行 */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_Bonding_PrefillFail_ReturnsPrefillRx)
{
    PrepareBondingDevs();
    SetSingleRoute(MakePort(1, 0, 1));
    GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    MountInitChain();
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGet));
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy)); /* UmqCleanup 遍历已建 handle */
    MOCKER_CPP(::umq_uninit).stubs().will(invoke(&MockUmqUninit));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_PREFILL_RX);
    EXPECT_EQ(g_mockDestroyCnt, 1); /* UmqCleanup 遍历销毁已建的主 umq */
    EXPECT_EQ(g_mockUmqUninitCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_Bonding_JfrMonitorFail_ReturnsError)
{
    PrepareBondingDevs();
    SetSingleRoute(MakePort(1, 0, 1));
    g_mockFdGetRet = -1; /* InitShareJfrMonitering 的 interrupt_fd_get 失败 */
    GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    MountInitChain();
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGet));
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy)); /* UmqCleanup 遍历已建 handle */
    MOCKER_CPP(::umq_uninit).stubs().will(invoke(&MockUmqUninit));

    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(g_mockUmqUninitCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, Init_Bonding_WarmUpFail_ReturnsUmqCreate)
{
    PrepareBondingDevs();
    SetSingleRoute(MakePort(1, 0, 1));
    GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    MountInitChain();
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGet));
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));
    MOCKER_CPP(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::AddEpollEvent).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP(&UmqTransportPool::WarmUp).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy)); /* UmqCleanup 遍历已建 handle */
    MOCKER_CPP(::umq_uninit).stubs().will(invoke(&MockUmqUninit));

    ock::ubs::Result ret = UmqBackend::Init();

    EXPECT_EQ(ret, UBS_UMQ_CREATE);
    EXPECT_EQ(g_mockUmqUninitCnt, 1);
    GlobalMockObject::verify();
}

/* ==================== UnInit / UmqCleanup / DestroyShareMainUmq ==================== */

TEST_F(UmqBackendTest, UnInit_NotInited_ReturnsEarly)
{
    UmqBackend::UMQ_INITED = false;
    MOCKER_CPP(::umq_uninit).expects(exactly(0));

    UmqBackend::UnInit();

    EXPECT_FALSE(UmqBackend::UMQ_INITED);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, UnInit_Inited_UninitsAndResetsFlag)
{
    UmqBackend::UMQ_INITED = true;
    MOCKER_CPP(::umq_uninit).stubs().will(invoke(&MockUmqUninit));

    UmqBackend::UnInit();

    EXPECT_EQ(g_mockUmqUninitCnt, 1);
    EXPECT_FALSE(UmqBackend::UMQ_INITED);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, UmqCleanup_ProfEnabled_StopsTpPerf)
{
    UmqBackend::UMQ_INITED = true;
    GlobalSetting::UBS_PROF_ENABLE = true;
    MOCKER_CPP(::umq_stats_tp_perf_stop).stubs().will(invoke(&MockStatsTpPerfStop));
    MOCKER_CPP(::umq_uninit).stubs().will(invoke(&MockUmqUninit));

    UmqBackend::UmqCleanup();

    EXPECT_EQ(g_mockStatsTpPerfStopCnt, 1);
    EXPECT_EQ(g_mockUmqUninitCnt, 1);
    EXPECT_FALSE(UmqBackend::UMQ_INITED);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, DestroyShareMainUmq_WithHandles_DestroysAllAndCleans)
{
    UmqEidTable::Instance().Add(g_mockEid, RM_TP, TEST_UMQ_HANDLE);
    UmqEidTable::Instance().Add(g_mockEid, RM_TP, TEST_UMQ_HANDLE_2);
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));

    UmqBackend::DestroyShareMainUmq();

    EXPECT_EQ(g_mockDestroyCnt, 2);
    EXPECT_TRUE(UmqEidTable::Instance().GetAllUmqHandles().empty());
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, DestroyShareMainUmq_DestroyFail_Continues)
{
    UmqEidTable::Instance().Add(g_mockEid, RM_TP, TEST_UMQ_HANDLE);
    UmqEidTable::Instance().Add(g_mockEid, RM_TP, TEST_UMQ_HANDLE_2);
    g_mockDestroyRet = UMQ_FAIL;
    MOCKER_CPP(::umq_destroy).stubs().will(invoke(&MockUmqDestroy));

    UmqBackend::DestroyShareMainUmq();

    EXPECT_EQ(g_mockDestroyCnt, 2); /* 失败仅记日志,继续遍历 */
    EXPECT_TRUE(UmqEidTable::Instance().GetAllUmqHandles().empty());
    GlobalMockObject::verify();
}

/* ==================== AddUbDev ==================== */

TEST_F(UmqBackendTest, AddUbDev_EmptyDevName_BondingDev0_AddsWithEidMode)
{
    PrepareBondingDevs();
    UmqSetting::UMQ_DEV_NAME = "";
    MountDevListMocks();
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockDevAdd));

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(trans_info.trans_mode, UMQ_TRANS_MODE_UB);
    EXPECT_EQ(trans_info.dev_info.assign_mode, UMQ_DEV_ASSIGN_MODE_EID);
    EXPECT_EQ(trans_info.dev_info.eid.eid.raw[0], TEST_EID_RAW_BYTE);
    EXPECT_TRUE(UmqSetting::UMQ_IS_BONDING);
    EXPECT_EQ(g_mockDevAddCnt, 1);
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_EmptyDevName_NoBondingDev_ReturnsError)
{
    SetDevInfo(0, TEST_DEV_NAME_UDMA, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    g_mockDevCount = 1;
    UmqSetting::UMQ_DEV_NAME = "";
    MountDevListMocks();

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_mockDevAddCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_EmptyDevName_BondingNoEid_ReturnsError)
{
    SetDevInfo(0, TEST_DEV_NAME_BONDING_0, 0, TEST_EID_INDEX, TEST_EID_RAW_BYTE); /* eid_cnt == 0 */
    g_mockDevCount = 1;
    UmqSetting::UMQ_DEV_NAME = "";
    MountDevListMocks();

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_mockDevAddCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_UdmaPrefix_FindsEid_AddsWithDevMode)
{
    PrepareSingleUdmaDev();
    MountDevListMocks();
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockDevAdd));

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(trans_info.dev_info.assign_mode, UMQ_DEV_ASSIGN_MODE_DEV);
    EXPECT_EQ(trans_info.dev_info.dev.eid_idx, TEST_EID_INDEX);
    EXPECT_EQ(strcmp(trans_info.dev_info.dev.dev_name, TEST_DEV_NAME_UDMA), 0);
    EXPECT_FALSE(UmqSetting::UMQ_IS_BONDING);
    EXPECT_EQ(g_mockDevAddCnt, 1);
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_UdmaPrefix_DevNotFound_ReturnsError)
{
    SetDevInfo(0, TEST_DEV_NAME_OTHER, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    g_mockDevCount = 1;
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_UDMA;
    MountDevListMocks();

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_mockDevAddCnt, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_BondingName_AssignsEidMode)
{
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_BONDING_1; /* 非空、非 udma 前缀:跳过设备查找 */
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockDevAdd));

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(trans_info.dev_info.assign_mode, UMQ_DEV_ASSIGN_MODE_EID);
    EXPECT_TRUE(UmqSetting::UMQ_IS_BONDING);
    EXPECT_EQ(g_mockDevAddCnt, 1);
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 0); /* 未走设备查找 */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_DevNameTooLong_ReturnsError)
{
    UmqSetting::UMQ_DEV_NAME = std::string(DEV_NAME_STR_LEN_MAX, 'd'); /* 64 字符,>= 上限 */
    MOCKER_CPP(::umq_dev_add).expects(exactly(0));

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_DevNameMaxLength_AddsSuccess)
{
    UmqSetting::UMQ_DEV_NAME = std::string(DEV_NAME_STR_LEN_MAX - 1, 'd'); /* 63 字符,sprintf 返回 63 < 64 */
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockDevAdd));

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_mockDevAddCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_DevAddFail_MapsErrno)
{
    g_mockDevAddRet = UMQ_FAIL;
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_BONDING_1;
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockDevAdd));

    errno = EINVAL;
    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, AddUbDev_DevAddEexist_ReturnsOk)
{
    g_mockDevAddRet = -UMQ_ERR_EEXIST; /* 设备已存在视为成功 */
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_BONDING_1;
    MOCKER_CPP(::umq_dev_add).stubs().will(invoke(&MockDevAdd));

    umq_trans_info_t trans_info{};
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_mockDevAddCnt, 1);
    GlobalMockObject::verify();
}

/* ==================== FindDevName ==================== */

TEST_F(UmqBackendTest, FindDevName_ListGetNull_MapsErrno)
{
    g_mockDevInfoListGetRet = -1;
    MOCKER_CPP(::umq_dev_info_list_get).stubs().will(invoke(&MockDevInfoListGet));

    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::FindDevName();

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL); /* ConvertHandleResult(BIND_INFO_GET, EINVAL) → EINVAL */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, FindDevName_DevCountZero_MapsErrno)
{
    g_mockDevCount = 0;
    MOCKER_CPP(::umq_dev_info_list_get).stubs().will(invoke(&MockDevInfoListGet));

    errno = ENOMEM;
    ock::ubs::Result ret = UmqBackend::FindDevName();

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, ENOMEM); /* ConvertHandleResult(BIND_INFO_GET, ENOMEM) → ENOMEM */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, FindDevName_NoBondingDev_ReturnsError)
{
    SetDevInfo(0, TEST_DEV_NAME_UDMA, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    g_mockDevCount = 1;
    MountDevListMocks();

    ock::ubs::Result ret = UmqBackend::FindDevName();

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, FindDevName_BondingDev0Found_SetsNameAndEid)
{
    PrepareBondingDevs();
    MountDevListMocks();

    ock::ubs::Result ret = UmqBackend::FindDevName();

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(UmqSetting::UMQ_DEV_NAME, TEST_DEV_NAME_BONDING_0); /* 优先精确匹配 bonding_dev_0 */
    EXPECT_EQ(UmqSetting::UMQ_LOCAL_EID.raw[0], TEST_EID_RAW_BYTE);
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, FindDevName_BondingPrefixFallback_SetsNameAndEid)
{
    SetDevInfo(0, TEST_DEV_NAME_BONDING_1, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    g_mockDevCount = 1;
    MountDevListMocks();

    ock::ubs::Result ret = UmqBackend::FindDevName();

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(UmqSetting::UMQ_DEV_NAME, TEST_DEV_NAME_BONDING_1); /* 回退到 bonding_dev_ 前缀 */
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 1);
    GlobalMockObject::verify();
}

/* ==================== FindDevEid ==================== */

TEST_F(UmqBackendTest, FindDevEid_ListGetNull_MapsErrno)
{
    g_mockDevInfoListGetRet = -1;
    MOCKER_CPP(::umq_dev_info_list_get).stubs().will(invoke(&MockDevInfoListGet));

    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::FindDevEid(TEST_DEV_NAME_UDMA, TEST_EID_INDEX);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, FindDevEid_DevNotFound_ReturnsError)
{
    SetDevInfo(0, TEST_DEV_NAME_OTHER, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE);
    g_mockDevCount = 1;
    MountDevListMocks();

    ock::ubs::Result ret = UmqBackend::FindDevEid(TEST_DEV_NAME_UDMA, TEST_EID_INDEX);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 1); /* ScopeExit 释放 */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, FindDevEid_EidIndexNotFound_ReturnsError)
{
    SetDevInfo(0, TEST_DEV_NAME_UDMA, 1, TEST_EID_INDEX + 1, TEST_EID_RAW_BYTE); /* eid_index 不匹配 */
    g_mockDevCount = 1;
    MountDevListMocks();

    ock::ubs::Result ret = UmqBackend::FindDevEid(TEST_DEV_NAME_UDMA, TEST_EID_INDEX);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 1);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, FindDevEid_Success_SetsLocalEid)
{
    PrepareSingleUdmaDev();
    MountDevListMocks();

    ock::ubs::Result ret = UmqBackend::FindDevEid(TEST_DEV_NAME_UDMA, TEST_EID_INDEX);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(UmqSetting::UMQ_LOCAL_EID.raw[0], TEST_EID_RAW_BYTE);
    EXPECT_EQ(g_mockDevInfoListFreeCnt, 1);
    GlobalMockObject::verify();
}

/* ==================== CreateShareMainUmq ==================== */

TEST_F(UmqBackendTest, CreateShareMainUmq_NotBonding_CreatesMainUmq)
{
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_UDMA;
    UmqSetting::UMQ_LOCAL_EID.raw[0] = TEST_EID_RAW_BYTE;
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));

    umq_eid_t local_eid{};
    uint64_t ret = UmqBackend::CreateShareMainUmq(local_eid);

    EXPECT_EQ(ret, TEST_UMQ_HANDLE);
    EXPECT_EQ(g_mockUmqCreateCnt, 1);
    EXPECT_EQ(UmqEidTable::Instance().GetFirst(local_eid, UmqSetting::UMQ_UB_TRANS_MODE)->GetUmqHandle(),
              TEST_UMQ_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, CreateShareMainUmq_AlreadyExists_SkipsCreate)
{
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_UDMA;
    UmqSetting::UMQ_LOCAL_EID.raw[0] = TEST_EID_RAW_BYTE;
    UmqEidTable::Instance().Add(UmqSetting::UMQ_LOCAL_EID, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE_2);
    MOCKER_CPP(::umq_create).expects(exactly(0));

    umq_eid_t local_eid{};
    uint64_t ret = UmqBackend::CreateShareMainUmq(local_eid);

    EXPECT_EQ(ret, TEST_UMQ_HANDLE_2); /* 已存在 → 不创建,返回已存在 handle */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, CreateShareMainUmq_Bonding_GetRouteListFail_ReturnsInvalid)
{
    g_mockRouteRet = UMQ_FAIL;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));

    umq_eid_t local_eid{};
    uint64_t ret = UmqBackend::CreateShareMainUmq(local_eid);

    EXPECT_EQ(ret, UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, CreateShareMainUmq_Bonding_GetDevEidFail_ReturnsInvalid)
{
    g_mockDevInfoGetRet = UMQ_FAIL;
    SetSingleRoute(MakePort(1, 0, 1));
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGet));

    umq_eid_t local_eid{};
    uint64_t ret = UmqBackend::CreateShareMainUmq(local_eid);

    EXPECT_EQ(ret, UMQ_INVALID_HANDLE);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, CreateShareMainUmq_Bonding_Success_SortsDedupsAndStores)
{
    /* 4 条路由: chip2@(0,1) x2(重复 value 去重)、chip1@(0,0)(target chip)、chip2@(1,2)、chip3@(0,3);
     * 期望排序后 target chip1 在前,同 chip 按 die、再按 port 排列 */
    umq_route_t routes[UMQ_MAX_ROUTES]{};
    routes[0].src_port = MakePort(2, 0, 1);
    routes[1].src_port = MakePort(1, 0, 0);
    routes[2].src_port = MakePort(2, 0, 1);
    routes[3].src_port = MakePort(2, 1, 2);
    routes[4].src_port = MakePort(3, 0, 3);
    for (int i = 0; i < 5; ++i) {
        g_mockRoutes[i] = routes[i];
    }
    g_mockRouteNum = 5;
    g_mockTopoType = UMQ_TOPO_TYPE_FULLMESH_1D;
    SetDevInfo(0, TEST_DEV_NAME_BONDING_1, 1, TEST_EID_INDEX, TEST_EID_RAW_BYTE); /* GetDevEid 返回数据 */
    UmqSetting::UMQ_DEV_NAME = TEST_DEV_NAME_BONDING_1;
    UmqSetting::UMQ_ALL_SOCKET_IDS = {0};
    UmqSetting::UMQ_PROCESS_SOCKET_ID = 0;
    GlobalSetting::LINK_SELECTION_POLICY = LinkSelectionPolicy::BONDING_BACKUP;
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGet));
    MOCKER_CPP(::umq_create).stubs().will(invoke(&MockUmqCreate));

    umq_eid_t local_eid{};
    uint64_t ret = UmqBackend::CreateShareMainUmq(local_eid);

    EXPECT_EQ(ret, TEST_UMQ_HANDLE);
    EXPECT_EQ(UmqBackend::GetTopoType(), UMQ_TOPO_TYPE_FULLMESH_1D);
    std::vector<umq_port_id_t> ports = UmqBackend::GetUsedPorts();
    ASSERT_EQ(ports.size(), 4u);       /* 重复 value 去重,保留 4 个 */
    EXPECT_EQ(ports[0].bs.chip_id, 1); /* target chip 排前 */
    EXPECT_EQ(ports[1].bs.chip_id, 2); /* 同 chip 按 die 升序 */
    EXPECT_EQ(ports[1].bs.die_id, 0);
    EXPECT_EQ(ports[2].bs.chip_id, 2);
    EXPECT_EQ(ports[2].bs.die_id, 1);
    EXPECT_EQ(ports[3].bs.chip_id, 3);
    EXPECT_EQ(g_mockUmqCreateCnt, 1);
    GlobalMockObject::verify();
}

/* ==================== PrefillShareMainUmq ==================== */

TEST_F(UmqBackendTest, PrefillShareMainUmq_ShareJfrDisabled_ReturnsOk)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = false;

    umq_eid_t local_eid{};
    ock::ubs::Result ret = UmqBackend::PrefillShareMainUmq(local_eid);

    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, PrefillShareMainUmq_MainUmqNotFound_ReturnsError)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    umq_eid_t local_eid{};
    local_eid.raw[0] = TEST_EID_RAW_BYTE; /* 未 Add,GetFirst 返回 null */

    ock::ubs::Result ret = UmqBackend::PrefillShareMainUmq(local_eid);

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, PrefillShareMainUmq_PrefillRxFail_ReturnsError)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    umq_eid_t local_eid{};
    local_eid.raw[0] = TEST_EID_RAW_BYTE;
    UmqEidTable::Instance().Add(local_eid, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
    MOCKER_CPP(&UmqConnHelper::PrefillRx).stubs().will(returnValue(static_cast<int32_t>(UBS_ERROR)));

    ock::ubs::Result ret = UmqBackend::PrefillShareMainUmq(local_eid);

    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, PrefillShareMainUmq_Success_PrefillsOnce)
{
    GlobalSetting::UBS_ENABLE_SHARE_JFR = true;
    umq_eid_t local_eid{};
    local_eid.raw[0] = TEST_EID_RAW_BYTE;
    UmqEidTable::Instance().Add(local_eid, UmqSetting::UMQ_UB_TRANS_MODE, TEST_UMQ_HANDLE);
    MOCKER_CPP(&UmqConnHelper::PrefillRx).expects(exactly(1)).will(returnValue(static_cast<int32_t>(UBS_OK)));

    ock::ubs::Result ret = UmqBackend::PrefillShareMainUmq(local_eid);
    EXPECT_EQ(ret, UBS_OK);
    ock::ubs::Result ret2 = UmqBackend::PrefillShareMainUmq(local_eid); /* 已 prefilled,不再调用 */
    EXPECT_EQ(ret2, UBS_OK);
    GlobalMockObject::verify();
}

/* ==================== InitShareJfrMonitering ==================== */

TEST_F(UmqBackendTest, InitShareJfrMonitering_StartFail_ReturnsStartResult)
{
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(TEST_START_RET));

    uint64_t ret = UmqBackend::InitShareJfrMonitering(TEST_UMQ_HANDLE);

    EXPECT_EQ(ret, static_cast<uint64_t>(TEST_START_RET));
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, InitShareJfrMonitering_FdGetFail_MapsErrno)
{
    g_mockFdGetRet = -1;
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));

    errno = EINVAL;
    uint64_t ret = UmqBackend::InitShareJfrMonitering(TEST_UMQ_HANDLE);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, InitShareJfrMonitering_AddShareEventFail_ReturnsError)
{
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::AddEpollEvent).stubs().will(returnValue(-1));

    uint64_t ret = UmqBackend::InitShareJfrMonitering(TEST_UMQ_HANDLE);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_mockFdGetCnt, 1); /* 未走到 retry fd */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, InitShareJfrMonitering_FlowControlDisabled_ReturnsOk)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = false;
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::AddEpollEvent).stubs().will(returnValue(0));

    uint64_t ret = UmqBackend::InitShareJfrMonitering(TEST_UMQ_HANDLE);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_mockFdGetCnt, 1); /* FC 关闭:不取 retry fd */
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, InitShareJfrMonitering_RetryFdGetFail_MapsErrno)
{
    g_mockFdGetRetryRet = -1; /* 第 2 次(FC retry fd)失败 */
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::AddEpollEvent).stubs().will(returnValue(0));

    errno = EINVAL;
    uint64_t ret = UmqBackend::InitShareJfrMonitering(TEST_UMQ_HANDLE);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    EXPECT_EQ(g_mockFdGetCnt, 2);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, InitShareJfrMonitering_AddRetryEventFail_ReturnsError)
{
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::AddEpollEvent)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(-1)); /* share 事件成功,retry 事件失败 */

    uint64_t ret = UmqBackend::InitShareJfrMonitering(TEST_UMQ_HANDLE);

    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(g_mockFdGetCnt, 2);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, InitShareJfrMonitering_Success_RegistersBothEvents)
{
    g_mockFdGetRet = TEST_FD;
    g_mockFdGetRetryRet = TEST_FD_RETRY;
    UmqSetting::UMQ_FLOW_CONTROL_ENABLE = true;
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::Start).stubs().will(returnValue(0));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(invoke(&MockInterruptFdGet));
    MOCKER_CPP_VIRTUAL(ShareJfrRunner::Instance(), &ShareJfrRunner::AddEpollEvent).stubs().will(returnValue(0));

    uint64_t ret = UmqBackend::InitShareJfrMonitering(TEST_UMQ_HANDLE);

    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(g_mockFdGetCnt, 2);
    GlobalMockObject::verify();
}

/* ==================== 访问器 ==================== */

TEST_F(UmqBackendTest, GetUsedPorts_ReturnsAssignedPorts)
{
    UmqBackend::used_ports_.push_back(MakePort(1, 0, 1));
    UmqBackend::used_ports_.push_back(MakePort(2, 0, 2));

    std::vector<umq_port_id_t> ports = UmqBackend::GetUsedPorts();

    ASSERT_EQ(ports.size(), 2u);
    EXPECT_EQ(ports[0].bs.chip_id, 1);
    EXPECT_EQ(ports[1].bs.chip_id, 2);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendTest, GetTopoType_ReturnsAssignedType)
{
    UmqBackend::topo_type_ = UMQ_TOPO_TYPE_CLOS;

    umq_topo_type_t type = UmqBackend::GetTopoType();

    EXPECT_EQ(type, UMQ_TOPO_TYPE_CLOS);
    GlobalMockObject::verify();
}

} // namespace
