/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Description: Provide the unit test for cli args parser, etc
 * Author:
 * Create: 2026-03-30
 * Note:
 * History: 2026-03-30
*/

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include <cstring>
#include <vector>

#include "umq_api.h"
#include "share_jfr_common.h"

namespace {
umq_eid_t MakeEid(uint8_t seed)
{
    umq_eid_t eid {};
    for (size_t i = 0; i < sizeof(eid.raw); ++i) {
        eid.raw[i] = static_cast<uint8_t>(seed + i);
    }
    return eid;
}

// Used for deterministic cleanup of SocketFdEpollTable.
constexpr int K_SOCKET_FD_EPOLL_CLEANUP_FD_0 = 100;
constexpr int K_SOCKET_FD_EPOLL_CLEANUP_FD_1 = 101;
constexpr int K_SOCKET_FD_EPOLL_CLEANUP_FD_2 = 102;

constexpr uint8_t K_EID_SEED_A = 1;
constexpr uint8_t K_EID_SEED_B = 2;
constexpr uint8_t K_EID_SEED_A_10 = 10;
constexpr uint8_t K_EID_SEED_A_20 = 20;

constexpr uint64_t K_EID_UMQ_TABLE_MAIN_ENTRY_A_0 = 1001;
constexpr uint64_t K_EID_UMQ_TABLE_MAIN_ENTRY_A_1 = 1002;
constexpr uint64_t K_EID_UMQ_TABLE_MAIN_ENTRY_B_0 = 2001;

constexpr size_t K_EID_UMQ_TABLE_EXPECTED_ENTRIES_COUNT_0 = 2;
constexpr size_t K_EID_UMQ_TABLE_EXPECTED_ENTRIES_COUNT_1 = 1;

constexpr int K_SOCKET_FD_EPOLL_TABLE_FD = 100;
constexpr int K_SOCKET_FD_EPOLL_TABLE_INVALID_EPOLL_FD = -1;
constexpr uint64_t K_SOCKET_FD_EPOLL_TABLE_EPOLL_FD = 9001;

constexpr int K_UMQ_STATE_SET_OK_RETURN = 0;
}  // namespace

class ShareJfrCommonTest : public testing::Test {
public:
    void SetUp() override
    {
        Brpc::EidUmqTable::Clean();
        Brpc::MainSubUmqTable::Clean();
        Brpc::SocketFdEpollTable::Remove(K_SOCKET_FD_EPOLL_CLEANUP_FD_0);
        Brpc::SocketFdEpollTable::Remove(K_SOCKET_FD_EPOLL_CLEANUP_FD_1);
        Brpc::SocketFdEpollTable::Remove(K_SOCKET_FD_EPOLL_CLEANUP_FD_2);
    }

    void TearDown() override
    {
        Brpc::EidUmqTable::Clean();
        Brpc::MainSubUmqTable::Clean();
        Brpc::SocketFdEpollTable::Remove(K_SOCKET_FD_EPOLL_CLEANUP_FD_0);
        Brpc::SocketFdEpollTable::Remove(K_SOCKET_FD_EPOLL_CLEANUP_FD_1);
        Brpc::SocketFdEpollTable::Remove(K_SOCKET_FD_EPOLL_CLEANUP_FD_2);
        GlobalMockObject::verify();
    }
};

TEST_F(ShareJfrCommonTest, UmqEidEqualityAndHashWork)
{
    umq_eid_t eidA = MakeEid(K_EID_SEED_A);
    umq_eid_t eidB = MakeEid(K_EID_SEED_A);
    umq_eid_t eidC = MakeEid(K_EID_SEED_B);

    EXPECT_TRUE(eidA == eidB);
    EXPECT_FALSE(eidA != eidB);
    EXPECT_TRUE(eidA != eidC);

    Brpc::UmqEidHash hasher;
    EXPECT_EQ(hasher(eidA), hasher(eidB));
}

TEST_F(ShareJfrCommonTest, EidUmqTableAddGetRemoveAndRemoveMainUmq)
{
    umq_eid_t eidA = MakeEid(K_EID_SEED_A_10);
    umq_eid_t eidB = MakeEid(K_EID_SEED_A_20);

    Brpc::EidUmqTable::Add(eidA, K_EID_UMQ_TABLE_MAIN_ENTRY_A_0);
    Brpc::EidUmqTable::Add(eidA, K_EID_UMQ_TABLE_MAIN_ENTRY_A_1);
    Brpc::EidUmqTable::Add(eidB, K_EID_UMQ_TABLE_MAIN_ENTRY_B_0);

    std::vector<uint64_t> out {};
    ASSERT_TRUE(Brpc::EidUmqTable::Get(eidA, out));
    ASSERT_EQ(out.size(), K_EID_UMQ_TABLE_EXPECTED_ENTRIES_COUNT_0);
    EXPECT_EQ(out[0], K_EID_UMQ_TABLE_MAIN_ENTRY_A_0);
    EXPECT_EQ(out[1], K_EID_UMQ_TABLE_MAIN_ENTRY_A_1);

    Brpc::EidUmqTable::RemoveMainUmq(K_EID_UMQ_TABLE_MAIN_ENTRY_A_0);
    out.clear();
    ASSERT_TRUE(Brpc::EidUmqTable::Get(eidA, out));
    ASSERT_EQ(out.size(), K_EID_UMQ_TABLE_EXPECTED_ENTRIES_COUNT_1);
    EXPECT_EQ(out[0], K_EID_UMQ_TABLE_MAIN_ENTRY_A_1);

    Brpc::EidUmqTable::Remove(eidA);
    out.clear();
    EXPECT_FALSE(Brpc::EidUmqTable::Get(eidA, out));
    EXPECT_TRUE(Brpc::EidUmqTable::Get(eidB, out));
}

TEST_F(ShareJfrCommonTest, SocketFdEpollTableSetGetContainsRemove)
{
    int epollFd = K_SOCKET_FD_EPOLL_TABLE_INVALID_EPOLL_FD;
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Contains(K_SOCKET_FD_EPOLL_TABLE_FD));
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Get(K_SOCKET_FD_EPOLL_TABLE_FD, epollFd));

    Brpc::SocketFdEpollTable::Set(K_SOCKET_FD_EPOLL_TABLE_FD, K_SOCKET_FD_EPOLL_TABLE_EPOLL_FD);
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Contains(K_SOCKET_FD_EPOLL_TABLE_FD));
    ASSERT_TRUE(Brpc::SocketFdEpollTable::Get(K_SOCKET_FD_EPOLL_TABLE_FD, epollFd));
    EXPECT_EQ(epollFd, K_SOCKET_FD_EPOLL_TABLE_EPOLL_FD);

    Brpc::SocketFdEpollTable::Remove(K_SOCKET_FD_EPOLL_TABLE_FD);
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Contains(K_SOCKET_FD_EPOLL_TABLE_FD));
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Get(K_SOCKET_FD_EPOLL_TABLE_FD, epollFd));
}

TEST_F(ShareJfrCommonTest, MainSubUmqTableAddGetContainsRemoveAndClean)
{
    constexpr uint64_t K_MAIN_UMQ_A = 3001;
    constexpr uint64_t K_MAIN_UMQ_B = 3002;
    constexpr uint64_t K_SUB_UMQ_A_1 = 4001;
    constexpr uint64_t K_SUB_UMQ_A_2 = 4002;
    constexpr uint64_t K_SUB_UMQ_B_1 = 5001;

    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_A, K_SUB_UMQ_A_1);
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_A, K_SUB_UMQ_A_2);
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_B, K_SUB_UMQ_B_1);

    std::vector<uint64_t> subUmqs {};
    ASSERT_TRUE(Brpc::MainSubUmqTable::Contains(K_MAIN_UMQ_A));
    ASSERT_TRUE(Brpc::MainSubUmqTable::GetSubUmqs(K_MAIN_UMQ_A, subUmqs));
    ASSERT_EQ(subUmqs.size(), K_EID_UMQ_TABLE_EXPECTED_ENTRIES_COUNT_0);

    Brpc::MainSubUmqTable::RemoveSubUmq(K_MAIN_UMQ_A, K_SUB_UMQ_A_1);
    subUmqs.clear();
    ASSERT_TRUE(Brpc::MainSubUmqTable::GetSubUmqs(K_MAIN_UMQ_A, subUmqs));
    ASSERT_EQ(subUmqs.size(), K_EID_UMQ_TABLE_EXPECTED_ENTRIES_COUNT_1);
    EXPECT_EQ(subUmqs[0], K_SUB_UMQ_A_2);

    MOCKER_CPP(umq_state_set).stubs().will(returnValue(int(K_UMQ_STATE_SET_OK_RETURN)));
    Brpc::MainSubUmqTable::Clean();

    EXPECT_FALSE(Brpc::MainSubUmqTable::Contains(K_MAIN_UMQ_A));
    EXPECT_FALSE(Brpc::MainSubUmqTable::Contains(K_MAIN_UMQ_B));
}

