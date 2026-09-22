/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_conn_helper.h"
#include "umq_setting.h"

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_common_includes.h"
#include "common/ubsocket_global_setting.h"
#include "core/ubsocket_event_epoll.h"
#include "iobuf/ubsocket_iobuf.h"
#include "under_api/dl_umq_api.h"

using namespace ock::ubs;
using namespace umq;

namespace {
static const uint64_t TEST_UMQ_HANDLE = 12345;
static const uint32_t TEST_EID_INDEX = 7;
static const uint32_t TEST_RXE_POST_FACTOR = 2;
static const uint32_t TEST_RX_DEPTH = 64;
static const uint32_t TEST_ROUTE_NUM = 3;
static const char TEST_DEV_NAME[] = "udma_test";
static const uint8_t TEST_EID_RAW_BYTE = 0xAB; /* eid raw 首字节标记值(Network Order) */
/* 取值域外非法 trans_mode(枚举强转注入,触发 GetTpInfo 失败分支) */
static const ub_trans_mode TEST_TRANS_MODE_INVALID_POSITIVE = static_cast<ub_trans_mode>(100);

/* PrefillRx 单次 do-while 循环内的 umq_post 调用数,用于 expects 断言 */
static uint32_t g_mockCfgPostFactor = 0;
static uint32_t g_mockCfgRxDepth = 0;
static uint32_t g_mockRouteNum = 0;

static umq_buf_t g_fakeRxBuf{};

/* ---------- mock 静态函数(CamelCase,签名与真实 API 完全一致) ---------- */

static int MockDevInfoGetSuccess(char *dev_name, umq_trans_mode_t trans_mode, umq_dev_info_t *dev_info)
{
    dev_info->ub.eid_cnt = 1;
    dev_info->ub.eid_list[0].eid_index = TEST_EID_INDEX;
    dev_info->ub.eid_list[0].eid.raw[0] = TEST_EID_RAW_BYTE;
    return 0;
}

static int MockDevInfoGetFail(char *dev_name, umq_trans_mode_t trans_mode, umq_dev_info_t *dev_info)
{
    errno = EINVAL;
    return UMQ_FAIL;
}

static int MockCfgGet(uint64_t umqh, umq_cfg_get_t *cfg)
{
    cfg->rqe_post_factor = g_mockCfgPostFactor;
    cfg->rx_depth = g_mockCfgRxDepth;
    return 0;
}

static int MockCfgGetFail(uint64_t umqh, umq_cfg_get_t *cfg)
{
    return UMQ_FAIL;
}

static umq_buf_t *MockBufAlloc(uint32_t request_size, uint32_t request_qbuf_num, uint64_t umqh,
                               umq_alloc_option_t *option)
{
    return &g_fakeRxBuf;
}

static umq_buf_t *MockBufAllocFail(uint32_t request_size, uint32_t request_qbuf_num, uint64_t umqh,
                                   umq_alloc_option_t *option)
{
    return nullptr;
}

static int MockPostWithBadQbuf(uint64_t umqh, umq_buf_t *qbuf, umq_io_option_t *option, umq_buf_t **bad_qbuf)
{
    if (bad_qbuf != nullptr) {
        *bad_qbuf = qbuf;
    }
    errno = EINVAL;
    return UMQ_FAIL;
}

static int MockPostFail(uint64_t umqh, umq_buf_t *qbuf, umq_io_option_t *option, umq_buf_t **bad_qbuf)
{
    errno = EINVAL;
    return UMQ_FAIL;
}

static void MockBufFree(umq_buf_t *qbuf)
{
    (void)qbuf;
}

static int MockGetRouteList(const umq_route_key_t *route_key, umq_trans_mode_t trans_mode, umq_route_list_t *route_list)
{
    route_list->route_num = g_mockRouteNum;
    return 0;
}

static int MockGetRouteListFail(const umq_route_key_t *route_key, umq_trans_mode_t trans_mode,
                                umq_route_list_t *route_list)
{
    errno = EINVAL;
    return UMQ_FAIL;
}
} // namespace

/* ==================== 纯逻辑用例(无需 UMQ mock) ==================== */

class UmqConnHelperPureLogicTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
    }

    void TearDown() override
    {
        errno = 0;
    }
};

// ==================== GetTpInfo ====================

TEST_F(UmqConnHelperPureLogicTest, GetTpInfo_RcTp_SetsRcRtp)
{
    umq_tp_mode_t tp_mode = UMQ_TM_MAX;
    umq_tp_type_t tp_type = UMQ_TP_TYPE_MAX;
    ock::ubs::Result ret = UmqConnHelper::GetTpInfo(tp_mode, tp_type, RC_TP);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(tp_mode, UMQ_TM_RC);
    EXPECT_EQ(tp_type, UMQ_TP_TYPE_RTP);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTpInfo_RmTp_SetsRmRtp)
{
    umq_tp_mode_t tp_mode = UMQ_TM_MAX;
    umq_tp_type_t tp_type = UMQ_TP_TYPE_MAX;
    ock::ubs::Result ret = UmqConnHelper::GetTpInfo(tp_mode, tp_type, RM_TP);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(tp_mode, UMQ_TM_RM);
    EXPECT_EQ(tp_type, UMQ_TP_TYPE_RTP);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTpInfo_RmCtp_SetsRmCtp)
{
    umq_tp_mode_t tp_mode = UMQ_TM_MAX;
    umq_tp_type_t tp_type = UMQ_TP_TYPE_MAX;
    ock::ubs::Result ret = UmqConnHelper::GetTpInfo(tp_mode, tp_type, RM_CTP);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(tp_mode, UMQ_TM_RM);
    EXPECT_EQ(tp_type, UMQ_TP_TYPE_CTP);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTpInfo_RcCtp_SetsRcCtp)
{
    umq_tp_mode_t tp_mode = UMQ_TM_MAX;
    umq_tp_type_t tp_type = UMQ_TP_TYPE_MAX;
    ock::ubs::Result ret = UmqConnHelper::GetTpInfo(tp_mode, tp_type, RC_CTP);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(tp_mode, UMQ_TM_RC);
    EXPECT_EQ(tp_type, UMQ_TP_TYPE_CTP);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTpInfo_UnsupportedMode_ReturnsError)
{
    umq_tp_mode_t tp_mode = UMQ_TM_MAX;
    umq_tp_type_t tp_type = UMQ_TP_TYPE_MAX;
    ock::ubs::Result ret = UmqConnHelper::GetTpInfo(tp_mode, tp_type, TEST_TRANS_MODE_INVALID_POSITIVE);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

// ==================== GetTargetChipId ====================

TEST_F(UmqConnHelperPureLogicTest, GetTargetChipId_FoundAtStart_ReturnsChipId)
{
    std::vector<uint32_t> socket_ids = {0, 1, 2};
    std::vector<uint32_t> chip_id_list = {10, 20, 30};
    EXPECT_EQ(UmqConnHelper::GetTargetChipId(socket_ids, chip_id_list, 0), 10u);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTargetChipId_FoundAtLast_ReturnsChipId)
{
    std::vector<uint32_t> socket_ids = {0, 1, 2};
    std::vector<uint32_t> chip_id_list = {10, 20, 30};
    EXPECT_EQ(UmqConnHelper::GetTargetChipId(socket_ids, chip_id_list, 2), 30u);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTargetChipId_NotFound_ReturnsUint32Max)
{
    std::vector<uint32_t> socket_ids = {0, 1, 2};
    std::vector<uint32_t> chip_id_list = {10, 20, 30};
    EXPECT_EQ(UmqConnHelper::GetTargetChipId(socket_ids, chip_id_list, 5), UINT32_MAX);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTargetChipId_IndexOutOfBounds_ReturnsUint32Max)
{
    std::vector<uint32_t> socket_ids = {0};
    std::vector<uint32_t> chip_id_list;
    EXPECT_EQ(UmqConnHelper::GetTargetChipId(socket_ids, chip_id_list, 0), UINT32_MAX);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperPureLogicTest, GetTargetChipId_EmptySocketIds_ReturnsUint32Max)
{
    std::vector<uint32_t> socket_ids;
    std::vector<uint32_t> chip_id_list = {10};
    EXPECT_EQ(UmqConnHelper::GetTargetChipId(socket_ids, chip_id_list, 0), UINT32_MAX);
    GlobalMockObject::verify();
}

/* ==================== 需要 UMQ mock 的用例 ==================== */

class UmqConnHelperTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        UmqSetting::UMQ_LINK_PRIORITY = UBSOCKET_LINK_PRIORITY_DEFAULT;
        UmqSetting::UMQ_TP_TYPE = POOL;
        UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
        g_mockCfgPostFactor = 0;
        g_mockCfgRxDepth = 0;
        g_mockRouteNum = 0;
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
        errno = 0;
    }
};

// ==================== NewBaseUmqCreateOptions ====================

TEST_F(UmqConnHelperTest, NewBaseUmqCreateOptions_DefaultTransMode_SetsFieldsAndFlags)
{
    UmqSetting::UMQ_UB_TRANS_MODE = RM_TP;
    umq_create_option_t option{};
    ock::ubs::Result ret = UmqConnHelper::NewBaseUmqCreateOptions(option, RM_TP);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(option.trans_mode, UmqSetting::UMQ_TRANS_MODE);
    EXPECT_EQ(option.rx_depth, GlobalSetting::UBS_RX_DEPTH);
    EXPECT_EQ(option.tx_depth, GlobalSetting::UBS_TX_DEPTH);
    EXPECT_EQ(option.rx_buf_size, UmqSetting::GetIOBufSize());
    EXPECT_EQ(option.mode, UMQ_MODE_INTERRUPT);
    EXPECT_EQ(option.tp_mode, UMQ_TM_RM);
    EXPECT_EQ(option.tp_type, UMQ_TP_TYPE_RTP);
    EXPECT_NE(option.create_flag & UMQ_CREATE_FLAG_PRIORITY, 0u);
    EXPECT_NE(option.create_flag & UMQ_CREATE_FLAG_SHARE_TRANSPORT, 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, NewBaseUmqCreateOptions_PriorityNotSet_NoPriorityFlag)
{
    UmqSetting::UMQ_LINK_PRIORITY = UBSOCKET_LINK_PRIORITY_NOT_SET;
    umq_create_option_t option{};
    ock::ubs::Result ret = UmqConnHelper::NewBaseUmqCreateOptions(option, RM_TP);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(option.create_flag & UMQ_CREATE_FLAG_PRIORITY, 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, NewBaseUmqCreateOptions_TpTypeSingle_NoShareTransportFlag)
{
    UmqSetting::UMQ_TP_TYPE = SINGLE;
    /* GetTpInfo 在函数内用默认参数 UMQ_UB_TRANS_MODE,置为 RC_CTP 以覆盖 CTP 映射 */
    UmqSetting::UMQ_UB_TRANS_MODE = RC_CTP;
    umq_create_option_t option{};
    ock::ubs::Result ret = UmqConnHelper::NewBaseUmqCreateOptions(option, RC_CTP);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(option.tp_mode, UMQ_TM_RC);
    EXPECT_EQ(option.tp_type, UMQ_TP_TYPE_CTP);
    EXPECT_EQ(option.create_flag & UMQ_CREATE_FLAG_SHARE_TRANSPORT, 0u);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, NewBaseUmqCreateOptions_UnsupportedTransMode_ReturnsError)
{
    /* GetTpInfo 在函数内部用默认参数 UmqSetting::UMQ_UB_TRANS_MODE,
     * 需将该静态成员置为不支持值才能触发失败路径(trans_mode 形参只进 DEBUG 日志,
     * 传 RM_TP 保证日志索引不越界) */
    UmqSetting::UMQ_UB_TRANS_MODE = TEST_TRANS_MODE_INVALID_POSITIVE;
    umq_create_option_t option{};
    ock::ubs::Result ret = UmqConnHelper::NewBaseUmqCreateOptions(option, RM_TP);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

// ==================== GetDevEid ====================

TEST_F(UmqConnHelperTest, GetDevEid_DevInfoGetFail_ReturnsError)
{
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGetFail));
    umq_eid_t eid{};
    errno = EINVAL;
    ock::ubs::Result ret = UmqConnHelper::GetDevEid(const_cast<char *>(TEST_DEV_NAME), TEST_EID_INDEX, &eid);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, GetDevEid_EidFound_ReturnsOk)
{
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGetSuccess));
    umq_eid_t eid{};
    ock::ubs::Result ret = UmqConnHelper::GetDevEid(const_cast<char *>(TEST_DEV_NAME), TEST_EID_INDEX, &eid);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(eid.raw[0], TEST_EID_RAW_BYTE);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, GetDevEid_EidNotFound_ReturnsInvalidParam)
{
    MOCKER_CPP(::umq_dev_info_get).stubs().will(invoke(&MockDevInfoGetSuccess));
    umq_eid_t eid{};
    ock::ubs::Result ret = UmqConnHelper::GetDevEid(const_cast<char *>(TEST_DEV_NAME), TEST_EID_INDEX + 1, &eid);
    EXPECT_EQ(ret, UBS_INVALID_PARAM);
    GlobalMockObject::verify();
}

// ==================== GetLeftPostRxNum ====================

TEST_F(UmqConnHelperTest, GetLeftPostRxNum_CfgGetSuccess_ReturnsFactorTimesDepth)
{
    g_mockCfgPostFactor = TEST_RXE_POST_FACTOR;
    g_mockCfgRxDepth = TEST_RX_DEPTH;
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    uint32_t ret = UmqConnHelper::GetLeftPostRxNum(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, TEST_RXE_POST_FACTOR * TEST_RX_DEPTH);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, GetLeftPostRxNum_CfgGetFail_ReturnsZero)
{
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGetFail));
    uint32_t ret = UmqConnHelper::GetLeftPostRxNum(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, 0u);
    GlobalMockObject::verify();
}

// ==================== PrefillRx ====================

TEST_F(UmqConnHelperTest, PrefillRx_LeftPostRxNumZero_ReturnsError)
{
    g_mockCfgPostFactor = 0;
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    ock::ubs::Result ret = UmqConnHelper::PrefillRx(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, PrefillRx_BatchAtLimit_SinglePostSucceeds)
{
    g_mockCfgPostFactor = 1;
    g_mockCfgRxDepth = UmqSetting::UMQ_POST_BATCH_MAX; /* 边界值 256:恰好一次批量 */
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockBufAlloc));
    MOCKER_CPP(::umq_post).expects(exactly(1)).will(returnValue(UMQ_SUCCESS));
    ock::ubs::Result ret = UmqConnHelper::PrefillRx(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, PrefillRx_BatchOverLimit_TwoPostsSucceeds)
{
    g_mockCfgPostFactor = 1;
    g_mockCfgRxDepth = UmqSetting::UMQ_POST_BATCH_MAX + 1; /* 相邻值 257:截断 256 + 余 1,两次 post */
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockBufAlloc));
    MOCKER_CPP(::umq_post).expects(exactly(2)).will(returnValue(UMQ_SUCCESS));
    ock::ubs::Result ret = UmqConnHelper::PrefillRx(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, PrefillRx_BatchBelowLimit_SinglePostSucceeds)
{
    g_mockCfgPostFactor = 1;
    g_mockCfgRxDepth = UmqSetting::UMQ_POST_BATCH_MAX - 1; /* 相邻值 255:单次批量 */
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockBufAlloc));
    MOCKER_CPP(::umq_post).expects(exactly(1)).will(returnValue(UMQ_SUCCESS));
    ock::ubs::Result ret = UmqConnHelper::PrefillRx(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, PrefillRx_BufAllocFail_ReturnsError)
{
    g_mockCfgPostFactor = 1;
    g_mockCfgRxDepth = TEST_RX_DEPTH;
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockBufAllocFail));
    ock::ubs::Result ret = UmqConnHelper::PrefillRx(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, PrefillRx_PostFailWithBadQbuf_FreeAndReturnsError)
{
    g_mockCfgPostFactor = 1;
    g_mockCfgRxDepth = TEST_RX_DEPTH;
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockBufAlloc));
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockPostWithBadQbuf));
    MOCKER_CPP(::umq_buf_free).expects(exactly(1)).will(invoke(&MockBufFree));
    errno = EINVAL;
    ock::ubs::Result ret = UmqConnHelper::PrefillRx(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, PrefillRx_PostFailBadQbufNull_ReturnsError)
{
    g_mockCfgPostFactor = 1;
    g_mockCfgRxDepth = TEST_RX_DEPTH;
    MOCKER_CPP(::umq_cfg_get).stubs().will(invoke(&MockCfgGet));
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(&MockBufAlloc));
    MOCKER_CPP(::umq_post).stubs().will(invoke(&MockPostFail));
    /* bad_qbuf==nullptr 时整条 rx_buf_list 未被 UMQ 消费，需释放整链（OE 泄漏修复契约） */
    MOCKER_CPP(::umq_buf_free).expects(exactly(1)).will(invoke(&MockBufFree));
    errno = EINVAL;
    ock::ubs::Result ret = UmqConnHelper::PrefillRx(TEST_UMQ_HANDLE);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

// ==================== GetRouteList ====================

TEST_F(UmqConnHelperTest, GetRouteList_GetTpInfoFail_ReturnsError)
{
    /* GetRouteList 内部 GetTpInfo 用默认参数 UMQ_UB_TRANS_MODE */
    UmqSetting::UMQ_UB_TRANS_MODE = TEST_TRANS_MODE_INVALID_POSITIVE;
    umq_eid_t src_eid{};
    umq_eid_t dst_eid{};
    umq_route_list_t route_list{};
    ock::ubs::Result ret = UmqConnHelper::GetRouteList(route_list, src_eid, dst_eid);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, GetRouteList_UmqGetRouteListFail_ReturnsError)
{
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteListFail));
    umq_eid_t src_eid{};
    umq_eid_t dst_eid{};
    umq_route_list_t route_list{};
    errno = EINVAL;
    ock::ubs::Result ret = UmqConnHelper::GetRouteList(route_list, src_eid, dst_eid);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, GetRouteList_EmptyRouteNum_ReturnsError)
{
    g_mockRouteNum = 0; /* 边界值:空路由 */
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    umq_eid_t src_eid{};
    umq_eid_t dst_eid{};
    umq_route_list_t route_list{};
    ock::ubs::Result ret = UmqConnHelper::GetRouteList(route_list, src_eid, dst_eid);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, GetRouteList_NonEmptyRouteNum_ReturnsOk)
{
    g_mockRouteNum = TEST_ROUTE_NUM; /* 相邻值:非空 */
    MOCKER_CPP(::umq_get_route_list).stubs().will(invoke(&MockGetRouteList));
    umq_eid_t src_eid{};
    umq_eid_t dst_eid{};
    umq_route_list_t route_list{};
    ock::ubs::Result ret = UmqConnHelper::GetRouteList(route_list, src_eid, dst_eid);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}

// ==================== RegisterSharedJfrForRead ====================

/* EpollRunnerBase::Start/AddEpollEvent 为纯虚函数,经 vtable 槽位 mock:
 * MOCKER_CPP_VIRTUAL 对 Instance() 单例对象 patch 该类型的共享 vtable,
 * RegisterSharedJfrForRead 内经基类引用调用同样被拦截(实验验证)。 */

static ock::ubs::Result RegisterSharedJfrForReadWithMockedRunner(int start_ret, int interrupt_fd, int add_epoll_ret)
{
    using ShareJfrRunner = EpollRunner<EpollRunnerType::SHARE_JFR_RX_RUNNER>;
    ShareJfrRunner &runner = ShareJfrRunner::Instance();
    MOCKER_CPP_VIRTUAL(runner, &ShareJfrRunner::Start).stubs().will(returnValue(start_ret));
    MOCKER_CPP_VIRTUAL(runner, &ShareJfrRunner::AddEpollEvent).stubs().will(returnValue(add_epoll_ret));
    MOCKER_CPP(::umq_interrupt_fd_get).stubs().will(returnValue(interrupt_fd));
    return UmqConnHelper::RegisterSharedJfrForRead(TEST_UMQ_HANDLE);
}

TEST_F(UmqConnHelperTest, RegisterSharedJfrForRead_StartFail_ReturnsStartResult)
{
    ock::ubs::Result ret = RegisterSharedJfrForReadWithMockedRunner(-1, 0, 0);
    EXPECT_NE(ret, UBS_OK);
    EXPECT_EQ(static_cast<int32_t>(ret), -1);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, RegisterSharedJfrForRead_InterruptFdGetFail_ReturnsError)
{
    errno = EINVAL;
    ock::ubs::Result ret = RegisterSharedJfrForReadWithMockedRunner(0, UMQ_FAIL, 0);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, RegisterSharedJfrForRead_AddEpollEventFail_ReturnsError)
{
    ock::ubs::Result ret = RegisterSharedJfrForReadWithMockedRunner(0, 0, -1);
    EXPECT_EQ(ret, UBS_ERROR);
    GlobalMockObject::verify();
}

TEST_F(UmqConnHelperTest, RegisterSharedJfrForRead_Success_ReturnsOk)
{
    ock::ubs::Result ret = RegisterSharedJfrForReadWithMockedRunner(0, 0, 0);
    EXPECT_EQ(ret, UBS_OK);
    GlobalMockObject::verify();
}
