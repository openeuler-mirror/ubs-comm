/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_data_rx_ops.h"
#include "umq_data_tx_ops.h"
#include "umq_backend.h"
#include "umq_setting.h"
#include "umq_socket.h"
#include "umq_socket_acceptor.h"
#include "umq_errno_converter.h"
#include "umq_eid_table.h"
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
#include <cstring>
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
constexpr uint32_t TEST_EID_INDEX = 0;
constexpr uint64_t TEST_IO_TOTAL_SIZE_MB = 64;
constexpr uint32_t TEST_SOCK_DEPTH = 256;
constexpr int TEST_PRESERVED_ERRNO = 9999;

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

umq_eid_t MakeTestEid(uint8_t val)
{
    umq_eid_t eid = {};
    memset_s(eid.raw, sizeof(eid.raw), val, sizeof(eid.raw));
    return eid;
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

class UmqBackendErrnoTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        UmqBackend::UMQ_INITED = false;
        UmqSetting::UMQ_DEV_NAME = "ubdev0";
        UmqSetting::UMQ_EID_INDEX = TEST_EID_INDEX;
        UmqSetting::UMQ_IO_TOTAL_SIZE_MB = TEST_IO_TOTAL_SIZE_MB;
        LockRegistry::RegisterDefaultOps();
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        UmqSetting::UMQ_DEV_NAME = "";
        UmqBackend::UMQ_INITED = false;
    }
};

class UmqSocketErrnoTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        SocketSet::Instance().Init();

        GlobalSetting::UBS_ENABLE_SHARE_JFR = false;
        GlobalSetting::UBS_RX_DEPTH = TEST_SOCK_DEPTH;
        GlobalSetting::UBS_TX_DEPTH = TEST_SOCK_DEPTH;
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

// ==================== UmqRxOps::UmqPollAndRefillRx ====================

TEST_F(UmqOpsErrnoTest, PollRxFail_EpermSavedEinval_MapsEinval)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf[POLL_BATCH_MAX] = {};
    MOCKER_CPP(::umq_poll)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = EINVAL;
    int ret = rxOps.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, PollRxFail_Eagain_MapsEagain)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf[POLL_BATCH_MAX] = {};
    MOCKER_CPP(::umq_poll)
        .stubs()
        .will(returnValue(-UMQ_ERR_EAGAIN));
    errno = 0;
    int ret = rxOps.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EAGAIN);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, PollRxFail_EpermSavedZero_MapsEio)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf[POLL_BATCH_MAX] = {};
    MOCKER_CPP(::umq_poll)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = 0;
    int ret = rxOps.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, PollRxFail_EnodevSavedEio_Override)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf[POLL_BATCH_MAX] = {};
    MOCKER_CPP(::umq_poll)
        .stubs()
        .will(returnValue(-UMQ_ERR_ENODEV));
    errno = EIO;
    int ret = rxOps.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqOpsErrnoTest, PollRxZeroRxQueueEmpty_NoConvert)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    rxOps.rx_queue_avail_num_ = 0;
    umq_buf_t *buf[POLL_BATCH_MAX] = {};
    MOCKER_CPP(::umq_poll)
        .stubs()
        .will(returnValue(0));
    errno = TEST_PRESERVED_ERRNO;
    int ret = rxOps.UmqPollAndRefillRx(buf, POLL_BATCH_MAX);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, TEST_PRESERVED_ERRNO);
    GlobalMockObject::verify();
}

// ==================== UmqRxOps::HandleErrorRxCqe ====================

TEST_F(UmqOpsErrnoTest, HandleErrorRxCqe_RemOperationErr_NoErrnoChange)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf = AllocMockBuf(TEST_BUF_SIZE, UMQ_BUF_REM_OPERATION_ERR);
    errno = 0;
    rxOps.HandleErrorRxCqe(buf);
    EXPECT_EQ(errno, 0);
}

TEST_F(UmqOpsErrnoTest, HandleErrorRxCqe_RemAccessAbortErr_NoErrnoChange)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf = AllocMockBuf(TEST_BUF_SIZE, UMQ_BUF_REM_ACCESS_ABORT_ERR);
    errno = 0;
    rxOps.HandleErrorRxCqe(buf);
    EXPECT_EQ(errno, 0);
}

TEST_F(UmqOpsErrnoTest, HandleErrorRxCqe_WrFlushErr_NoErrnoChange)
{
    UmqRxOps rxOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf = AllocMockBuf(TEST_BUF_SIZE, UMQ_BUF_WR_FLUSH_ERR);
    errno = 0;
    rxOps.HandleErrorRxCqe(buf);
    EXPECT_EQ(errno, 0);
}

// ==================== UmqTxOps::HandleErrorTxCqe ====================

TEST_F(UmqOpsErrnoTest, HandleErrorTxCqe_RemOperationErr_NoErrnoChange)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf = AllocMockBuf(TEST_BUF_SIZE, UMQ_BUF_REM_OPERATION_ERR);
    errno = 0;
    txOps.HandleErrorTxCqe(buf);
    EXPECT_EQ(errno, 0);
}

TEST_F(UmqOpsErrnoTest, HandleErrorTxCqe_RemAccessAbortErr_NoErrnoChange)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf = AllocMockBuf(TEST_BUF_SIZE, UMQ_BUF_REM_ACCESS_ABORT_ERR);
    errno = 0;
    txOps.HandleErrorTxCqe(buf);
    EXPECT_EQ(errno, 0);
}

TEST_F(UmqOpsErrnoTest, HandleErrorTxCqe_FakeBufFcErr_NoErrnoChange)
{
    UmqTxOps txOps(TEST_FD, TEST_UMQ_HANDLE);
    umq_buf_t *buf = AllocMockBuf(TEST_BUF_SIZE, UMQ_FAKE_BUF_FC_ERR);
    errno = 0;
    txOps.HandleErrorTxCqe(buf);
    EXPECT_EQ(errno, 0);
}

// ==================== UmqBackend::AddUbDev ====================

TEST_F(UmqBackendErrnoTest, AddUbDev_SuccessReturnsOk)
{
    umq_trans_info_t trans_info = {};
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(0));
    errno = 0;
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(errno, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendErrnoTest, AddUbDev_EexistSkippedReturnsOk)
{
    umq_trans_info_t trans_info = {};
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(-UMQ_ERR_EEXIST));
    errno = 0;
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(errno, 0);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendErrnoTest, AddUbDev_FailSavedZero_MapsEio)
{
    umq_trans_info_t trans_info = {};
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = 0;
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendErrnoTest, AddUbDev_FailSavedEinval_MapsEinval)
{
    umq_trans_info_t trans_info = {};
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(-1));
    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendErrnoTest, AddUbDev_EnodevSavedEio_Override)
{
    umq_trans_info_t trans_info = {};
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(-UMQ_ERR_ENODEV));
    errno = EIO;
    ock::ubs::Result ret = UmqBackend::AddUbDev(trans_info);
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

// ==================== UmqBackend::FindDevName ====================

TEST_F(UmqBackendErrnoTest, FindDevName_Nullptr_MapsEio)
{
    UmqSetting::UMQ_DEV_NAME = "";
    MOCKER_CPP(::umq_dev_info_list_get)
        .stubs()
        .will(returnValue(static_cast<umq_dev_info_t *>(nullptr)));
    errno = 0;
    ock::ubs::Result ret = UmqBackend::FindDevName();
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqBackendErrnoTest, FindDevName_NullptrSavedEinval_MapsEinval)
{
    UmqSetting::UMQ_DEV_NAME = "";
    MOCKER_CPP(::umq_dev_info_list_get)
        .stubs()
        .will(returnValue(static_cast<umq_dev_info_t *>(nullptr)));
    errno = EINVAL;
    ock::ubs::Result ret = UmqBackend::FindDevName();
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

// ==================== UmqSocket::GetTxFd ====================

TEST_F(UmqSocketErrnoTest, GetTxFd_FailSavedZero_MapsEio)
{
    UmqSocket sock(TEST_FD);
    sock.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = 0;
    int ret = sock.GetTxFd();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketErrnoTest, GetTxFd_FailSavedEinval_MapsEinval)
{
    UmqSocket sock(TEST_FD);
    sock.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get)
        .stubs()
        .will(returnValue(-1));
    errno = EINVAL;
    int ret = sock.GetTxFd();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketErrnoTest, GetTxFd_EnodevSavedEio_Override)
{
    UmqSocket sock(TEST_FD);
    sock.umq_handle_ = TEST_UMQ_HANDLE;
    MOCKER_CPP(::umq_interrupt_fd_get)
        .stubs()
        .will(returnValue(-UMQ_ERR_ENODEV));
    errno = EIO;
    int ret = sock.GetTxFd();
    EXPECT_EQ(ret, -1);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

// ==================== UmqSocket::GetDevEid ====================

TEST_F(UmqSocketErrnoTest, GetDevEid_FailSavedZero_MapsEio)
{
    UmqSocket sock(TEST_FD);
    sock.umq_handle_ = TEST_UMQ_HANDLE;
    char dev_name[32] = "test_dev";
    umq_eid_t eid = {};
    MOCKER_CPP(::umq_dev_info_get)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = 0;
    ock::ubs::Result ret = sock.GetDevEid(dev_name, 0, &eid);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketErrnoTest, GetDevEid_FailSavedEinval_MapsEinval)
{
    UmqSocket sock(TEST_FD);
    sock.umq_handle_ = TEST_UMQ_HANDLE;
    char dev_name[32] = "test_dev";
    umq_eid_t eid = {};
    MOCKER_CPP(::umq_dev_info_get)
        .stubs()
        .will(returnValue(-1));
    errno = EINVAL;
    ock::ubs::Result ret = sock.GetDevEid(dev_name, 0, &eid);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketErrnoTest, GetDevEid_EnodevSavedEio_Override)
{
    UmqSocket sock(TEST_FD);
    sock.umq_handle_ = TEST_UMQ_HANDLE;
    char dev_name[32] = "test_dev";
    umq_eid_t eid = {};
    MOCKER_CPP(::umq_dev_info_get)
        .stubs()
        .will(returnValue(-UMQ_ERR_ENODEV));
    errno = EIO;
    ock::ubs::Result ret = sock.GetDevEid(dev_name, 0, &eid);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

// ==================== UmqAcceptorOps::CheckDevAdd ====================

TEST_F(UmqSocketErrnoTest, CheckDevAdd_AlreadyRegisteredReturnsOk)
{
    UmqAcceptorOps acceptor(TEST_FD);
    umq_eid_t testEid = MakeTestEid(0xAA);
    EidRegistry::Instance().RegisterEid(testEid);
    errno = 0;
    ock::ubs::Result ret = acceptor.CheckDevAdd(testEid);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(errno, 0);
    EidRegistry::Instance().UnregisterEid(testEid);
}

TEST_F(UmqSocketErrnoTest, CheckDevAdd_SuccessReturnsOk)
{
    UmqAcceptorOps acceptor(TEST_FD);
    umq_eid_t testEid = MakeTestEid(0xBB);
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(0));
    errno = 0;
    ock::ubs::Result ret = acceptor.CheckDevAdd(testEid);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(errno, 0);
    EidRegistry::Instance().UnregisterEid(testEid);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketErrnoTest, CheckDevAdd_EexistSkippedReturnsOk)
{
    UmqAcceptorOps acceptor(TEST_FD);
    umq_eid_t testEid = MakeTestEid(0xCC);
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(-UMQ_ERR_EEXIST));
    errno = 0;
    ock::ubs::Result ret = acceptor.CheckDevAdd(testEid);
    EXPECT_EQ(ret, UBS_OK);
    EXPECT_EQ(errno, 0);
    EidRegistry::Instance().UnregisterEid(testEid);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketErrnoTest, CheckDevAdd_FailSavedZero_MapsEio)
{
    UmqAcceptorOps acceptor(TEST_FD);
    umq_eid_t testEid = MakeTestEid(0xDD);
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(-UMQ_ERR_EPERM));
    errno = 0;
    ock::ubs::Result ret = acceptor.CheckDevAdd(testEid);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EIO);
    GlobalMockObject::verify();
}

TEST_F(UmqSocketErrnoTest, CheckDevAdd_FailSavedEinval_MapsEinval)
{
    UmqAcceptorOps acceptor(TEST_FD);
    umq_eid_t testEid = MakeTestEid(0xEE);
    MOCKER_CPP(::umq_dev_add)
        .stubs()
        .will(returnValue(-1));
    errno = EINVAL;
    ock::ubs::Result ret = acceptor.CheckDevAdd(testEid);
    EXPECT_EQ(ret, UBS_ERROR);
    EXPECT_EQ(errno, EINVAL);
    GlobalMockObject::verify();
}