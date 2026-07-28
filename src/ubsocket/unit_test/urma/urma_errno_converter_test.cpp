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
#include <gtest/gtest.h>
#include <cerrno>

#include "core/urma/urma_errno_converter.h"

using namespace ock::ubs::urma;

class UrmaErrnoConverterTest : public ::testing::Test {
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

TEST_F(UrmaErrnoConverterTest, Convert_Success_ReturnsZero)
{
    EXPECT_EQ(0, UrmaErrnoConverter::Convert(URMA_SUCCESS, URMA_OP_WRITEV));
    EXPECT_EQ(0, UrmaErrnoConverter::Convert(URMA_SUCCESS, URMA_OP_READV));
    EXPECT_EQ(0, UrmaErrnoConverter::Convert(URMA_SUCCESS, URMA_OP_CONNECT));
    EXPECT_EQ(0, UrmaErrnoConverter::Convert(URMA_SUCCESS, URMA_OP_ACCEPT));
}

TEST_F(UrmaErrnoConverterTest, Convert_EAGAIN_ReturnsEAGAIN)
{
    EXPECT_EQ(EAGAIN, UrmaErrnoConverter::Convert(URMA_EAGAIN, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, Convert_ENOMEM_ReturnsENOMEM)
{
    EXPECT_EQ(ENOMEM, UrmaErrnoConverter::Convert(URMA_ENOMEM, URMA_OP_READV));
}

TEST_F(UrmaErrnoConverterTest, Convert_EINVAL_ReturnsEINVAL)
{
    EXPECT_EQ(EINVAL, UrmaErrnoConverter::Convert(URMA_EINVAL, URMA_OP_CREATE_CTX));
}

TEST_F(UrmaErrnoConverterTest, Convert_UnknownStatus_WritevReturnsEIO)
{
    EXPECT_EQ(EIO, UrmaErrnoConverter::Convert(-9999, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, Convert_UnknownStatus_ReadvReturnsEIO)
{
    EXPECT_EQ(EIO, UrmaErrnoConverter::Convert(-9999, URMA_OP_READV));
}

TEST_F(UrmaErrnoConverterTest, Convert_UnknownStatus_ConnectReturnsConnrefused)
{
    EXPECT_EQ(ECONNREFUSED, UrmaErrnoConverter::Convert(-9999, URMA_OP_CONNECT));
    EXPECT_EQ(ECONNREFUSED, UrmaErrnoConverter::Convert(-9999, URMA_OP_ACCEPT));
}

TEST_F(UrmaErrnoConverterTest, Convert_UnknownStatus_CreateCtxReturnsEnodev)
{
    EXPECT_EQ(ENODEV, UrmaErrnoConverter::Convert(-9999, URMA_OP_CREATE_CTX));
    EXPECT_EQ(ENODEV, UrmaErrnoConverter::Convert(-9999, URMA_OP_CREATE_JETTY));
}

TEST_F(UrmaErrnoConverterTest, Convert_UnknownStatus_ImportReturnsConnrefused)
{
    EXPECT_EQ(ECONNREFUSED, UrmaErrnoConverter::Convert(-9999, URMA_OP_IMPORT_JETTY));
    EXPECT_EQ(ECONNREFUSED, UrmaErrnoConverter::Convert(-9999, URMA_OP_BIND));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_Success_ReturnsZero)
{
    EXPECT_EQ(0, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_SUCCESS, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_RnrRetryExc_ReturnsEAGAIN)
{
    EXPECT_EQ(EAGAIN, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_RNR_RETRY_CNT_EXC_ERR, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_AckTimeout_ReturnsETIMEDOUT)
{
    EXPECT_EQ(ETIMEDOUT, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_ACK_TIMEOUT_ERR, URMA_OP_READV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_AccessError_ReturnsEACCES)
{
    EXPECT_EQ(EACCES, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_LOC_ACCESS_ERR, URMA_OP_WRITEV));
    EXPECT_EQ(EACCES, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_REM_ACCESS_ABORT_ERR, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_LenError_ReturnsEMSGSIZE)
{
    EXPECT_EQ(EMSGSIZE, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_LOC_LEN_ERR, URMA_OP_WRITEV));
    EXPECT_EQ(EMSGSIZE, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_REM_RESP_LEN_ERR, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_OperationError_ReturnsEIO)
{
    EXPECT_EQ(EIO, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_LOC_OPERATION_ERR, URMA_OP_WRITEV));
    EXPECT_EQ(EIO, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_REM_OPERATION_ERR, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_FlushError_ReturnsENETRESET)
{
    EXPECT_EQ(ENETRESET, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_WR_FLUSH_ERR, URMA_OP_WRITEV));
    EXPECT_EQ(ENETRESET, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_WR_FLUSH_ERR_DONE, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_DataPoison_ReturnsEIO)
{
    EXPECT_EQ(EIO, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_LOC_DATA_POISON, URMA_OP_WRITEV));
    EXPECT_EQ(EIO, UrmaErrnoConverter::ConvertCrStatus(URMA_CR_REM_DATA_POISON, URMA_OP_WRITEV));
}

TEST_F(UrmaErrnoConverterTest, ConvertCrStatus_Unknown_ReturnsEIO)
{
    EXPECT_EQ(EIO, UrmaErrnoConverter::ConvertCrStatus(static_cast<urma_cr_status_t>(9999), URMA_OP_WRITEV));
}
