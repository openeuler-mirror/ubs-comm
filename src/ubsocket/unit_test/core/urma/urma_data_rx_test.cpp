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
#include <sys/eventfd.h>
#include <cerrno>
#include <cstring>
#include <mockcpp/mockcpp.hpp>

#include "common/ubsocket_global_setting.h"
#include "common/ubsocket_lock.h"
#include "core/ubsocket_socket.h"
#include "core/urma/urma_data_rx_ops.h"
#include "core/urma/urma_socket.h"
#include "core/urma/urma_socket_types.h"

using namespace ock::ubs;
using namespace ock::ubs::urma;

namespace {
static const int TEST_FD = 100;
static const size_t BUF_SIZE = 4 * 1024 * 1024;
static uint8_t g_recv_buf[BUF_SIZE];
} // namespace

class UrmaDataRxTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        errno = 0;
        LockRegistry::RegisterDefaultOps();
        ArraySet<Socket>::GetInstance().Init();
        GlobalSetting::UBS_TRACE_ENABLED = false;
        GlobalSetting::UBS_SPLIT_TRACE_ENABLED = false;

        memset(g_recv_buf, 0, BUF_SIZE);

        auto urma_sock = MakeRef<UrmaSocket>(TEST_FD);
        urma_sock->recv_buf_addr_ = reinterpret_cast<uintptr_t>(g_recv_buf);
        urma_sock->recv_buf_size_ = static_cast<uint32_t>(BUF_SIZE);
        urma_sock->send_buf_addr_ = reinterpret_cast<uintptr_t>(g_recv_buf);
        urma_sock->send_buf_size_ = static_cast<uint32_t>(BUF_SIZE);
        sock_ = RefConvert<UrmaSocket, Socket>(urma_sock);

        MOCKER(::eventfd_write).stubs().will(returnValue(0));
        MOCKER(::eventfd).stubs().will(returnValue(42));
        MOCKER_CPP(&UrmaRxOps::ResponseCtrlMessage).stubs().will(returnValue(static_cast<int32_t>(UBS_OK)));
    }

    void TearDown() override
    {
        errno = 0;
        GlobalMockObject::verify();
        ArraySet<Socket>::GetInstance().ReleaseAll();
        sock_ = nullptr;
    }

    void PrepareSlot(uint64_t seq_no, uint64_t write_imm)
    {
        UrmaRxOps rx(TEST_FD);
        rx.DataReached(sock_, seq_no, write_imm);
    }

    SocketPtr sock_{nullptr};
};

TEST_F(UrmaDataRxTest, DataReached_SlotFree_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    auto result = rx.DataReached(sock_, 0, 0x1000);
    EXPECT_EQ(UBS_OK, result);
}

TEST_F(UrmaDataRxTest, DataReached_SlotOccupied_ReturnsPrefillRx)
{
    UrmaRxOps rx(TEST_FD);
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, 0x1000));
    auto result = rx.DataReached(sock_, 256, 0x2000);
    EXPECT_EQ(UBS_PREFILL_RX, result);
}

TEST_F(UrmaDataRxTest, DataReached_OutOfOrder_ReturnsDataOrder)
{
    UrmaRxOps rx(TEST_FD);
    auto result = rx.DataReached(sock_, 1, 0x1000);
    EXPECT_EQ(UBS_LATER_SEQ, result);
}

TEST_F(UrmaDataRxTest, DataReached_StaleSlot_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, 0x1000));
    rx.consume_seq_no_.store(1, std::memory_order_release);

    auto result = rx.DataReached(sock_, 0, 0x2000);
    EXPECT_EQ(UBS_LATER_SEQ, result);
}

TEST_F(UrmaDataRxTest, DataReached_SameSeqAsConsumed_ReturnsOk)
{
    UrmaRxOps rx(TEST_FD);
    auto result = rx.DataReached(sock_, 0, 0x1000);
    EXPECT_EQ(UBS_OK, result);
}

TEST_F(UrmaDataRxTest, ReadV_NullIov_ReturnsEINVAL)
{
    UrmaRxOps rx(TEST_FD);
    errno = 0;
    auto ret = rx.ReadV(sock_, nullptr, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaDataRxTest, ReadV_ZeroIovcnt_ReturnsEINVAL)
{
    UrmaRxOps rx(TEST_FD);
    struct iovec iov;
    iov.iov_base = g_recv_buf + 128;
    iov.iov_len = 128;
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 0, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EINVAL, errno);
}

TEST_F(UrmaDataRxTest, ReadV_NoData_ReturnsEAGAIN)
{
    UrmaRxOps rx(TEST_FD);
    uint8_t buf[128];
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}

TEST_F(UrmaDataRxTest, ReadV_SingleMsgSingleIov_ReturnsFullData)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 10;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + 128);
    ctrl->small_io.body_size = body_size;
    const char test_data[] = "0123456789";
    memcpy(ctrl->small_io.data, test_data, body_size);

    WriteImmData imm;
    imm.ubsocket.buffer_offset = 128 / URMA_SND_BUFF_ALLOC_UNIT;

    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(body_size, ret);
    EXPECT_EQ(0, memcmp(buf, test_data, body_size));
}

TEST_F(UrmaDataRxTest, ReadV_MultiMsgMultiIov_ReturnsTotalData)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body1 = 5;
    const uint32_t body2 = 5;
    auto *ctrl1 = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + 128);
    ctrl1->small_io.body_size = body1;
    memcpy(ctrl1->small_io.data, "hello", body1);
    WriteImmData imm1;
    imm1.ubsocket.buffer_offset = 128 / URMA_SND_BUFF_ALLOC_UNIT;

    auto *ctrl2 = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + 256);
    ctrl2->small_io.body_size = body2;
    memcpy(ctrl2->small_io.data, "world", body2);
    WriteImmData imm2;
    imm2.ubsocket.buffer_offset = 256 / URMA_SND_BUFF_ALLOC_UNIT;

    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm1.data));
    ASSERT_EQ(UBS_LATER_SEQ, rx.DataReached(sock_, 1, imm2.data));

    uint8_t buf1[3] = {};
    uint8_t buf2[64] = {};
    struct iovec iov[2];
    iov[0].iov_base = buf1;
    iov[0].iov_len = sizeof(buf1);
    iov[1].iov_base = buf2;
    iov[1].iov_len = sizeof(buf2);

    auto ret = rx.ReadV(sock_, iov, 2, 0);
    EXPECT_EQ(static_cast<ssize_t>(body1 + body2), ret);
    EXPECT_EQ(0, memcmp(buf1, "hel", 3));
    EXPECT_EQ(0, memcmp(buf2, "loworld", 7));
}

TEST_F(UrmaDataRxTest, ReadV_PartialRead_ReturnsPartialData)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 20;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + 128);
    ctrl->small_io.body_size = body_size;
    const char test_data[] = "0123456789ABCDEFGHIJ";
    memcpy(ctrl->small_io.data, test_data, body_size);

    WriteImmData imm;
    imm.ubsocket.buffer_offset = 128 / URMA_SND_BUFF_ALLOC_UNIT;

    ASSERT_EQ(UBS_OK, rx.DataReached(sock_, 0, imm.data));

    uint8_t buf[10] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(10, ret);
    EXPECT_EQ(0, memcmp(buf, test_data, 10));
}

TEST_F(UrmaDataRxTest, ReadV_LaterSeqArrivedFirst_ReturnsEAGAIN)
{
    UrmaRxOps rx(TEST_FD);

    const uint32_t body_size = 10;
    auto *ctrl = reinterpret_cast<UrmaCtrlMessage *>(g_recv_buf + 256);
    ctrl->small_io.body_size = body_size;
    memcpy(ctrl->small_io.data, "ABCDEFGHIJ", body_size);
    WriteImmData imm;
    imm.ubsocket.buffer_offset = 256 / URMA_SND_BUFF_ALLOC_UNIT;

    ASSERT_EQ(UBS_LATER_SEQ, rx.DataReached(sock_, 1, imm.data));

    uint8_t buf[64] = {};
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);
    errno = 0;
    auto ret = rx.ReadV(sock_, &iov, 1, 0);
    EXPECT_EQ(-1, ret);
    EXPECT_EQ(EAGAIN, errno);
}
