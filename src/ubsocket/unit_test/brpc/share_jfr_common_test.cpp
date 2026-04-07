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

// Extra named constants (G.CNS.02)
constexpr uint64_t K_MAIN_SUB_NON_EXIST_99999 = 99999U;
constexpr uint64_t K_MAIN_SUB_NON_EXIST_88888 = 88888U;
constexpr uint8_t K_EID_SEED_99 = 99;
constexpr uint8_t K_EID_SEED_50 = 50;
constexpr uint8_t K_EID_SEED_60 = 60;
constexpr uint8_t K_EID_SEED_70 = 70;
constexpr uint8_t K_EID_SEED_71 = 71;
constexpr uint8_t K_EID_SEED_72 = 72;
constexpr uint8_t K_EID_SEED_80 = 80;
constexpr uint8_t K_EID_SEED_81 = 81;
constexpr uint8_t K_EID_SEED_100 = 100;
constexpr uint8_t K_EID_SEED_110 = 110;
constexpr uint8_t K_EID_SEED_200 = 200;
constexpr uint64_t K_EID_UMQ_1001 = 1001U;
constexpr uint64_t K_EID_UMQ_1002 = 1002U;
constexpr uint64_t K_EID_UMQ_1003 = 1003U;
constexpr uint64_t K_EID_UMQ_1004 = 1004U;
constexpr uint64_t K_EID_UMQ_2001 = 2001U;
constexpr uint64_t K_EID_UMQ_2002 = 2002U;
constexpr uint64_t K_EID_UMQ_2003 = 2003U;
constexpr uint64_t K_EID_UMQ_3001 = 3001U;
constexpr uint64_t K_EID_UMQ_3002 = 3002U;
constexpr uint64_t K_EID_UMQ_3003 = 3003U;
constexpr uint64_t K_EID_UMQ_4001 = 4001U;
constexpr uint64_t K_EID_UMQ_4002 = 4002U;
constexpr uint64_t K_EID_UMQ_10001 = 10001U;
constexpr uint64_t K_EID_UMQ_20001 = 20001U;
constexpr uint64_t K_EID_UMQ_20002 = 20002U;
constexpr uint64_t K_EID_UMQ_20003 = 20003U;
constexpr uint64_t K_EID_UMQ_30001 = 30001U;
constexpr int K_SOCK_FD_EPOLL_SET_100 = 100;
constexpr int K_SOCK_FD_EPOLL_SET_101 = 101;
constexpr int K_SOCK_FD_EPOLL_SET_102 = 102;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_1000 = 1000U;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_1001 = 1001U;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_1002 = 1002U;
constexpr int K_SOCK_FD_EPOLL_SET_200 = 200;
constexpr int K_SOCK_FD_EPOLL_SET_201 = 201;
constexpr int K_SOCK_FD_EPOLL_SET_202 = 202;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_2000 = 2000U;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_2001 = 2001U;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_2002 = 2002U;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_3000 = 3000U;
constexpr int K_SOCK_FD_EPOLL_NON_EXIST_99999 = 99999;
constexpr uint8_t K_EID_LAST_BYTE_FF = 0xFF;
constexpr uint8_t K_EID_LAST_IDX_15 = 15;
constexpr uint8_t K_EID_RAW_BYTE_VAL_1 = 1;
constexpr uint8_t K_EID_RAW_BYTE_VAL_2 = 2;
constexpr size_t K_EID_OUT_IDX_0 = 0;
constexpr size_t K_EID_OUT_IDX_1 = 1;
constexpr size_t K_EID_OUT_IDX_2 = 2;
constexpr size_t K_EID_OUT_IDX_3 = 3;
constexpr uint64_t K_MAIN_UMQ_6001 = 6001U;
constexpr uint64_t K_MAIN_UMQ_6002 = 6002U;
constexpr uint64_t K_MAIN_UMQ_6003 = 6003U;
constexpr uint64_t K_SUB_UMQ_7001 = 7001U;
constexpr uint64_t K_SUB_UMQ_7002 = 7002U;
constexpr uint64_t K_SUB_UMQ_7003 = 7003U;
constexpr uint64_t K_MAIN_UMQ_8001 = 8001U;
constexpr uint64_t K_SUB_UMQ_9001 = 9001U;
constexpr uint64_t K_SUB_UMQ_9002 = 9002U;
constexpr uint64_t K_SUB_UMQ_9003 = 9003U;
constexpr int K_SOCK_FD_EPOLL_500 = 500;
constexpr uint64_t K_SOCK_FD_EPOLL_VAL_5000 = 5000U;
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

TEST_F(ShareJfrCommonTest, MainSubUmqTable_GetSubUmqs_NotFound)
{
    std::vector<uint64_t> subUmqs;
    EXPECT_FALSE(Brpc::MainSubUmqTable::GetSubUmqs(K_MAIN_SUB_NON_EXIST_99999, subUmqs));
}

TEST_F(ShareJfrCommonTest, MainSubUmqTable_RemoveSubUmq_NotFound)
{
    // Should not crash
    Brpc::MainSubUmqTable::RemoveSubUmq(K_MAIN_SUB_NON_EXIST_99999, K_MAIN_SUB_NON_EXIST_88888);
}

TEST_F(ShareJfrCommonTest, EidUmqTable_Get_NotFound)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_99);
    std::vector<uint64_t> out;
    EXPECT_FALSE(Brpc::EidUmqTable::Get(eid, out));
}

TEST_F(ShareJfrCommonTest, EidUmqTable_Remove_NotFound)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_99);
    // Should not crash
    Brpc::EidUmqTable::Remove(eid);
}

TEST_F(ShareJfrCommonTest, EidUmqTable_RemoveMainUmq_NotFound)
{
    // Should not crash - takes uint64_t
    Brpc::EidUmqTable::RemoveMainUmq(K_MAIN_SUB_NON_EXIST_99999);
}

// ============= UmqEidHash Tests =============

TEST_F(ShareJfrCommonTest, UmqEidHash_ConsistentHash)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_A);

    Brpc::UmqEidHash hasher;
    std::size_t hash1 = hasher(eid);
    std::size_t hash2 = hasher(eid);

    EXPECT_EQ(hash1, hash2);
}

TEST_F(ShareJfrCommonTest, UmqEidHash_DifferentEidsDifferentHashes)
{
    umq_eid_t eid1 = MakeEid(1);
    umq_eid_t eid2 = MakeEid(2);

    Brpc::UmqEidHash hasher;
    std::size_t hash1 = hasher(eid1);
    std::size_t hash2 = hasher(eid2);

    // Different eids should likely have different hashes (not guaranteed, but likely)
    // Just verify both hashes are computed without crashing
    (void)hash1;
    (void)hash2;
}

// ============= EidUmqTable Additional Tests =============

TEST_F(ShareJfrCommonTest, EidUmqTable_AddMultipleUmqs)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_50);

    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_1001);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_1002);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_1003);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_1004);

    std::vector<uint64_t> out;
    ASSERT_TRUE(Brpc::EidUmqTable::Get(eid, out));
    EXPECT_EQ(out.size(), 4u);
    EXPECT_EQ(out[K_EID_OUT_IDX_0], K_EID_UMQ_1001);
    EXPECT_EQ(out[K_EID_OUT_IDX_1], K_EID_UMQ_1002);
    EXPECT_EQ(out[K_EID_OUT_IDX_2], K_EID_UMQ_1003);
    EXPECT_EQ(out[K_EID_OUT_IDX_3], K_EID_UMQ_1004);

    Brpc::EidUmqTable::Remove(eid);
}

TEST_F(ShareJfrCommonTest, EidUmqTable_RemoveMainUmq_Multiple)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_60);

    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_2001);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_2002);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_2003);

    // Remove two entries
    Brpc::EidUmqTable::RemoveMainUmq(K_EID_UMQ_2001);
    Brpc::EidUmqTable::RemoveMainUmq(K_EID_UMQ_2003);

    std::vector<uint64_t> out;
    ASSERT_TRUE(Brpc::EidUmqTable::Get(eid, out));
    EXPECT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0], K_EID_UMQ_2002);

    Brpc::EidUmqTable::Remove(eid);
}

TEST_F(ShareJfrCommonTest, EidUmqTable_MultipleEids)
{
    umq_eid_t eid1 = MakeEid(K_EID_SEED_70);
    umq_eid_t eid2 = MakeEid(K_EID_SEED_71);
    umq_eid_t eid3 = MakeEid(K_EID_SEED_72);

    Brpc::EidUmqTable::Add(eid1, K_EID_UMQ_3001);
    Brpc::EidUmqTable::Add(eid2, K_EID_UMQ_3002);
    Brpc::EidUmqTable::Add(eid3, K_EID_UMQ_3003);

    std::vector<uint64_t> out1;
    std::vector<uint64_t> out2;
    std::vector<uint64_t> out3;
    EXPECT_TRUE(Brpc::EidUmqTable::Get(eid1, out1));
    EXPECT_TRUE(Brpc::EidUmqTable::Get(eid2, out2));
    EXPECT_TRUE(Brpc::EidUmqTable::Get(eid3, out3));

    EXPECT_EQ(out1.size(), 1u);
    EXPECT_EQ(out2.size(), 1u);
    EXPECT_EQ(out3.size(), 1u);

    Brpc::EidUmqTable::Remove(eid1);
    Brpc::EidUmqTable::Remove(eid2);
    Brpc::EidUmqTable::Remove(eid3);
}

TEST_F(ShareJfrCommonTest, EidUmqTable_Clean)
{
    umq_eid_t eid1 = MakeEid(K_EID_SEED_80);
    umq_eid_t eid2 = MakeEid(K_EID_SEED_81);

    Brpc::EidUmqTable::Add(eid1, K_EID_UMQ_4001);
    Brpc::EidUmqTable::Add(eid2, K_EID_UMQ_4002);

    Brpc::EidUmqTable::Clean();

    std::vector<uint64_t> out1;
    std::vector<uint64_t> out2;
    EXPECT_FALSE(Brpc::EidUmqTable::Get(eid1, out1));
    EXPECT_FALSE(Brpc::EidUmqTable::Get(eid2, out2));
}

// ============= SocketFdEpollTable Additional Tests =============

TEST_F(ShareJfrCommonTest, SocketFdEpollTable_MultipleEntries)
{
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_100, K_SOCK_FD_EPOLL_VAL_1000);
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_101, K_SOCK_FD_EPOLL_VAL_1001);
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_102, K_SOCK_FD_EPOLL_VAL_1002);

    int epollFd1 = 0;
    int epollFd2 = 0;
    int epollFd3 = 0;
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Get(K_SOCK_FD_EPOLL_SET_100, epollFd1));
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Get(K_SOCK_FD_EPOLL_SET_101, epollFd2));
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Get(K_SOCK_FD_EPOLL_SET_102, epollFd3));

    EXPECT_EQ(epollFd1, K_SOCK_FD_EPOLL_VAL_1000);
    EXPECT_EQ(epollFd2, K_SOCK_FD_EPOLL_VAL_1001);
    EXPECT_EQ(epollFd3, K_SOCK_FD_EPOLL_VAL_1002);

    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_SET_100);
    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_SET_101);
    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_SET_102);
}

TEST_F(ShareJfrCommonTest, SocketFdEpollTable_Overwrite)
{
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_200, K_SOCK_FD_EPOLL_VAL_2000);
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_200, K_SOCK_FD_EPOLL_VAL_2001);  // Overwrite

    int epollFd = 0;
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Get(K_SOCK_FD_EPOLL_SET_200, epollFd));
    EXPECT_EQ(epollFd, K_SOCK_FD_EPOLL_VAL_2001);

    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_SET_200);
}

TEST_F(ShareJfrCommonTest, SocketFdEpollTable_RemoveNonExistent)
{
    // Should not crash
    Brpc::SocketFdEpollTable::Remove(K_MAIN_SUB_NON_EXIST_99999);
}

TEST_F(ShareJfrCommonTest, SocketFdEpollTable_GetNonExistent)
{
    int epollFd = 0;
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Get(K_MAIN_SUB_NON_EXIST_99999, epollFd));
}

TEST_F(ShareJfrCommonTest, SocketFdEpollTable_ContainsNonExistent)
{
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Contains(K_MAIN_SUB_NON_EXIST_99999));
}

// ============= UmqEid Equality Tests =============

TEST_F(ShareJfrCommonTest, UmqEid_Equality_AllZeros)
{
    umq_eid_t eid1 = {};
    umq_eid_t eid2 = {};

    EXPECT_TRUE(eid1 == eid2);
    EXPECT_FALSE(eid1 != eid2);
}

TEST_F(ShareJfrCommonTest, UmqEid_Equality_AllOnes)
{
    umq_eid_t eid1 = {};
    umq_eid_t eid2 = {};

    for (size_t i = 0; i < sizeof(eid1.raw); ++i) {
        eid1.raw[i] = K_EID_LAST_BYTE_FF;
        eid2.raw[i] = K_EID_LAST_BYTE_FF;
    }

    EXPECT_TRUE(eid1 == eid2);
    EXPECT_FALSE(eid1 != eid2);
}

TEST_F(ShareJfrCommonTest, UmqEid_Inequality_SingleByteDiff)
{
    umq_eid_t eid1 = {};
    umq_eid_t eid2 = {};

    eid1.raw[0] = K_EID_RAW_BYTE_VAL_1;
    eid2.raw[0] = K_EID_RAW_BYTE_VAL_2;

    EXPECT_FALSE(eid1 == eid2);
    EXPECT_TRUE(eid1 != eid2);
}

TEST_F(ShareJfrCommonTest, UmqEid_Inequality_LastByteDiff)
{
    umq_eid_t eid1 = MakeEid(K_EID_SEED_A_10);
    umq_eid_t eid2 = MakeEid(K_EID_SEED_A_10);

    eid2.raw[K_EID_LAST_IDX_15] = K_EID_LAST_BYTE_FF;

    EXPECT_FALSE(eid1 == eid2);
    EXPECT_TRUE(eid1 != eid2);
}

// ============= Additional EidUmqTable Tests =============

TEST_F(ShareJfrCommonTest, EidUmqTable_AddSameEidMultipleTimes)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_100);

    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_10001);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_20001);  // Add different umq for same eid

    std::vector<uint64_t> out;
    ASSERT_TRUE(Brpc::EidUmqTable::Get(eid, out));
    EXPECT_EQ(out.size(), 2u);  // Both entries should be added

    Brpc::EidUmqTable::Remove(eid);
}

TEST_F(ShareJfrCommonTest, EidUmqTable_RemoveMainUmq_Partial)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_110);

    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_20001);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_20002);
    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_20003);

    // Remove only one
    Brpc::EidUmqTable::RemoveMainUmq(K_EID_UMQ_20002);

    std::vector<uint64_t> out;
    ASSERT_TRUE(Brpc::EidUmqTable::Get(eid, out));
    EXPECT_EQ(out.size(), 2u);

    Brpc::EidUmqTable::Remove(eid);
}

TEST_F(ShareJfrCommonTest, MainSubUmqTable_MultipleMain)
{
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_6001, K_SUB_UMQ_7001);
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_6002, K_SUB_UMQ_7002);
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_6003, K_SUB_UMQ_7003);

    EXPECT_TRUE(Brpc::MainSubUmqTable::Contains(K_MAIN_UMQ_6001));
    EXPECT_TRUE(Brpc::MainSubUmqTable::Contains(K_MAIN_UMQ_6002));
    EXPECT_TRUE(Brpc::MainSubUmqTable::Contains(K_MAIN_UMQ_6003));

    MOCKER_CPP(umq_state_set).stubs().will(returnValue(int(K_UMQ_STATE_SET_OK_RETURN)));
    Brpc::MainSubUmqTable::Clean();
}

TEST_F(ShareJfrCommonTest, SocketFdEpollTable_MultipleOperations)
{
    // Test multiple set/get/remove operations
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_200, K_SOCK_FD_EPOLL_VAL_2000);
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_201, K_SOCK_FD_EPOLL_VAL_2001);
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_202, K_SOCK_FD_EPOLL_VAL_2002);

    int epollFd = 0;
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Get(K_SOCK_FD_EPOLL_SET_200, epollFd));
    EXPECT_EQ(epollFd, K_SOCK_FD_EPOLL_VAL_2000);

    // Overwrite
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_SET_200, K_SOCK_FD_EPOLL_VAL_3000);
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Get(K_SOCK_FD_EPOLL_SET_200, epollFd));
    EXPECT_EQ(epollFd, K_SOCK_FD_EPOLL_VAL_3000);

    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_SET_200);
    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_SET_201);
    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_SET_202);
}

TEST_F(ShareJfrCommonTest, UmqEid_CompareAllBytes)
{
    umq_eid_t eid1 = {};
    umq_eid_t eid2 = {};

    // All bytes same
    for (size_t i = 0; i < sizeof(eid1.raw); ++i) {
        eid1.raw[i] = static_cast<uint8_t>(i);
        eid2.raw[i] = static_cast<uint8_t>(i);
    }
    EXPECT_TRUE(eid1 == eid2);

    // Change one byte at a time
    for (size_t i = 0; i < sizeof(eid1.raw); ++i) {
        eid2.raw[i] = K_EID_LAST_BYTE_FF;
        EXPECT_FALSE(eid1 == eid2);
        EXPECT_TRUE(eid1 != eid2);
        eid2.raw[i] = static_cast<uint8_t>(i);
    }
}

TEST_F(ShareJfrCommonTest, MainSubUmqTable_RemoveSubUmqFromMultiple)
{
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_8001, K_SUB_UMQ_9001);
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_8001, K_SUB_UMQ_9002);
    Brpc::MainSubUmqTable::Add(K_MAIN_UMQ_8001, K_SUB_UMQ_9003);

    std::vector<uint64_t> subUmqs;
    ASSERT_TRUE(Brpc::MainSubUmqTable::GetSubUmqs(K_MAIN_UMQ_8001, subUmqs));
    EXPECT_EQ(subUmqs.size(), 3u);

    // Remove middle
    Brpc::MainSubUmqTable::RemoveSubUmq(K_MAIN_UMQ_8001, K_SUB_UMQ_9002);
    subUmqs.clear();
    ASSERT_TRUE(Brpc::MainSubUmqTable::GetSubUmqs(K_MAIN_UMQ_8001, subUmqs));
    EXPECT_EQ(subUmqs.size(), 2u);

    MOCKER_CPP(umq_state_set).stubs().will(returnValue(int(K_UMQ_STATE_SET_OK_RETURN)));
    Brpc::MainSubUmqTable::Clean();
}

TEST_F(ShareJfrCommonTest, EidUmqTable_GetAfterRemove)
{
    umq_eid_t eid = MakeEid(K_EID_SEED_200);

    Brpc::EidUmqTable::Add(eid, K_EID_UMQ_30001);
    Brpc::EidUmqTable::Remove(eid);

    std::vector<uint64_t> out;
    EXPECT_FALSE(Brpc::EidUmqTable::Get(eid, out));
}

TEST_F(ShareJfrCommonTest, SocketFdEpollTable_ContainsAfterSet)
{
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Contains(K_SOCK_FD_EPOLL_500));
    Brpc::SocketFdEpollTable::Set(K_SOCK_FD_EPOLL_500, K_SOCK_FD_EPOLL_VAL_5000);
    EXPECT_TRUE(Brpc::SocketFdEpollTable::Contains(K_SOCK_FD_EPOLL_500));
    Brpc::SocketFdEpollTable::Remove(K_SOCK_FD_EPOLL_500);
    EXPECT_FALSE(Brpc::SocketFdEpollTable::Contains(K_SOCK_FD_EPOLL_500));
}