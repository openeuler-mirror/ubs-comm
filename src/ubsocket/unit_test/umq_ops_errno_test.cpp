/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_data_rx_ops.h"
#include "umq_data_tx_ops.h"
#include "umq_errno_converter.h"
#include "under_api/dl_umq_api.h"
#include "core/ubsocket_socket_set.h"
#include "core/ubsocket_core_types.h"
#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_errno.h"
#include "common/ubsocket_defines.h"
#include "common/ubsocket_lock.h"

#include <mockcpp/mockcpp.hpp>
#include <gtest/gtest.h>
#include <cerrno>
#include <securec.h>
#include <atomic>

using namespace ock::ubs;
using namespace ock::ubs::umq;

namespace {
constexpr int TEST_FD = 10;
constexpr uint64_t TEST_UMQ_HANDLE = 12345ULL;
constexpr uint32_t TEST_BUF_DATA_SIZE = 8192;
constexpr uint32_t TEST_BUF_SIZE = 64;
constexpr int TEST_INTERRUPT_FD = 42;
constexpr uint32_t TEST_AVAIL_NUM = 10;
constexpr uint32_t TEST_DEPTH = 256;

umq_buf_t *AllocMockBuf(uint32_t size, umq_buf_status_t status = UMQ_BUF_SUCCESS)
{
    static uint8_t bufData[TEST_BUF_DATA_SIZE];
    static umq_buf_pro_t bufPro;
    static umq_buf_t mockBuf;

    memset_s(&bufPro, sizeof(umq_buf_pro_t), 0, sizeof(umq_buf_pro_t));
    bufPro.opcode = UMQ_OPC_SEND;

    memset_s(&mockBuf, sizeof(umq_buf_t), 0, sizeof(umq_buf_t));
    mockBuf.buf_data = reinterpret_cast<char *>(bufData);  // NOLINT(G.STD.02)
    mockBuf.data_size = size;
    mockBuf.total_data_size = size;
    mockBuf.status = status;
    memcpy_s(mockBuf.qbuf_ext, sizeof(mockBuf.qbuf_ext), &bufPro, sizeof(umq_buf_pro_t));
    mockBuf.qbuf_next = nullptr;
    mockBuf.io_direction = UMQ_IO_RX;

    return &mockBuf;
}
}

class UmqOpsErrnoTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        SocketSet::Instance().Init();

        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_RX_DEPTH = TEST_DEPTH;
        GlobalSetting::UBS_TX_DEPTH = TEST_DEPTH;
        GlobalSetting::UBS_TRACE_ENABLED = false;
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        SocketSet::Instance().ReleaseAll();
    }
};

// ==================== UmqRxOps::RearmRxInterrupt ====================

TEST_F(UmqOpsErrnoTest, RearmRxInterrupt_FailEpermSavedEinval_MapsEinval)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EINVAL;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_EPERM);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, RearmRxInterrupt_FailEagain_MapsEagain)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EAGAIN));
    errno = 0;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_EAGAIN);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, RearmRxInterrupt_FailEpermSavedZero_MapsEio)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = 0;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_EPERM);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, RearmRxInterrupt_FailEnodevSavedEio_Override)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_ENODEV));
    errno = EIO;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, RearmRxInterrupt_FailEnodevNoOverride_MapsEnodev)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_ENODEV));
    errno = ENOMEM;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(errno, ENODEV);
    GlobalMockObject::verify();
}

// ==================== UmqRxOps::GetAndAckEvent (RX) ====================

TEST_F(UmqOpsErrnoTest, RxGetAndAckEvent_FailEpermSavedEinval_MapsEinval)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EINVAL;
    int ret = rxOps.GetAndAckEvent();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, RxGetAndAckEvent_FailEagain_MapsEagain)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EAGAIN));
    errno = 0;
    int ret = rxOps.GetAndAckEvent();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, RxGetAndAckEvent_FailEinval_MapsEinval)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EINVAL));
    errno = 0;
    int ret = rxOps.GetAndAckEvent();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, RxGetAndAckEvent_FailEpermSavedEagain_NoOverrideMapsEio)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EAGAIN;
    int ret = rxOps.GetAndAckEvent();
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

// ==================== UmqTxOps::DpRearmTxInterrupt ====================

TEST_F(UmqOpsErrnoTest, DpRearmTxInterrupt_SuccessSetsEagain)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(0));
    errno = 0;
    int ret = txOps.DpRearmTxInterrupt();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, DpRearmTxInterrupt_FailEpermSavedEinval_MapsEinval)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EINVAL;
    int ret = txOps.DpRearmTxInterrupt();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, DpRearmTxInterrupt_FailEagain_MapsEagain)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EAGAIN));
    errno = 0;
    int ret = txOps.DpRearmTxInterrupt();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, DpRearmTxInterrupt_FailEpermSavedZero_MapsEio)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = 0;
    int ret = txOps.DpRearmTxInterrupt();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, DpRearmTxInterrupt_FailEnodevSavedEio_Override)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_ENODEV));
    errno = EIO;
    int ret = txOps.DpRearmTxInterrupt();
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

// ==================== UmqTxOps::GetAndAckEvent (TX) ====================

TEST_F(UmqOpsErrnoTest, TxGetAndAckEvent_FailEpermSavedEinval_MapsEinval)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EINVAL;
    int ret = txOps.GetAndAckEvent();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, TxGetAndAckEvent_FailEagain_MapsEagain)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EAGAIN));
    errno = 0;
    int ret = txOps.GetAndAckEvent();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, TxGetAndAckEvent_FailEinval_MapsEinval)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EINVAL));
    errno = 0;
    int ret = txOps.GetAndAckEvent();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, TxGetAndAckEvent_FailEpermSavedEagain_NoOverrideMapsEio)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_get_cq_event)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EAGAIN;
    int ret = txOps.GetAndAckEvent();
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

// ==================== Writev/Readv EEXIST/EINPROGRESS mapping ====================

TEST_F(UmqOpsErrnoTest, DpRearmTxInterrupt_FailEexist_MapsEexist)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EEXIST));
    errno = 0;
    int ret = txOps.DpRearmTxInterrupt();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EEXIST);
    GlobalMockObject::verify();
}

 TEST_F(UmqOpsErrnoTest, DpRearmTxInterrupt_FailEinprogress_MapsEinprogress)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EINPROGRESS));
    errno = 0;
    int ret = txOps.DpRearmTxInterrupt();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINPROGRESS);
    GlobalMockObject::verify();
}

 TEST_F(UmqOpsErrnoTest, RearmRxInterrupt_FailEexist_MapsEexist)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EEXIST));
    errno = 0;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_EEXIST);
    EXPECT_EQ(errno, EEXIST);
    GlobalMockObject::verify();
}

 TEST_F(UmqOpsErrnoTest, RearmRxInterrupt_FailEinprogress_MapsEinprogress)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    MOCKER_CPP(::umq_rearm_interrupt)
        .stubs()
        .will(returnValue(-UMQ_ERR_EINPROGRESS));
    errno = 0;
    int ret = rxOps.RearmRxInterrupt();
    EXPECT_EQ(ret, -UMQ_ERR_EINPROGRESS);
    EXPECT_EQ(errno, EINPROGRESS);
    GlobalMockObject::verify();
}