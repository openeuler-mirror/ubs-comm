/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 */

#include "umq_errno_converter.h"
#include "umq_errno.h"

#include <gtest/gtest.h>
#include <cstring>

using namespace ock::ubs::umq;

namespace {
static const int UMQ_CUSTOM_ERR_1 = 0xFFFF;
static const umq_buf_status_t UMQ_BUF_CUSTOM_ERR = static_cast<umq_buf_status_t>(257);
static const int UMQ_INVALID_OPERATION = 999;
}

class UmqErrnoConverterTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

// ==================== Convert - Connect ====================

TEST_F(UmqErrnoConverterTest, ConvertConnect_Success)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, UMQ_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Eperm)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_EPERM), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Eagain)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_EAGAIN), EAGAIN);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Enomem)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_ENOMEM), ENOMEM);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Ebusy)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_EBUSY), EBUSY);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Eexist)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_EEXIST), EEXIST);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Einval)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_EINVAL), EINVAL);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Enodev)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_ENODEV), ENODEV);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Enosr)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_ENOSR), ENOSR);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Etimeout)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_ETIMEOUT), ETIMEDOUT);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Einprogress)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_EINPROGRESS), EINPROGRESS);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_EtsegNonImported)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_ETSEG_NON_IMPORTED), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_Eflowctl)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_ERR_EFLOWCTL), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_UnknownError)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -UMQ_CUSTOM_ERR_1), EIO);
}

// ==================== Convert - Accept ====================

TEST_F(UmqErrnoConverterTest, ConvertAccept_Success)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::ACCEPT, UMQ_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertAccept_Eperm)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::ACCEPT, -UMQ_ERR_EPERM), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertAccept_Eagain)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::ACCEPT, -UMQ_ERR_EAGAIN), EAGAIN);
}

TEST_F(UmqErrnoConverterTest, ConvertAccept_Etimeout)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::ACCEPT, -UMQ_ERR_ETIMEOUT), ETIMEDOUT);
}

TEST_F(UmqErrnoConverterTest, ConvertAccept_Eflowctl)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::ACCEPT, -UMQ_ERR_EFLOWCTL), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertAccept_UnknownError)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::ACCEPT, -UMQ_CUSTOM_ERR_1), EIO);
}

// ==================== Convert - Writev ====================

TEST_F(UmqErrnoConverterTest, ConvertWritev_Success)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::WRITEV, UMQ_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertWritev_Eperm)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::WRITEV, -UMQ_ERR_EPERM), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertWritev_Eagain)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::WRITEV, -UMQ_ERR_EAGAIN), EAGAIN);
}

TEST_F(UmqErrnoConverterTest, ConvertWritev_Eflowctl)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::WRITEV, -UMQ_ERR_EFLOWCTL), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertWritev_UnknownError)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::WRITEV, -UMQ_CUSTOM_ERR_1), EIO);
}

// ==================== Convert - Readv ====================

TEST_F(UmqErrnoConverterTest, ConvertReadv_Success)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::READV, UMQ_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertReadv_Eperm)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::READV, -UMQ_ERR_EPERM), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertReadv_Eagain)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::READV, -UMQ_ERR_EAGAIN), EAGAIN);
}

TEST_F(UmqErrnoConverterTest, ConvertReadv_Eflowctl)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::READV, -UMQ_ERR_EFLOWCTL), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertReadv_UnknownError)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::READV, -UMQ_CUSTOM_ERR_1), EIO);
}

// ==================== ConvertBufStatus - Connect ====================

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_Success)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_UnsupportedOpcode)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_UNSUPPORTED_OPCODE_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_LocLenErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_LOC_LEN_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_LocOperationErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_LOC_OPERATION_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_LocAccessErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_LOC_ACCESS_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_RemRespLenErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_REM_RESP_LEN_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_RemUnsupportedReqErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_REM_UNSUPPORTED_REQ_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_RemOperationErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_REM_OPERATION_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_RemAccessAbortErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_REM_ACCESS_ABORT_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_AckTimeoutErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_ACK_TIMEOUT_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_RnrRetryCntExcErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_RNR_RETRY_CNT_EXC_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_WrFlushErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_WR_FLUSH_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_WrSuspendDone)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_WR_SUSPEND_DONE), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_WrFlushErrDone)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_WR_FLUSH_ERR_DONE), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_WrUnhandled)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_WR_UNHANDLED), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_LocDataPoison)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_LOC_DATA_POISON), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_RemDataPoison)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_REM_DATA_POISON), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusConnect_UnknownErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::CONNECT, UMQ_BUF_CUSTOM_ERR), EIO);
}

// ==================== ConvertBufStatus - Accept ====================

TEST_F(UmqErrnoConverterTest, ConvertBufStatusAccept_Success)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::ACCEPT, UMQ_BUF_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusAccept_RemAccessAbortErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::ACCEPT, UMQ_BUF_REM_ACCESS_ABORT_ERR), EIO);
}

// ==================== ConvertBufStatus - Writev ====================

TEST_F(UmqErrnoConverterTest, ConvertBufStatusWritev_Success)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, UMQ_BUF_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusWritev_RemOperationErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, UMQ_BUF_REM_OPERATION_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusWritev_RemAccessAbortErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, UMQ_BUF_REM_ACCESS_ABORT_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusWritev_WrFlushErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, UMQ_BUF_WR_FLUSH_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusWritev_FlowControlUpdate)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, UMQ_BUF_FLOW_CONTROL_UPDATE), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusWritev_FakeBufFcUpdate)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, UMQ_FAKE_BUF_FC_UPDATE), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusWritev_FakeBufFcErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, UMQ_FAKE_BUF_FC_ERR), EIO);
}

// ==================== ConvertBufStatus - Readv ====================

TEST_F(UmqErrnoConverterTest, ConvertBufStatusReadv_Success)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, UMQ_BUF_SUCCESS), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusReadv_RemOperationErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, UMQ_BUF_REM_OPERATION_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusReadv_RemAccessAbortErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, UMQ_BUF_REM_ACCESS_ABORT_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusReadv_WrFlushErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, UMQ_BUF_WR_FLUSH_ERR), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusReadv_FlowControlUpdate)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, UMQ_BUF_FLOW_CONTROL_UPDATE), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusReadv_FakeBufFcUpdate)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, UMQ_FAKE_BUF_FC_UPDATE), 0);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatusReadv_FakeBufFcErr)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(UmqOperation::READV, UMQ_FAKE_BUF_FC_ERR), EIO);
}

// ==================== Invalid Operation ====================

TEST_F(UmqErrnoConverterTest, Convert_InvalidOperation)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(static_cast<UmqOperation>(UMQ_INVALID_OPERATION), -1), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertBufStatus_InvalidOperation)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertBufStatus(static_cast<UmqOperation>(UMQ_INVALID_OPERATION),
        UMQ_BUF_SUCCESS), EIO);
}

// ==================== GetErrorDescription ====================

TEST_F(UmqErrnoConverterTest, GetErrorDescription_Success)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, UMQ_SUCCESS);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Success");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_ConnectEperm)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, -UMQ_ERR_EPERM);
    ASSERT_NE(desc, nullptr);
    EXPECT_STRNE(desc, "Unknown error");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_ConnectEagain)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, -UMQ_ERR_EAGAIN);
    ASSERT_NE(desc, nullptr);
    EXPECT_STRNE(desc, "Unknown error");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_ConnectEnomem)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, -UMQ_ERR_ENOMEM);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Out of memory");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_ConnectEtimeout)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, -UMQ_ERR_ETIMEOUT);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Connection timed out");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_ConnectEinprogress)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, -UMQ_ERR_EINPROGRESS);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Operation now in progress");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_ConnectEtsegNonImported)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, -UMQ_ERR_ETSEG_NON_IMPORTED);
    ASSERT_NE(desc, nullptr);
    EXPECT_STRNE(desc, "Unknown error");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_UnknownError)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(UmqOperation::CONNECT, -UMQ_CUSTOM_ERR_1);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Unknown error");
}

TEST_F(UmqErrnoConverterTest, GetErrorDescription_InvalidOperation)
{
    const char* desc = UmqErrnoConverter::GetErrorDescription(static_cast<UmqOperation>(UMQ_INVALID_OPERATION),
        UMQ_SUCCESS);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Success");
}

// ==================== GetBufStatusDescription ====================

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_ConnectSuccess)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(UmqOperation::CONNECT, UMQ_BUF_SUCCESS);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Buffer operation success");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_ConnectUnsupportedOpcode)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::CONNECT, UMQ_BUF_UNSUPPORTED_OPCODE_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STRNE(desc, "Unknown buffer status");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_ConnectLocLenErr)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::CONNECT, UMQ_BUF_LOC_LEN_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Message too long");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_ConnectRemAccessAbortErr)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::CONNECT, UMQ_BUF_REM_ACCESS_ABORT_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Connection reset by peer");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_ConnectAckTimeoutErr)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::CONNECT, UMQ_BUF_ACK_TIMEOUT_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Acknowledgement timeout");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_WritevRemOperationErr)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::WRITEV, UMQ_BUF_REM_OPERATION_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Broken pipe");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_WritevWrFlushErr)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::WRITEV, UMQ_BUF_WR_FLUSH_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Write flush error");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_ReadvRemOperationErr)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::READV, UMQ_BUF_REM_OPERATION_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Connection reset by peer");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_ReadvWrFlushErr)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::READV, UMQ_BUF_WR_FLUSH_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Connection reset by peer, write flush error");
}

TEST_F(UmqErrnoConverterTest, GetBufStatusDescription_UnknownBufStatus)
{
    const char* desc = UmqErrnoConverter::GetBufStatusDescription(
        UmqOperation::CONNECT, UMQ_BUF_CUSTOM_ERR);
    ASSERT_NE(desc, nullptr);
    EXPECT_STREQ(desc, "Unknown buffer status");
}

// ==================== Convert with savedErrno - UMQ_FAIL override ====================

TEST_F(UmqErrnoConverterTest, ConvertConnect_FailWithSavedErrnoEinval)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -1, EINVAL), EINVAL);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_FailWithSavedErrnoEnodev)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -1, ENODEV), ENODEV);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_FailWithSavedErrnoEnomem)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -1, ENOMEM), ENOMEM);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_FailWithSavedErrnoEnoexec)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -1, ENOEXEC), ENOEXEC);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_FailWithSavedErrnoEio)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -1, EIO), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_FailWithSavedErrnoEagain_NoOverride)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -1, EAGAIN), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_FailWithSavedErrnoZero_NoOverride)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(UmqOperation::CONNECT, -1, 0), EIO);
}

// ==================== Convert with savedErrno - ENODEV override ====================

TEST_F(UmqErrnoConverterTest, ConvertConnect_EnodevWithSavedErrnoEinval)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(
        UmqOperation::CONNECT, -UMQ_ERR_ENODEV, EINVAL), EINVAL);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_EnodevWithSavedErrnoEio)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(
        UmqOperation::CONNECT, -UMQ_ERR_ENODEV, EIO), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertConnect_EnodevWithSavedErrnoEnomem_NoOverride)
{
    EXPECT_EQ(UmqErrnoConverter::Convert(
        UmqOperation::CONNECT, -UMQ_ERR_ENODEV, ENOMEM), ENODEV);
}

// ==================== ConvertHandleResult - CREATE ====================

TEST_F(UmqErrnoConverterTest, ConvertHandleResultCreate_ErrnoEinval)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(UmqOperation::CREATE, EINVAL), EINVAL);
}

TEST_F(UmqErrnoConverterTest, ConvertHandleResultCreate_ErrnoEperm)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(UmqOperation::CREATE, EPERM), EPERM);
}

TEST_F(UmqErrnoConverterTest, ConvertHandleResultCreate_ErrnoEnomem)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(UmqOperation::CREATE, ENOMEM), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertHandleResultCreate_ErrnoZero)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(UmqOperation::CREATE, 0), EIO);
}

// ==================== ConvertHandleResult - BIND_INFO_GET ====================

TEST_F(UmqErrnoConverterTest, ConvertHandleResultBindInfoGet_ErrnoEinval)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(
        UmqOperation::BIND_INFO_GET, EINVAL), EINVAL);
}

TEST_F(UmqErrnoConverterTest, ConvertHandleResultBindInfoGet_ErrnoEnomem)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(
        UmqOperation::BIND_INFO_GET, ENOMEM), ENOMEM);
}

TEST_F(UmqErrnoConverterTest, ConvertHandleResultBindInfoGet_ErrnoEbusy)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(
        UmqOperation::BIND_INFO_GET, EBUSY), EIO);
}

TEST_F(UmqErrnoConverterTest, ConvertHandleResultBindInfoGet_ErrnoZero)
{
    EXPECT_EQ(UmqErrnoConverter::ConvertHandleResult(UmqOperation::BIND_INFO_GET, 0), EIO);
}