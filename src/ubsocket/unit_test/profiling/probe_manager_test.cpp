/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of the license at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "profiling/probe/probe_manager.h"

#include <cstring>
#include <vector>

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

using namespace Statistics;
using ock::ubs::umq::UmqSocket;

namespace {

struct ProbeBuf {
    umq_buf_t buf{};
    umq_buf_pro_t pro{};
    ProbeTimeInfo info{};

    ProbeBuf(uint32_t type, uint64_t userData = PROBE_USER_DATA_ID)
    {
        std::memcpy(buf.qbuf_ext, &pro, sizeof(pro));
        buf.buf_data = reinterpret_cast<char *>(&info);
        reinterpret_cast<umq_buf_pro_t *>(buf.qbuf_ext)->imm.user_data = userData;
        info.type = type;
    }
};

int gRegisterRet = 0;
int gPostRet = 0;
int gFreeCount = 0;
umq_buf_t *gAllocBuf = nullptr;

int MockRegister(umq_io_perf_callback_t)
{
    return gRegisterRet;
}

int MockPost(uint64_t, umq_buf_t *, umq_io_option_t *, umq_buf_t **)
{
    return gPostRet;
}

umq_buf_t *MockAlloc(uint32_t, uint32_t, uint64_t, umq_alloc_option_t *)
{
    return gAllocBuf;
}

void MockFree(umq_buf_t *)
{
    ++gFreeCount;
}

class ProbeManagerTest : public testing::Test {
protected:
    void SetUp() override
    {
        manager = &ProbeManager::GetInstance();
        manager->Stop();
        manager->mRunning.store(false);
        manager->mRecords.clear();
        manager->mQueueSt = manager->mQueueEd = 0;
        gRegisterRet = 0;
        gPostRet = 0;
        gFreeCount = 0;
        gAllocBuf = nullptr;
    }

    void TearDown() override
    {
        manager->Stop();
        GlobalMockObject::verify();
    }

    ProbeManager *manager;
};

TEST_F(ProbeManagerTest, UpdateBufferNullAndAllMasks)
{
    ProbeManager::UpdateBuffer(nullptr, MASK_NONE);
    ProbeTimeInfo info{};
    ProbeManager::UpdateBuffer(&info, MASK_CLIENT_SEND | MASK_CLIENT_RSP | MASK_SERVER_RECV | MASK_SERVER_RSP |
                                          MASK_UMQ_CLIENT_POST | MASK_UMQ_CLIENT_RECV | MASK_UMQ_SERVER_RECV |
                                          MASK_UMQ_SERVER_RSP);
    EXPECT_GT(info.client_send_time_ns, 0U);
    EXPECT_GT(info.client_recv_rsp_time_ns, 0U);
    EXPECT_GT(info.server_recv_time_ns, 0U);
    EXPECT_GT(info.server_rsp_time_ns, 0U);
    EXPECT_GT(info.umq_client_post_time_ns, 0U);
    EXPECT_GT(info.umq_client_recv_time_ns, 0U);
    EXPECT_GT(info.umq_server_recv_time_ns, 0U);
    EXPECT_GT(info.umq_server_rsp_time_ns, 0U);
}

TEST_F(ProbeManagerTest, UmqPerfCallbackFiltersAndUpdates)
{
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, nullptr);
    ProbeBuf noData(PROBE_TYPE_REQUEST);
    noData.buf.buf_data = nullptr;
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, &noData.buf);
    ProbeBuf invalid(PROBE_TYPE_REQUEST, PROBE_USER_DATA_ID + 1);
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, &invalid.buf);
    ProbeBuf request(PROBE_TYPE_REQUEST);
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, &request.buf);
    EXPECT_GT(request.info.umq_client_post_time_ns, 0U);
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POLL_RX, &request.buf);
    EXPECT_GE(request.info.server_recv_time_ns, 0U);
    ProbeBuf response(PROBE_TYPE_RESPONSE);
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POST_SEND, &response.buf);
    EXPECT_GT(response.info.umq_server_rsp_time_ns, 0U);
    ProbeManager::UmqPerfCallback(UMQ_PERF_RECORD_TRANSPORT_POLL_RX, &response.buf);
    EXPECT_GT(response.info.umq_client_recv_time_ns, 0U);
}

TEST_F(ProbeManagerTest, RegisterCallbacksHandlesBothResults)
{
    MOCKER_CPP(::umq_io_perf_callback_register).stubs().will(invoke(MockRegister));
    gRegisterRet = 0;
    manager->RegisterUmqCallbacks();
    gRegisterRet = -1;
    manager->RegisterUmqCallbacks();
}

TEST_F(ProbeManagerTest, HandleReceivedPacketFiltersAndRequest)
{
    sem_init(&manager->mSem, 0, 0);
    manager->mRunning.store(true);
    manager->HandleReceivedPacket(11, nullptr);
    ProbeBuf noData(PROBE_TYPE_REQUEST);
    noData.buf.buf_data = nullptr;
    manager->HandleReceivedPacket(11, &noData.buf);
    ProbeBuf wrong(PROBE_TYPE_REQUEST, PROBE_USER_DATA_ID + 1);
    manager->HandleReceivedPacket(11, &wrong.buf);
    ProbeBuf request(PROBE_TYPE_REQUEST);
    request.info.seq_id = 7;
    manager->HandleReceivedPacket(11, &request.buf);
    EXPECT_EQ(manager->mQueueEd, 1U);
    EXPECT_EQ(manager->mRecvQueue[0].mSockFd, 11U);
    sem_destroy(&manager->mSem);
    manager->mRunning.store(false);
}

TEST_F(ProbeManagerTest, HandleResponseComputesRttAndMissingRecord)
{
    sem_init(&manager->mSem, 0, 0);
    manager->mRunning.store(true);
    ProbeBuf response(PROBE_TYPE_RESPONSE);
    response.info.client_send_time_ns = 100;
    response.info.client_recv_rsp_time_ns = 350;
    manager->HandleReceivedPacket(12, &response.buf);
    ASSERT_EQ(manager->mRecords.count(12), 1U);
    EXPECT_GT(manager->mRecords[12].mLastRttNs, 0U);
    response.info.client_send_time_ns = UINT64_MAX;
    manager->HandleReceivedPacket(12, &response.buf);
    EXPECT_EQ(manager->mRecords[12].mLastRttNs, 0U);
    manager->mRunning.store(false);
    sem_destroy(&manager->mSem);
}

TEST_F(ProbeManagerTest, HandlePacketWhenStoppedAndGetData)
{
    ProbeBuf request(PROBE_TYPE_REQUEST);
    manager->mRunning.store(false);
    manager->HandleReceivedPacket(13, &request.buf);
    manager->mRecords[13].mSockFd = 13;
    manager->mRecords[13].mLastRttNs = 99;
    manager->mRecords[13].mProbeInfo.client_send_time_ns = 123;
    std::vector<CLIProbeData> output;
    manager->GetCLIProbeData(output);
    ASSERT_EQ(output.size(), 1U);
    EXPECT_EQ(output[0].fd, 13);
    EXPECT_EQ(output[0].rtt, 99U);
    EXPECT_EQ(output[0].client_send_time_ns, 123U);
}

TEST_F(ProbeManagerTest, SendProbePacketAllocationAndPostPaths)
{
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(MockAlloc));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockFree));
    MOCKER_CPP(::umq_post).stubs().will(invoke(MockPost));
    UmqSocket sock(21);
    MOCKER_CPP(&UmqSetting::GetIOBufSize).stubs().will(returnValue(static_cast<uint32_t>(1)));
    gAllocBuf = nullptr;
    EXPECT_EQ(manager->SendProbePacket(&sock), -1);
    ProbeBuf allocated(PROBE_TYPE_REQUEST);
    gAllocBuf = &allocated.buf;
    gPostRet = -1;
    EXPECT_EQ(manager->SendProbePacket(&sock), -1);
    EXPECT_EQ(gFreeCount, 1);
    gPostRet = 0;
    EXPECT_EQ(manager->SendProbePacket(&sock), 0);
    EXPECT_EQ(manager->mRecords[21].mProbeInfo.seq_id, 2U);
}

TEST_F(ProbeManagerTest, SendResponseAllocationAndPostPaths)
{
    MOCKER_CPP(::umq_buf_alloc).stubs().will(invoke(MockAlloc));
    MOCKER_CPP(::umq_buf_free).stubs().will(invoke(MockFree));
    MOCKER_CPP(::umq_post).stubs().will(invoke(MockPost));
    UmqSocket sock(22);
    MOCKER_CPP(&UmqSetting::GetIOBufSize).stubs().will(returnValue(static_cast<uint32_t>(1)));
    ProbeTimeInfo request{};
    request.type = PROBE_TYPE_REQUEST;
    gAllocBuf = nullptr;
    EXPECT_EQ(manager->SendResponsePacket(&sock, &request), -1);
    ProbeBuf allocated(PROBE_TYPE_RESPONSE);
    gAllocBuf = &allocated.buf;
    gPostRet = -1;
    EXPECT_EQ(manager->SendResponsePacket(&sock, &request), -1);
    gPostRet = 0;
    EXPECT_EQ(manager->SendResponsePacket(&sock, &request), 0);
    EXPECT_EQ(allocated.info.type, PROBE_TYPE_RESPONSE);
}

TEST_F(ProbeManagerTest, StartAndStopAreIdempotent)
{
    MOCKER_CPP(::umq_io_perf_callback_register).stubs().will(returnValue(0));
    manager->Start(1000, 0, -1);
    manager->Start(1000, 0, -1);
    manager->Stop();
    manager->Stop();
    EXPECT_FALSE(manager->mRunning.load());
}

TEST_F(ProbeManagerTest, PrivateSchedulingHelpersCoverEmptyAndBoundaryPaths)
{
    struct timespec ts {
        1, 900000000
    };
    manager->UpdateTimespec(&ts, 200);
    EXPECT_EQ(ts.tv_sec, 2);
    EXPECT_EQ(ts.tv_nsec, 100000000);

    uint32_t sentCount = 0;
    manager->mProbeBatch = 0;
    manager->ProcessServerQueue(sentCount);
    manager->ProcessClientProbing(sentCount);
    manager->mProbeBatch = 1;
    manager->ProcessClientProbing(sentCount);

    std::thread worker([] {});
    manager->BindThreadToCore(worker, 0);
    worker.join();
}

TEST_F(ProbeManagerTest, RequestQueueWrapsAtCapacity)
{
    sem_init(&manager->mSem, 0, 0);
    manager->mRunning.store(true);
    manager->mQueueEd = RPC_ADPT_FD_MAX - 1U;
    ProbeBuf request(PROBE_TYPE_REQUEST);
    manager->HandleReceivedPacket(31, &request.buf);
    EXPECT_EQ(manager->mQueueEd, 0U);
    manager->mRunning.store(false);
    sem_destroy(&manager->mSem);
}

TEST_F(ProbeManagerTest, StartWithCoreAndImmediateTimeout)
{
    MOCKER_CPP(::umq_io_perf_callback_register).stubs().will(returnValue(0));
    manager->Start(0, 1, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    manager->Stop();
    EXPECT_FALSE(manager->mRunning.load());
}

} // namespace
