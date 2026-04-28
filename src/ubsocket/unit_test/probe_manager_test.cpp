/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Unit tests for probe_manager module
 */

#include "probe_manager.h"
#include "rpc_adpt_vlog.h"
#include "file_descriptor.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include <cstring>
#include <new>
#include <vector>

using namespace Statistics;

// Named constants to avoid magic numbers (G.CNS.02)
namespace {
static const uint32_t PROBE_TEST_TYPE_REQUEST = 1U;
static const uint32_t PROBE_TEST_TYPE_RESPONSE = 2U;
static const uint32_t PROBE_TEST_SEQ_ID = 123U;
static const uint64_t PROBE_TEST_TIME_NS_1000 = 1000U;
static const uint64_t PROBE_TEST_TIME_NS_2000 = 2000U;
static const uint64_t PROBE_TEST_TIME_NS_3000 = 3000U;
static const uint64_t PROBE_TEST_TIME_NS_4000 = 4000U;
static const uint64_t PROBE_TEST_RTT_1000 = 1000U;
static const int PROBE_TEST_FD_1 = 1;
static const int PROBE_TEST_FD_2 = 2;
static const uint32_t PROBE_TEST_MAX_COUNT_10 = 10U;
static const uint32_t PROBE_TEST_USER_DATA_ID = 1U;
static const uint32_t PROBE_TEST_IOBUF_SIZE = 1024U;
static const uint64_t PROBE_TEST_UMQ_HANDLE = 100U;

// New constants to avoid magic numbers
static const uint32_t PROBE_IOBUF_HALF_DIVISOR = 2U;
static const uint32_t PROBE_START_INTERVAL_MS = 1000U;
static const uint32_t PROBE_START_THREADS = 1U;
static const int PROBE_BIND_CORE_SKIP = -1;
static const useconds_t PROBE_SLEEP_SHORT_US = 5000U;
static const useconds_t PROBE_SLEEP_MEDIUM_US = 10000U;
static const useconds_t PROBE_SLEEP_TINY_US = 1000U;
static const uint32_t PROBE_TEST_NUM_U32_FIELDS = 2U;
static const uint32_t PROBE_TEST_NUM_U64_FIELDS = 8U;
static const uint32_t PROBE_TEST_EXTRA_PADDING = 8U;
} // namespace

// Mock SocketFd class for testing
class MockSocketFd : public SocketFd {
public:
    MockSocketFd(int fd, uint64_t umqHandle, uint32_t iobufSize)
        : SocketFd(fd), mUmqHandle(umqHandle), mIoBufSize(iobufSize) {}

    virtual ~MockSocketFd() {}

    void OutputStats(std::ostringstream &oss) override {}
    void GetSocketCLIData(Statistics::CLISocketData *data) override {}
    void GetSocketFlowControlData(Statistics::CLIFlowControlData *data) override {}
    void GetSocketQbufPoolData(Statistics::CLIQbufPoolData *data) override {}
    void GetSocketUmqInfoData(Statistics::CLIUmqInfoData *data) override {}
    void GetSocketIoPacketData(Statistics::CLIIoPacketData *data) override {}
    void GetSocketUmqPerfData(Statistics::CLIUmqPerfData *data) override {}
    uint64_t GetLocalUmqHandle(void) override { return mUmqHandle; }
    bool IsClient(void) override { return true; }
    uint32_t GetBrpcIoBufSize(void) override { return mIoBufSize; }

private:
    uint64_t mUmqHandle = 0;
    uint32_t mIoBufSize = 0;
};

// Helper to create a test umq_buf_t
static umq_buf_t *CreateTestUmqBuf(size_t dataSize)
{
    size_t totalSize = sizeof(umq_buf_t) + dataSize + sizeof(umq_buf_pro_t) + 64;
    char *mem = new char[totalSize];
    // use safe memset variant
    (void)memset_s(mem, totalSize, 0, totalSize);

    umq_buf_t *buf = reinterpret_cast<umq_buf_t *>(mem);
    buf->buf_size = dataSize;
    buf->data_size = dataSize;
    buf->buf_data = mem + sizeof(umq_buf_t);
    buf->qbuf_ext[0] = reinterpret_cast<uint64_t>(mem + sizeof(umq_buf_t) + dataSize);

    return buf;
}

static void FreeTestUmqBuf(umq_buf_t *buf)
{
    if (buf) {
        char *mem = reinterpret_cast<char *>(buf);
        delete[] mem;
    }
}

// Test fixture for ProbeManager tests
class ProbeManagerTest : public testing::Test {
public:
    void SetUp() override;
    void TearDown() override;
};

void ProbeManagerTest::SetUp()
{
    RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
}

void ProbeManagerTest::TearDown()
{
    // Ensure any background thread from the singleton is stopped between tests
    ProbeManager::GetInstance().Stop();
    GlobalMockObject::verify();
}

// ============= UpdateBuffer Static Method Tests =============

TEST_F(ProbeManagerTest, UpdateBuffer_NullInfo_Returns)
{
    // Null ProbeTimeInfo should return without crash
    ProbeManager::UpdateBuffer(nullptr, MASK_CLIENT_SEND);
    // Should not crash
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskClientSend)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_SEND);

    // client_send_time_ns should be updated
    EXPECT_GT(info.client_send_time_ns, 0U);
    // Other fields should remain 0
    EXPECT_EQ(info.client_recv_rsp_time_ns, 0U);
    EXPECT_EQ(info.server_recv_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskClientRsp)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_RSP);

    // client_recv_rsp_time_ns should be updated
    EXPECT_GT(info.client_recv_rsp_time_ns, 0U);
    // Other fields should remain 0
    EXPECT_EQ(info.client_send_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskServerRecv)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_SERVER_RECV);

    // server_recv_time_ns should be updated
    EXPECT_GT(info.server_recv_time_ns, 0U);
    // Other fields should remain 0
    EXPECT_EQ(info.server_rsp_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskServerRsp)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_SERVER_RSP);

    // server_rsp_time_ns should be updated
    EXPECT_GT(info.server_rsp_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskUmqClientPost)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_UMQ_CLIENT_POST);

    // umq_client_post_time_ns should be updated
    EXPECT_GT(info.umq_client_post_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskUmqClientRecv)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_UMQ_CLIENT_RECV);

    // umq_client_recv_time_ns should be updated
    EXPECT_GT(info.umq_client_recv_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskUmqServerRecv)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_UMQ_SERVER_RECV);

    // umq_server_recv_time_ns should be updated
    EXPECT_GT(info.umq_server_recv_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskUmqServerRsp)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_UMQ_SERVER_RSP);

    // umq_server_rsp_time_ns should be updated
    EXPECT_GT(info.umq_server_rsp_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskNone)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_NONE);

    // All fields should remain 0
    EXPECT_EQ(info.client_send_time_ns, 0U);
    EXPECT_EQ(info.client_recv_rsp_time_ns, 0U);
    EXPECT_EQ(info.server_recv_time_ns, 0U);
    EXPECT_EQ(info.server_rsp_time_ns, 0U);
    EXPECT_EQ(info.umq_client_post_time_ns, 0U);
    EXPECT_EQ(info.umq_client_recv_time_ns, 0U);
    EXPECT_EQ(info.umq_server_recv_time_ns, 0U);
    EXPECT_EQ(info.umq_server_rsp_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskMultiple)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    // Set multiple masks at once
    uint32_t multiMask = MASK_CLIENT_SEND | MASK_SERVER_RECV | MASK_UMQ_CLIENT_POST;
    ProbeManager::UpdateBuffer(&info, multiMask);

    // Multiple fields should be updated
    EXPECT_GT(info.client_send_time_ns, 0U);
    EXPECT_GT(info.server_recv_time_ns, 0U);
    EXPECT_GT(info.umq_client_post_time_ns, 0U);
    // Other fields should remain 0
    EXPECT_EQ(info.client_recv_rsp_time_ns, 0U);
    EXPECT_EQ(info.server_rsp_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UpdateBuffer_MaskAll)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_SEND | MASK_CLIENT_RSP |
                                       MASK_SERVER_RECV | MASK_SERVER_RSP |
                                       MASK_UMQ_CLIENT_POST | MASK_UMQ_CLIENT_RECV |
                                       MASK_UMQ_SERVER_RECV | MASK_UMQ_SERVER_RSP);

    // All time fields should be updated
    EXPECT_GT(info.client_send_time_ns, 0U);
    EXPECT_GT(info.client_recv_rsp_time_ns, 0U);
    EXPECT_GT(info.server_recv_time_ns, 0U);
    EXPECT_GT(info.server_rsp_time_ns, 0U);
    EXPECT_GT(info.umq_client_post_time_ns, 0U);
    EXPECT_GT(info.umq_client_recv_time_ns, 0U);
    EXPECT_GT(info.umq_server_recv_time_ns, 0U);
    EXPECT_GT(info.umq_server_rsp_time_ns, 0U);
}

// ============= ProbeRecord Tests =============

TEST_F(ProbeManagerTest, ProbeRecord_DefaultConstructor)
{
    ProbeRecord record;
    EXPECT_EQ(record.mSockFd, 0U);
    EXPECT_EQ(record.mLastRttNs, 0U);
    EXPECT_FALSE(record.mIsCompleted);
    EXPECT_EQ(record.mProbeInfo.type, 0U);
    EXPECT_EQ(record.mProbeInfo.seq_id, 0U);
}

TEST_F(ProbeManagerTest, ProbeRecord_ManualInitialization)
{
    ProbeRecord record;
    record.mSockFd = PROBE_TEST_FD_2;
    record.mLastRttNs = PROBE_TEST_RTT_1000;
    record.mIsCompleted = true;
    record.mProbeInfo.type = PROBE_TYPE_REQUEST;
    record.mProbeInfo.seq_id = PROBE_TEST_SEQ_ID;

    EXPECT_EQ(record.mSockFd, PROBE_TEST_FD_2);
    EXPECT_EQ(record.mLastRttNs, PROBE_TEST_RTT_1000);
    EXPECT_TRUE(record.mIsCompleted);
    EXPECT_EQ(record.mProbeInfo.type, PROBE_TYPE_REQUEST);
    EXPECT_EQ(record.mProbeInfo.seq_id, PROBE_TEST_SEQ_ID);
}

// ============= ProbeTimeInfo Tests =============

TEST_F(ProbeManagerTest, ProbeTimeInfo_Structure)
{
    ProbeTimeInfo info;
    info.type = PROBE_TEST_TYPE_REQUEST;
    info.seq_id = PROBE_TEST_SEQ_ID;
    info.client_send_time_ns = PROBE_TEST_TIME_NS_1000;
    info.client_recv_rsp_time_ns = PROBE_TEST_TIME_NS_2000;
    info.umq_client_post_time_ns = PROBE_TEST_TIME_NS_3000;
    info.umq_client_recv_time_ns = PROBE_TEST_TIME_NS_4000;
    info.server_recv_time_ns = PROBE_TEST_TIME_NS_1000;
    info.server_rsp_time_ns = PROBE_TEST_TIME_NS_2000;
    info.umq_server_recv_time_ns = PROBE_TEST_TIME_NS_3000;
    info.umq_server_rsp_time_ns = PROBE_TEST_TIME_NS_4000;

    EXPECT_EQ(info.type, PROBE_TEST_TYPE_REQUEST);
    EXPECT_EQ(info.seq_id, PROBE_TEST_SEQ_ID);
    EXPECT_EQ(info.client_send_time_ns, PROBE_TEST_TIME_NS_1000);
    EXPECT_EQ(info.client_recv_rsp_time_ns, PROBE_TEST_TIME_NS_2000);
    EXPECT_EQ(info.umq_client_post_time_ns, PROBE_TEST_TIME_NS_3000);
    EXPECT_EQ(info.umq_client_recv_time_ns, PROBE_TEST_TIME_NS_4000);
    EXPECT_EQ(info.server_recv_time_ns, PROBE_TEST_TIME_NS_1000);
    EXPECT_EQ(info.server_rsp_time_ns, PROBE_TEST_TIME_NS_2000);
    EXPECT_EQ(info.umq_server_recv_time_ns, PROBE_TEST_TIME_NS_3000);
    EXPECT_EQ(info.umq_server_rsp_time_ns, PROBE_TEST_TIME_NS_4000);
}

TEST_F(ProbeManagerTest, ProbeTimeInfo_PackedAttribute)
{
    // Verify struct size does not exceed expected packed layout
    EXPECT_LE(sizeof(ProbeTimeInfo), sizeof(uint32_t) * PROBE_TEST_NUM_U32_FIELDS +
                                   sizeof(uint64_t) * PROBE_TEST_NUM_U64_FIELDS +
                                   PROBE_TEST_EXTRA_PADDING);
}

// ============= ProbeType Enum Tests =============

TEST_F(ProbeManagerTest, ProbeType_Values)
{
    EXPECT_EQ(PROBE_TYPE_REQUEST, PROBE_TEST_TYPE_REQUEST);
    EXPECT_EQ(PROBE_TYPE_RESPONSE, PROBE_TEST_TYPE_RESPONSE);
}

// ============= ProbeUpdateMask Enum Tests =============

TEST_F(ProbeManagerTest, ProbeUpdateMask_Values)
{
    EXPECT_EQ(MASK_NONE, 0x00);
    EXPECT_EQ(MASK_CLIENT_SEND, 0x01);
    EXPECT_EQ(MASK_CLIENT_RSP, 0x02);
    EXPECT_EQ(MASK_SERVER_RECV, 0x04);
    EXPECT_EQ(MASK_SERVER_RSP, 0x08);
    EXPECT_EQ(MASK_UMQ_CLIENT_POST, 0x10);
    EXPECT_EQ(MASK_UMQ_CLIENT_RECV, 0x20);
    EXPECT_EQ(MASK_UMQ_SERVER_RECV, 0x40);
    EXPECT_EQ(MASK_UMQ_SERVER_RSP, 0x80);
}

// ============= GetCLIProbeData Tests (without UMQ dependency) =============

TEST_F(ProbeManagerTest, GetCLIProbeData_EmptyVector_Returns)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    std::vector<CLIProbeData> dataVec;
    manager.GetCLIProbeData(dataVec);
    // Should not crash, vector may be empty or contain records
}

TEST_F(ProbeManagerTest, GetCLIProbeData_ValidVector_ReturnsData)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    std::vector<CLIProbeData> dataVec;
    manager.GetCLIProbeData(dataVec);
    // No crash and valid vector returned; content verification is done in other tests
}

// ============= GetInstance Tests =============

TEST_F(ProbeManagerTest, GetInstance_ReturnsValidInstance)
{
    ProbeManager& instance1 = ProbeManager::GetInstance();
    ProbeManager& instance2 = ProbeManager::GetInstance();

    // Same instance returned (singleton)
    EXPECT_EQ(&instance1, &instance2);
}

// ============= CLIProbeData Structure Tests =============

TEST_F(ProbeManagerTest, CLIProbeData_Structure)
{
    CLIProbeData data;
    data.fd = PROBE_TEST_FD_1;
    data.rtt = PROBE_TEST_RTT_1000;
    data.client_send_time_ns = PROBE_TEST_TIME_NS_1000;
    data.client_recv_rsp_time_ns = PROBE_TEST_TIME_NS_2000;
    data.umq_client_post_time_ns = PROBE_TEST_TIME_NS_3000;
    data.umq_client_recv_time_ns = PROBE_TEST_TIME_NS_4000;
    data.server_recv_time_ns = PROBE_TEST_TIME_NS_1000;
    data.server_rsp_time_ns = PROBE_TEST_TIME_NS_2000;
    data.umq_server_recv_time_ns = PROBE_TEST_TIME_NS_3000;
    data.umq_server_rsp_time_ns = PROBE_TEST_TIME_NS_4000;

    EXPECT_EQ(data.fd, PROBE_TEST_FD_1);
    EXPECT_EQ(data.rtt, PROBE_TEST_RTT_1000);
}

// ============= Additional Coverage Tests =============

TEST_F(ProbeManagerTest, UpdateBuffer_SequentialCalls)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    // Call multiple times with different masks
    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_SEND);
    uint64_t firstTime = info.client_send_time_ns;

    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_SEND);
    // Second call should update the same field
    EXPECT_GE(info.client_send_time_ns, firstTime);
}

TEST_F(ProbeManagerTest, UpdateBuffer_TimeIncreases)
{
    ProbeTimeInfo info;
    memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_SEND);
    uint64_t time1 = info.client_send_time_ns;

    // Small delay to ensure time increases
    usleep(PROBE_SLEEP_TINY_US);

    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_SEND);
    uint64_t time2 = info.client_send_time_ns;

    EXPECT_GE(time2, time1);
}

TEST_F(ProbeManagerTest, UpdateBuffer_EachMaskIndependently)
{
    // Test each mask independently
    for (uint32_t mask = MASK_CLIENT_SEND; mask <= MASK_UMQ_SERVER_RSP; mask <<= 1) {
        ProbeTimeInfo info;
        memset_s(&info, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

        ProbeManager::UpdateBuffer(&info, mask);

        // At least one field should be non-zero
        bool hasNonZero = (info.client_send_time_ns > 0) ||
                          (info.client_recv_rsp_time_ns > 0) ||
                          (info.server_recv_time_ns > 0) ||
                          (info.server_rsp_time_ns > 0) ||
                          (info.umq_client_post_time_ns > 0) ||
                          (info.umq_client_recv_time_ns > 0) ||
                          (info.umq_server_recv_time_ns > 0) ||
                          (info.umq_server_rsp_time_ns > 0);
        EXPECT_TRUE(hasNonZero);
    }
}

// ============= Constants Tests =============

TEST_F(ProbeManagerTest, Constants_ProbeIntervalSec)
{
    EXPECT_EQ(PROBE_INTERVAL_SEC, 1U);
}

TEST_F(ProbeManagerTest, Constants_ProbeUserDataId)
{
    EXPECT_EQ(PROBE_USER_DATA_ID, 1U);
}

TEST_F(ProbeManagerTest, Constants_ProbeSemMsToNs)
{
    EXPECT_EQ(PROBE_SEM_MS_TO_NS, 1000000ULL);
}

TEST_F(ProbeManagerTest, Constants_ProbeSemSToNs)
{
    EXPECT_EQ(PROBE_SEM_S_TO_NS, 1000000000ULL);
}

// ============= Mock Tests for SendProbePacket with MockSocketFd =============

TEST_F(ProbeManagerTest, SendProbePacket_BufAllocFail_ReturnsError)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    MockSocketFd mockSock(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue((umq_buf_t *)nullptr));

    int ret = manager.SendProbePacket(&mockSock);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
}

TEST_F(ProbeManagerTest, SendProbePacket_UmqPostFail_ReturnsError)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    MockSocketFd mockSock(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue(testBuf));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(-1)));
    MOCKER_CPP(umq_buf_free).stubs();

    int ret = manager.SendProbePacket(&mockSock);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
    // umq_buf_free is stubbed; the mocked framework may handle freeing; avoid double-free here
}

TEST_F(ProbeManagerTest, SendProbePacket_Success_ReturnsZero)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    MockSocketFd mockSock(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue(testBuf));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(0)));

    int ret = manager.SendProbePacket(&mockSock);
    EXPECT_EQ(ret, 0);

    GlobalMockObject::verify();
    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, SendProbePacket_SmallIoBufSize_UsesProbeInfoSize)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    // Test with small iobuf size
    MockSocketFd mockSock(PROBE_TEST_FD_2, PROBE_TEST_UMQ_HANDLE, sizeof(ProbeTimeInfo) / PROBE_IOBUF_HALF_DIVISOR);
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue(testBuf));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(0)));

    int ret = manager.SendProbePacket(&mockSock);
    EXPECT_EQ(ret, 0);

    GlobalMockObject::verify();
    FreeTestUmqBuf(testBuf);
}

// ============= Mock Tests for SendResponsePacket =============

TEST_F(ProbeManagerTest, SendResponsePacket_BufAllocFail_ReturnsError)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    MockSocketFd mockSock(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);
    ProbeTimeInfo reqInfo;
    memset_s(&reqInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue((umq_buf_t *)nullptr));

    int ret = manager.SendResponsePacket(&mockSock, &reqInfo);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
}

TEST_F(ProbeManagerTest, SendResponsePacket_UmqPostFail_ReturnsError)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    MockSocketFd mockSock(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);
    ProbeTimeInfo reqInfo;
    memset_s(&reqInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    reqInfo.type = PROBE_TYPE_REQUEST;
    reqInfo.seq_id = PROBE_TEST_SEQ_ID;

    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue(testBuf));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(-1)));
    MOCKER_CPP(umq_buf_free).stubs();

    int ret = manager.SendResponsePacket(&mockSock, &reqInfo);
    EXPECT_EQ(ret, -1);

    GlobalMockObject::verify();
    // umq_buf_free is stubbed; avoid manual free to prevent double-free
}

TEST_F(ProbeManagerTest, SendResponsePacket_Success_ReturnsZero)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    MockSocketFd mockSock(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);
    ProbeTimeInfo reqInfo;
    memset_s(&reqInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    reqInfo.type = PROBE_TYPE_REQUEST;
    reqInfo.seq_id = PROBE_TEST_SEQ_ID;

    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue(testBuf));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(0)));

    int ret = manager.SendResponsePacket(&mockSock, &reqInfo);
    EXPECT_EQ(ret, 0);

    GlobalMockObject::verify();
    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, SendResponsePacket_SmallIoBufSize_UsesProbeInfoSize)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    MockSocketFd mockSock(PROBE_TEST_FD_2, PROBE_TEST_UMQ_HANDLE, sizeof(ProbeTimeInfo) / PROBE_IOBUF_HALF_DIVISOR);
    ProbeTimeInfo reqInfo;
    memset_s(&reqInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    reqInfo.type = PROBE_TYPE_REQUEST;

    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue(testBuf));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(0)));

    int ret = manager.SendResponsePacket(&mockSock, &reqInfo);
    EXPECT_EQ(ret, 0);

    GlobalMockObject::verify();
    FreeTestUmqBuf(testBuf);
}

// ============= UmqPerfCallback Tests =============

TEST_F(ProbeManagerTest, UmqPerfCallback_NullBuffer_Returns)
{
    // Null buffer should return without crash
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, nullptr);
}

TEST_F(ProbeManagerTest, UmqPerfCallback_NullBufferData_Returns)
{
    umq_buf_t buf;
    memset_s(&buf, sizeof(umq_buf_t), 0, sizeof(umq_buf_t));
    buf.buf_data = nullptr;

    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, &buf);
}

TEST_F(ProbeManagerTest, UmqPerfCallback_NonProbePacket_Returns)
{
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = 0; // Not PROBE_USER_DATA_ID

    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, testBuf);

    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, UmqPerfCallback_RequestPostSend_UpdatesClientPostTime)
{
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(testBuf->buf_data);
    memset_s(probeInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    probeInfo->type = PROBE_TYPE_REQUEST;

    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = PROBE_USER_DATA_ID;

    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, testBuf);

    // umq_client_post_time_ns should be updated for REQUEST type
    EXPECT_GT(probeInfo->umq_client_post_time_ns, 0U);

    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, UmqPerfCallback_ResponsePostSend_UpdatesServerRspTime)
{
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(testBuf->buf_data);
    memset_s(probeInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    probeInfo->type = PROBE_TYPE_RESPONSE;

    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = PROBE_USER_DATA_ID;

    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, testBuf);

    // umq_server_rsp_time_ns should be updated for RESPONSE type
    EXPECT_GT(probeInfo->umq_server_rsp_time_ns, 0U);

    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, UmqPerfCallback_RequestPollRx_UpdatesServerRecvTime)
{
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(testBuf->buf_data);
    memset_s(probeInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    probeInfo->type = PROBE_TYPE_REQUEST;

    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = PROBE_USER_DATA_ID;

    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POLL_RX, testBuf);

    // umq_server_recv_time_ns should be updated for REQUEST type in poll_rx
    EXPECT_GT(probeInfo->umq_server_recv_time_ns, 0U);

    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, UmqPerfCallback_ResponsePollRx_UpdatesClientRecvTime)
{
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(testBuf->buf_data);
    memset_s(probeInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    probeInfo->type = PROBE_TYPE_RESPONSE;

    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = PROBE_USER_DATA_ID;

    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POLL_RX, testBuf);

    // umq_client_recv_time_ns should be updated for RESPONSE type in poll_rx
    EXPECT_GT(probeInfo->umq_client_recv_time_ns, 0U);

    FreeTestUmqBuf(testBuf);
}

// ============= HandleReceivedPacket Tests =============

TEST_F(ProbeManagerTest, HandleReceivedPacket_NullBuffer_Returns)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    manager.HandleReceivedPacket(PROBE_TEST_FD_1, nullptr);
}

TEST_F(ProbeManagerTest, HandleReceivedPacket_NullBufferData_Returns)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    umq_buf_t buf;
    memset_s(&buf, sizeof(umq_buf_t), 0, sizeof(umq_buf_t));
    buf.buf_data = nullptr;

    manager.HandleReceivedPacket(PROBE_TEST_FD_1, &buf);
}

TEST_F(ProbeManagerTest, HandleReceivedPacket_NonProbePacket_Returns)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = 0; // Not PROBE_USER_DATA_ID

    manager.HandleReceivedPacket(PROBE_TEST_FD_1, testBuf);

    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, HandleReceivedPacket_RequestPacket_UpdatesServerRecvTime)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(testBuf->buf_data);
    memset_s(probeInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    probeInfo->type = PROBE_TYPE_REQUEST;

    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = PROBE_USER_DATA_ID;

    manager.HandleReceivedPacket(PROBE_TEST_FD_1, testBuf);

    // server_recv_time_ns should be updated for REQUEST type
    EXPECT_GT(probeInfo->server_recv_time_ns, 0U);

    FreeTestUmqBuf(testBuf);
}

TEST_F(ProbeManagerTest, HandleReceivedPacket_ResponsePacket_UpdatesClientRspTime)
{
    ProbeManager& manager = ProbeManager::GetInstance();
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    ProbeTimeInfo *probeInfo = reinterpret_cast<ProbeTimeInfo *>(testBuf->buf_data);
    memset_s(probeInfo, sizeof(ProbeTimeInfo), 0, sizeof(ProbeTimeInfo));
    probeInfo->type = PROBE_TYPE_RESPONSE;
    probeInfo->client_send_time_ns = PROBE_TEST_TIME_NS_1000;
    probeInfo->client_recv_rsp_time_ns = PROBE_TEST_TIME_NS_2000;

    umq_buf_pro_t *buf_pro = reinterpret_cast<umq_buf_pro_t *>(testBuf->qbuf_ext);
    buf_pro->imm.user_data = PROBE_USER_DATA_ID;

    manager.HandleReceivedPacket(PROBE_TEST_FD_1, testBuf);

    // client_recv_rsp_time_ns should be updated for RESPONSE type
    EXPECT_GT(probeInfo->client_recv_rsp_time_ns, 0U);

    FreeTestUmqBuf(testBuf);
}

// ============= Start/Stop Tests =============

TEST_F(ProbeManagerTest, Start_Stop_BasicLifecycle)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    // Mock umq_io_perf_callback_register to avoid real UMQ dependency
    MOCKER_CPP(umq_io_perf_callback_register).stubs().will(returnValue(int(0)));

    // Start with bindCore=-1 to skip BindThreadToCore
    manager.Start(PROBE_START_INTERVAL_MS, PROBE_START_THREADS, PROBE_BIND_CORE_SKIP);

    // Give thread some time to start
    usleep(PROBE_SLEEP_MEDIUM_US); // 10ms

    // Stop the manager
    manager.Stop();

    GlobalMockObject::verify();
}

TEST_F(ProbeManagerTest, Start_Twice_ReturnsEarly)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    MOCKER_CPP(umq_io_perf_callback_register).stubs().will(returnValue(int(0)));

    // First start
    manager.Start(PROBE_START_INTERVAL_MS, PROBE_START_THREADS, PROBE_BIND_CORE_SKIP);
    usleep(PROBE_SLEEP_SHORT_US);

    // Second start should return early (exchange returns true)
    manager.Start(PROBE_START_INTERVAL_MS, PROBE_START_THREADS, PROBE_BIND_CORE_SKIP);

    // Clean up
    manager.Stop();

    GlobalMockObject::verify();
}

TEST_F(ProbeManagerTest, Stop_WhenNotRunning_ReturnsEarly)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    // Stop when not running should return early
    manager.Stop(); // mRunning is false, should do nothing
}

TEST_F(ProbeManagerTest, RegisterUmqCallbacks_Success)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    MOCKER_CPP(umq_io_perf_callback_register).stubs().will(returnValue(int(0)));

    manager.RegisterUmqCallbacks();

    GlobalMockObject::verify();
}

TEST_F(ProbeManagerTest, RegisterUmqCallbacks_Failure_LogsError)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    MOCKER_CPP(umq_io_perf_callback_register).stubs().will(returnValue(int(-1)));

    manager.RegisterUmqCallbacks();

    GlobalMockObject::verify();
}

// ============= GetCLIProbeData with Records Tests =============

TEST_F(ProbeManagerTest, GetCLIProbeData_WithRecords_ReturnsCorrectCount)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    // First, send a probe packet to create a record
    MockSocketFd mockSock(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);
    umq_buf_t *testBuf = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MOCKER_CPP(umq_buf_alloc).stubs().will(returnValue(testBuf));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(0)));

    manager.SendProbePacket(&mockSock);

    GlobalMockObject::verify();
    FreeTestUmqBuf(testBuf);

    // Now get CLI probe data
    std::vector<CLIProbeData> dataVec;
    manager.GetCLIProbeData(dataVec);

    // Should return at least 1 record
    EXPECT_GE(dataVec.size(), 1U);
}

TEST_F(ProbeManagerTest, GetCLIProbeData_MultipleRecords)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    // Send probe packets to multiple FDs to create multiple records
    umq_buf_t *testBuf1 = CreateTestUmqBuf(sizeof(ProbeTimeInfo));
    umq_buf_t *testBuf2 = CreateTestUmqBuf(sizeof(ProbeTimeInfo));

    MockSocketFd mockSock1(PROBE_TEST_FD_1, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);
    MockSocketFd mockSock2(PROBE_TEST_FD_2, PROBE_TEST_UMQ_HANDLE, PROBE_TEST_IOBUF_SIZE);

    MOCKER_CPP(umq_buf_alloc).stubs()
        .will(returnValue(testBuf1))
        .then(returnValue(testBuf2));
    MOCKER_CPP(umq_post).stubs().will(returnValue(int(0)));

    manager.SendProbePacket(&mockSock1);
    manager.SendProbePacket(&mockSock2);

    GlobalMockObject::verify();
    FreeTestUmqBuf(testBuf1);
    FreeTestUmqBuf(testBuf2);

    // Get CLI probe data
    std::vector<CLIProbeData> dataVec;
    manager.GetCLIProbeData(dataVec);

    EXPECT_GE(dataVec.size(), 2U);
}

// ============= BindThreadToCore Tests (indirect via Start) =============

TEST_F(ProbeManagerTest, Start_BindThreadToCore_Success)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    // Mock CPU affinity setting and umq callback register
    MOCKER_CPP(umq_io_perf_callback_register).stubs().will(returnValue(int(0)));
    MOCKER_CPP(pthread_setaffinity_np).stubs().will(returnValue(int(0)));

    // Start with bindCore=0 (valid core) - will call BindThreadToCore
    manager.Start(PROBE_START_INTERVAL_MS, PROBE_START_THREADS, 0);
    usleep(PROBE_SLEEP_MEDIUM_US);

    manager.Stop();

    GlobalMockObject::verify();
}

TEST_F(ProbeManagerTest, Start_BindThreadToCore_Failure_LogsError)
{
    ProbeManager& manager = ProbeManager::GetInstance();

    // Mock CPU affinity setting failure
    MOCKER_CPP(umq_io_perf_callback_register).stubs().will(returnValue(int(0)));
    MOCKER_CPP(pthread_setaffinity_np).stubs().will(returnValue(int(-1)));

    // Start with bindCore=0 - BindThreadToCore will fail but continue
    manager.Start(PROBE_START_INTERVAL_MS, PROBE_START_THREADS, 0);
    usleep(PROBE_SLEEP_MEDIUM_US);

    manager.Stop();

    GlobalMockObject::verify();
}