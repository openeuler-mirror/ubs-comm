/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "hcom.h"
#include "net_shm_sync_endpoint.h"
#include "net_shm_async_endpoint.h"

namespace ock {
namespace hcom {
uint8_t mockData[8];
UBSHcomNetTransHeader mockReq{};

class TestNetShmEndpoint : public testing::Test {
public:
    TestNetShmEndpoint();
    virtual void SetUp(void);
    virtual void TearDown(void);
};

TestNetShmEndpoint::TestNetShmEndpoint() {}

static HResult MockGetPeerDataAddressByOffset(uint64_t offset, uintptr_t &address)
{
    address = reinterpret_cast<uintptr_t>(&mockReq);
    offset = 0;
    return 0;
}

static HResult MockDequeueEvent(int32_t timeout, ShmEvent &opEvent)
{
    opEvent.opType = static_cast<ShmOpContextInfo::ShmOpType>(mockData[0]);
    opEvent.peerChannelAddress = 0;
    return 0;
}
void TestNetShmEndpoint::SetUp() {}

void TestNetShmEndpoint::TearDown()
{
    GlobalMockObject::verify();
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointShmPostSend)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    // mWorker create
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *mWorker = new (std::nothrow) ShmWorker("NetAsyncEndpointShmPostSend", indexWorker, options, opMemPool,
        opCtxMemPool, sglOpMemPool);
    ASSERT_NE(mWorker, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), mWorker, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&indexWorker);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;

    MOCKER_CPP(&ShmWorker::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0))
        .then(returnValue(static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE)));
    MOCKER_CPP(&AesGcm128::Encrypt,
        bool (AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    ep->mIsNeedEncrypt = 1;
    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));

    ep->mIsNeedEncrypt = 0;
    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointShmPostSendTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSendTwo", 0, NN_NO128, NN_NO4, ch);
    // mWorker create
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *mWorker = new (std::nothrow) ShmWorker("NetAsyncEndpointShmPostSendTwo", indexWorker, options, opMemPool,
        opCtxMemPool, sglOpMemPool);
    ASSERT_NE(mWorker, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), mWorker, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&indexWorker);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;

    MOCKER_CPP(&ShmWorker::PostSend)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE)));

    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, 0);

    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSend)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSend", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSend", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;
    UBSHcomNetTransOpInfo opInfo{};

    MOCKER_CPP(&AesGcm128::Encrypt,
        bool (AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));
    MOCKER(NetFunc::CalcHeaderCrc32, uint32_t(UBSHcomNetTransHeader *))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(0)));
    MOCKER_CPP(&ShmSyncEndpoint::PostSend)
        .stubs()
        .will(returnValue(1));

    ep->mIsNeedEncrypt = true;
    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));

    ep->mIsNeedEncrypt = false;
    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSendTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSendTwo", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSendTwo", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // shmEp create
    ShmMRHandleMap map;
    UBSHcomNetWorkerIndex index;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;
    UBSHcomNetTransOpInfo opInfo{};

    MOCKER(NetFunc::CalcHeaderCrc32, uint32_t(UBSHcomNetTransHeader *))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(0)));
    MOCKER_CPP(&ShmSyncEndpoint::PostSend)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE)));

    ep->mIsNeedEncrypt = false;
    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, 0);
    ret = ep->PostSend(0, request, 0);
    EXPECT_EQ(ret, static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSendThree)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSendThree", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSendThree", 0,
        SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;
    UBSHcomNetTransOpInfo opInfo{};

    MOCKER_CPP(&ShmSyncEndpoint::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER(NetFunc::CalcHeaderCrc32, uint32_t(UBSHcomNetTransHeader *))
        .stubs()
        .will(returnValue(static_cast<uint32_t>(0)));

    ep->mIsNeedEncrypt = false;

    ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, 1);

    ret = ep->PostSend(0, request, opInfo);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSendRaw)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSendRaw", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSendRaw", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;
    ep->mSegSize = NN_NO128;

    MOCKER_CPP(&ShmSyncEndpoint::PostSend)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    ep->mIsNeedEncrypt = true;
    ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));

    ep->mIsNeedEncrypt = false;
    ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSendRawTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSendRawTwo", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSendRawTwo", 0,
        SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    ep->mAllowedSize = NN_NO128;
    ep->mSegSize = NN_NO128;
    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    request.size = 1;

    MOCKER_CPP(&ShmSyncEndpoint::PostSend)
        .stubs()
        .will(returnValue(0));

    ep->mIsNeedEncrypt = false;
    ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSendRawThree)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSendRawThree", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSendRawThree", 0,
        SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomNetTransRequest request;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;
    ep->mSegSize = NN_NO128;

    MOCKER_CPP(&ShmChannel::DCGetFreeBuck)
        .stubs()
        .will(returnValue(1));

    ret = ep->PostSendRaw(request, 0);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSendRawSgl)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSendRawSgl", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSendRawSgl", 0,
        SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetSyncEndpointShmPostSendRawSgl", false,
        SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), driver, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransSglRequest request{};
    UBSHcomNetTransSgeIov iov;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&index);
    ep->mSegSize = NN_NO128;

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::PostSendRawSgl)
        .stubs()
        .will(returnValue(1));
    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));

    ep->mIsNeedEncrypt = true;
    ret = ep->PostSendRawSgl(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_ENCRYPT_FAILED));

    ep->mIsNeedEncrypt = false;
    ret = ep->PostSendRawSgl(request, 1);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostSendRawSglTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostSendRawSglTwo", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostSendRawSglTwo", 0,
        SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetSyncEndpointShmPostSendRawSglTwo", false,
        SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), driver, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransSglRequest request{};
    UBSHcomNetTransSgeIov iov;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&index);
    ep->mSegSize = NN_NO128;

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::PostSendRawSgl)
        .stubs()
        .will(returnValue(0));

    ep->mIsNeedEncrypt = false;
    ret = ep->PostSendRawSgl(request, 1);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostRead)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostRead", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostRead", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetSyncEndpointShmPostRead", false, SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), driver, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransRequest request{};
    request.size = 1;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    ep->mSegSize = NN_NO128;

    UBSHcomNetTransOpInfo opInfo{};

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::PostRead, HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransRequest &,
        ShmMRHandleMap &))
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));

    ret = ep->PostRead(request);
    EXPECT_EQ(ret, 1);

    ret = ep->PostRead(request);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostReadTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmPostReadTwo", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostReadTwo", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetSyncEndpointShmPostReadTwo", false, SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), driver, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransSglRequest request{};
    UBSHcomNetTransSgeIov iov;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&index);
    ep->mSegSize = NN_NO128;

    UBSHcomNetTransOpInfo opInfo{};

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::PostRead, HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransSglRequest &,
        ShmMRHandleMap &))
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));

    ret = ep->PostRead(request);
    EXPECT_EQ(ret, 1);

    ret = ep->PostRead(request);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostWrite)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("NetSyncEndpointShmPostWrite", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostWrite", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetSyncEndpointShmPostWrite", false, SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), driver, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransRequest request{};
    request.size = 1;
    request.lAddress = reinterpret_cast<uintptr_t>(&index);
    ep->mSegSize = NN_NO128;

    UBSHcomNetTransOpInfo opInfo{};

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::PostWrite, HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransRequest &,
        ShmMRHandleMap &))
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));

    ret = ep->PostWrite(request);
    EXPECT_EQ(ret, 1);

    ret = ep->PostWrite(request);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmPostWriteTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("NetSyncEndpointShmPostWriteTwo", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointShmPostWriteTwo", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetSyncEndpointShmPostWriteTwo", false, SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(ch->Id(), ch.Get(), driver, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransSglRequest request{};
    UBSHcomNetTransSgeIov iov;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&index);
    ep->mSegSize = NN_NO128;

    UBSHcomNetTransOpInfo opInfo{};

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion).stubs().will(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::PostWrite, HResult(ShmSyncEndpoint::*)(ShmChannel *, const UBSHcomNetTransSglRequest &,
        ShmMRHandleMap &))
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));

    ret = ep->PostWrite(request);
    EXPECT_EQ(ret, 1);

    ret = ep->PostWrite(request);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointSetEpOption)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomEpOptions epOptions{};

    ret = ep->SetEpOption(epOptions);
    EXPECT_EQ(ret, 0);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointGetSendQueueCount)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    ret = ep->GetSendQueueCount();
    EXPECT_EQ(ret, 0);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointPeerIpAndPort)
{
    std::string ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    ret = ep->PeerIpAndPort();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointPeerIpAndPortTwo)
{
    int ret;
    std::string result;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointPeerIpAndPortTwo", 0, NN_NO128, NN_NO4, ch);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    result = ep->PeerIpAndPort();
    EXPECT_EQ(result, "");
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointUdsName)
{
    std::string ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    ret = ep->UdsName();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointUdsNameTwo)
{
    int ret;
    std::string result;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointUdsNameTwo", 0, NN_NO128, NN_NO4, ch);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    result = ep->UdsName();
    EXPECT_EQ(result, "");
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointInvalidOperation)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomNetResponseContext ctx{};

    ret = ep->WaitCompletion(0);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_OPERATION));

    ret = ep->Receive(0, ctx);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_OPERATION));
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointInvalidOperationTwo)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomNetResponseContext ctx{};

    ret = ep->ReceiveRaw(0, ctx);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_OPERATION));
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointEnableEncrypt)
{
    // shmSecrets create
    NetSecrets shmSecrets{};
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    UBSHcomNetDriverOptions options;
    options.cipherSuite = AES_GCM_256;
    ep->EnableEncrypt(options);
    EXPECT_EQ(ep->mAes.mCipherSuite, AES_GCM_256);

    ep->SetSecrets(shmSecrets);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointEstimatedEncryptLen)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen)
        .stubs()
        .will(returnValue(1));
    ret = ep->EstimatedEncryptLen(0);
    EXPECT_EQ(ret, 0);

    ret = ep->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointEstimatedEncryptLenTwo)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen)
        .stubs()
        .will(returnValue(1));

    ep->mIsNeedEncrypt = true;
    ret = ep->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointEncrypt)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen;

    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));
    ret = ep->Encrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    ep->mIsNeedEncrypt = true;
    ret = ep->Encrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointEncryptTwo)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen = 0;

    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(true));

    ep->mIsNeedEncrypt = true;
    ret = ep->Encrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointEstimatedDecryptLen)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&AesGcm128::GetRawLen)
        .stubs()
        .will(returnValue(1));
    ret = ep->EstimatedDecryptLen(1);
    EXPECT_EQ(ret, 0);

    ep->mIsNeedEncrypt = true;
    ret = ep->EstimatedDecryptLen(1);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointDecrypt)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen;

    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));
    ret = ep->Decrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    ep->mIsNeedEncrypt = true;
    ret = ep->Decrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointDecryptTwo)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen = 0;

    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(true));
    ep->mIsNeedEncrypt = true;
    ret = ep->Decrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointSendFds)
{
    int ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_NEW);
    // SendFds data
    int fds[1] = {1};

    ret = ep->SendFds(fds, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_PARAM_INVALID));

    ret = ep->SendFds(fds, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointReceiveFds)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointReceiveFds", 0, NN_NO128, NN_NO4, ch);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    // ReceiveFds data
    int fds[1] = {1};

    MOCKER_CPP(&ShmChannel::Close).stubs();
    MOCKER_CPP(&ShmChannel::RemoveUserFds)
        .stubs()
        .will(returnValue(0));

    ret = ep->ReceiveFds(fds, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_PARAM_INVALID));

    ret = ep->ReceiveFds(fds, 1, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointReceiveFdsTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointReceiveFdsTwo", 0, NN_NO128, NN_NO4, ch);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(ch->Id(), ch.Get(), 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    // ReceiveFds data
    int fds[1] = {1};

    MOCKER_CPP(&ShmChannel::RemoveUserFds)
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&ShmChannel::Close).stubs();

    ep->mState.Set(NEP_ESTABLISHED);
    ret = ep->ReceiveFds(fds, 1, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
    ep->Close();

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointGetRemoteUdsIdInfo)
{
    int ret;
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetAsyncEndpointGetRemoteUdsIdInfo", false,
        SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, driver, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_NEW);
    UBSHcomNetUdsIdInfo idInfo{};

    MOCKER_CPP(&ShmChannel::Close).stubs();
    ret = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));

    ep->mState.Set(NEP_ESTABLISHED);
    driver->mStartOobSvr = false;
    ret = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_UDS_ID_INFO_NOT_SUPPORT));

    ep->Close();
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointGetRemoteUdsIdInfoTwo)
{
    int ret;
    // driver create
    NetDriverShmWithOOB *driver = new (std::nothrow) NetDriverShmWithOOB("NetAsyncEndpointGetRemoteUdsIdInfo", false,
        SHM);
    ASSERT_NE(driver, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, driver, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_NEW);
    UBSHcomNetUdsIdInfo idInfo{};

    MOCKER_CPP(&ShmChannel::Close).stubs();

    ep->mState.Set(NEP_ESTABLISHED);
    driver->mStartOobSvr = true;
    ret = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));

    ep->Close();
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointGetPeerIpPort)
{
    bool ret;
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new (std::nothrow) NetAsyncEndpointShm(0, 0, 0, 0, index, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);

    std::string ip("127.0.0.1");
    uint16_t port = 1234;

    ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointSetEpOption)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new (std::nothrow) ShmSyncEndpoint("NetSyncEndpointSetEpOption", 0, SHM_EVENT_POLLING);
    ASSERT_NE(shmEp, nullptr);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new (std::nothrow) NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ASSERT_NE(ep, nullptr);
    ep->mState.Set(NEP_ESTABLISHED);
    UBSHcomEpOptions epOptions{};

    ret = ep->SetEpOption(epOptions);
    EXPECT_EQ(ret, 0);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointGetSendQueueCount)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointGetSendQueueCount", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    ret = ep->GetSendQueueCount();
    EXPECT_EQ(ret, 0);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointPeerIpAndPort)
{
    std::string ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointPeerIpAndPort", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    ret = ep->PeerIpAndPort();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointPeerIpAndPortTwo)
{
    int ret;
    std::string result;
    // mShmCh create
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("NetSyncEndpointPeerIpAndPort2", 0, NN_NO128, NN_NO4, ch);
    if (NN_UNLIKELY(ret != NN_OK)) {
        return;
    }
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointPeerIpAndPort2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    result = ep->PeerIpAndPort();
    EXPECT_EQ(result, "");
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointUdsName)
{
    std::string ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointUdsName", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    ret = ep->UdsName();
    EXPECT_EQ(ret, CONST_EMPTY_STRING);
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointUdsNameTwo)
{
    int ret;
    std::string result;
    // mShmCh create
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("NetSyncEndpointUdsName2", 0, NN_NO128, NN_NO4, ch);
    if (NN_UNLIKELY(ret != NN_OK)) {
        return;
    }
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointUdsName2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    result = ep->UdsName();
    EXPECT_EQ(result, "");
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointEnableEncrypt)
{
    // shmSecrets create
    NetSecrets shmSecrets{};
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointEnableEncrypt", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    UBSHcomNetDriverOptions options;
    options.cipherSuite = AES_GCM_256;
    ep->EnableEncrypt(options);
    EXPECT_EQ(ep->mAes.mCipherSuite, AES_GCM_256);

    ep->SetSecrets(shmSecrets);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointEstimatedEncryptLen)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointEstimatedEncryptLen", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen)
        .stubs()
        .will(returnValue(1));
    ret = ep->EstimatedEncryptLen(0);
    EXPECT_EQ(ret, 0);

    ret = ep->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointEstimatedEncryptLenTwo)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointEstimatedEncryptLen2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&AesGcm128::EstimatedEncryptLen)
        .stubs()
        .will(returnValue(1));

    ep->mIsNeedEncrypt = true;
    ret = ep->EstimatedEncryptLen(1);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointEncrypt)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointEncrypt", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen;

    ret = ep->Encrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointEncryptTwo)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointEncrypt2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen = 0;

    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));

    ep->mIsNeedEncrypt = true;
    ret = ep->Encrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    ret = ep->Encrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointEstimatedDecryptLen)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointEstimatedDecryptLen", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&AesGcm128::GetRawLen)
        .stubs()
        .will(returnValue(1));
    ret = ep->EstimatedDecryptLen(1);
    EXPECT_EQ(ret, 0);

    ep->mIsNeedEncrypt = true;
    ret = ep->EstimatedDecryptLen(1);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointDecrypt)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointDecrypt", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen;

    ret = ep->Decrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointDecryptTwo)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointDecrypt2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // Encrypt data
    uint8_t encryptData = 0;
    uint8_t *cipher = reinterpret_cast<uint8_t *>(&index);
    uint64_t cipherLen = 0;

    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));

    ep->mIsNeedEncrypt = true;
    ret = ep->Decrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    ret = ep->Decrypt(&encryptData, 1, cipher, cipherLen);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointSendFds)
{
    int ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointSendFds", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    // SendFds data
    int fds[1] = {1};

    ret = ep->SendFds(fds, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_PARAM_INVALID));

    ret = ep->SendFds(fds, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointReceiveFds)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("NetSyncEndpointReceiveFds", 0, NN_NO128, NN_NO4, ch);
    if (NN_UNLIKELY(ret != NN_OK)) {
        return;
    }
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointReceiveFds", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_NEW);
    // ReceiveFds data
    int fds[1] = {1};

    MOCKER_CPP(&ShmChannel::RemoveUserFds)
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&ShmChannel::Close).stubs();

    ret = ep->ReceiveFds(fds, 0, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_PARAM_INVALID));

    ret = ep->ReceiveFds(fds, 1, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));
    ep->Close();

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointReceiveFdsTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ret = ShmChannel::CreateAndInit("NetSyncEndpointReceiveFds2", 0, NN_NO128, NN_NO4, ch);
    if (NN_UNLIKELY(ret != NN_OK)) {
        return;
    }
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointReceiveFds2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_NEW);
    // ReceiveFds data
    int fds[1] = {1};

    MOCKER_CPP(&ShmChannel::RemoveUserFds)
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&ShmChannel::Close).stubs();

    ep->mState.Set(NEP_ESTABLISHED);
    ret = ep->ReceiveFds(fds, 1, 0);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));
    ep->Close();

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointGetRemoteUdsIdInfo)
{
    int ret;
    // driver create
    NetDriverShmWithOOB *driver = new NetDriverShmWithOOB("NetSyncEndpointGetRemoteUdsIdInfo", false, SHM);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointGetRemoteUdsIdInfo", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, driver, index, shmEp, map);
    ep->mState.Set(NEP_NEW);
    UBSHcomNetUdsIdInfo idInfo{};

    ret = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_EP_NOT_ESTABLISHED));

    ep->mState.Set(NEP_ESTABLISHED);
    driver->mStartOobSvr = false;
    ret = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_UDS_ID_INFO_NOT_SUPPORT));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointGetRemoteUdsIdInfoTwo)
{
    int ret;
    // driver create
    NetDriverShmWithOOB *driver = new NetDriverShmWithOOB("NetSyncEndpointGetRemoteUdsIdInfo2", false, SHM);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointGetRemoteUdsIdInfo2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, driver, index, shmEp, map);
    ep->mState.Set(NEP_NEW);
    UBSHcomNetUdsIdInfo idInfo{};

    ep->mState.Set(NEP_ESTABLISHED);
    driver->mStartOobSvr = true;
    ret = ep->GetRemoteUdsIdInfo(idInfo);
    EXPECT_EQ(ret, static_cast<int>(NN_OK));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointGetPeerIpPort)
{
    bool ret;
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointGetPeerIpPort", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(0, 0, 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    std::string ip("127.0.0.1");
    uint16_t port = 1234;

    ret = ep->GetPeerIpPort(ip, port);
    EXPECT_EQ(ret, false);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointReceive)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointReceive", 0, NN_NO128, NN_NO4, ch);
    // peerChannel create
    ShmChannelPtr peerCh;
    ShmChannel::CreateAndInit("NetSyncEndpointReceive", 0, NN_NO128, NN_NO4, peerCh);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointReceive", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    UBSHcomNetResponseContext ctx{};
    int32_t timeout = 0;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(returnValue(1))
        .then(returnValue(0));
    MOCKER_CPP(&ShmSyncEndpoint::Receive)
        .stubs()
        .will(returnValue(1));

    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = 0;
    ret = ep->Receive(0, ctx);
    EXPECT_EQ(ret, static_cast<int>(NN_ERROR));

    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(peerCh.Get());
    ep->mDelayHandleReceiveEvent.dataOffset = 0;
    ret = ep->Receive(0, ctx);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointWaitCompletion)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointWaitCompletion", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointWaitCompletion", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&ShmSyncEndpoint::DequeueEvent)
        .stubs()
        .will(returnValue(1));

    ret = ep->WaitCompletion(0);
    EXPECT_EQ(ret, 1);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointWaitCompletionTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointWaitCompletion2", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointWaitCompletion2", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&ShmSyncEndpoint::DequeueEvent)
        .stubs()
        .will(invoke(MockDequeueEvent));

    mockData[0] = static_cast<uint8_t>(ShmOpContextInfo::SH_RECEIVE);
    ep->mExistDelayEvent = true;
    ret = ep->WaitCompletion(0);
    EXPECT_EQ(ret, static_cast<int>(SH_ERROR));

    mockData[0] = static_cast<uint8_t>(ShmOpContextInfo::SH_SEND);
    ret = ep->WaitCompletion(0);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointWaitCompletionThree)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointWaitCompletion3", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointWaitCompletion3", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    MOCKER_CPP(&ShmSyncEndpoint::DequeueEvent)
        .stubs()
        .will(invoke(MockDequeueEvent));

    ep->mExistDelayEvent = true;
    mockData[0] = static_cast<uint8_t>(ShmOpContextInfo::SH_SGL_WRITE);
    ret = ep->WaitCompletion(0);
    EXPECT_EQ(ret, 0);

    mockData[0] = static_cast<uint8_t>(ShmOpContextInfo::SH_RECEIVE_RAW);
    ret = ep->WaitCompletion(0);
    EXPECT_EQ(ret, static_cast<int>(SH_ERROR));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointShmPostSendRawSgl)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSendRawSgl", 0, NN_NO128, NN_NO4, ch);
    // mWorker create
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *mWorker = new ShmWorker("NetAsyncEndpointShmPostSendRawSgl", indexWorker, options, opMemPool,
        opCtxMemPool, sglOpMemPool);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new NetAsyncEndpointShm(ch->Id(), ch.Get(), mWorker, 0, index, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransSglRequest request{};
    UBSHcomNetTransSgeIov iov;
    uint32_t data;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&data);
    ep->mSegSize = NN_NO128;

    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));

    MOCKER_CPP(&AesGcm128::Encrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false));
    MOCKER_CPP(&MemoryRegionChecker::Validate)
        .stubs()
        .will(returnValue(0));

    ep->mIsNeedEncrypt = 1;
    ret = ep->PostSendRawSgl(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_MALLOC_FAILED));

    ret = ep->PostSendRawSgl(request, 1);
    EXPECT_EQ(ret, static_cast<int>(NN_INVALID_PARAM));
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointShmPostSendRawSglTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostSendRawSgl2", 0, NN_NO128, NN_NO4, ch);
    // mWorker create
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *mWorker = new ShmWorker("NetAsyncEndpointShmPostSendRawSgl2", indexWorker, options, opMemPool,
        opCtxMemPool, sglOpMemPool);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new NetAsyncEndpointShm(ch->Id(), ch.Get(), mWorker, 0, index, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransSglRequest request{};
    UBSHcomNetTransSgeIov iov;
    uint32_t data;
    request.iovCount = 1;
    request.iov = &iov;
    iov.size = 1;
    iov.lAddress = reinterpret_cast<uintptr_t>(&data);
    ep->mSegSize = NN_NO128;

    MOCKER_CPP(&MemoryRegionChecker::Validate)
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&ShmWorker::PostSendRawSgl)
        .stubs()
        .will(returnValue(static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE)))
        .then(returnValue(static_cast<int>(SH_PEER_FD_ERROR)));

    ep->mIsNeedEncrypt = 0;
    ret = ep->PostSendRawSgl(request, 1);
    EXPECT_EQ(ret, static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE));

    ret = ep->PostSendRawSgl(request, 1);
    EXPECT_EQ(ret, static_cast<int>(SH_PEER_FD_ERROR));
    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetAsyncEndpointShmPostReadTwo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetAsyncEndpointShmPostRead2", 0, NN_NO128, NN_NO4, ch);
    /// driver create
    NetDriverShmWithOOB *driver = new NetDriverShmWithOOB("NetAsyncEndpointShmPostRead2", false, SHM);
    // mWorker create
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *mWorker = new ShmWorker("NetAsyncEndpointShmPostRead2", indexWorker, options, opMemPool,
        opCtxMemPool, sglOpMemPool);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetAsyncEndpointShm *ep = new NetAsyncEndpointShm(ch->Id(), ch.Get(), mWorker, driver, index, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // request create
    UBSHcomNetTransRequest request;
    uint32_t data;
    request.lAddress = reinterpret_cast<uintptr_t>(&data);
    request.size = 1;
    ep->mAllowedSize = NN_NO128;

    MOCKER_CPP(&NetDriverShmWithOOB::ValidateMemoryRegion)
        .stubs()
        .will(returnValue(0));
    MOCKER_CPP(&ShmWorker::PostRead)
        .stubs()
        .will(returnValue(static_cast<int>(SH_OP_CTX_FULL)))
        .then(returnValue(static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE)));

    ep->mIsNeedEncrypt = 0;
    ep->mDefaultTimeout = 1;
    ret = ep->PostRead(request);
    EXPECT_EQ(ret, static_cast<int>(SH_SEND_COMPLETION_CALLBACK_FAILURE));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, SyncReceiveFailWithErrorOpType)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReceiveFailWithErrorOpType", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("SyncReceiveFailWithErrorOpType", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // param init
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(returnValue(1))
        .then(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, 1);

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_ERROR));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, SyncReceiveFailWithOverDataSize)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReceiveFailWithOverDataSize", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("SyncReceiveFailWithOverDataSize", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // param init
    // dataLength is over NET_SGE_MAX_SIZE
    mockReq.dataLength = NET_SGE_MAX_SIZE + NN_NO1;
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx {};
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    ep->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(returnValue(1))
        .then(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));

    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, 1);

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_INVALID_PARAM));

    delete (ep);
}

TEST_F(TestNetShmEndpoint, SyncReceiveFailWithErrorSeqNo)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReceiveFailWithErrorSeqNo", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("SyncReceiveFailWithErrorSeqNo", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // param init
    mockReq.seqNo = 1;
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    ep->mDelayHandleReceiveEvent.dataSize = NN_NO1024;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));
    MOCKER(NetFunc::ValidateHeaderCrc32, bool(UBSHcomNetTransHeader *))
        .stubs()
        .will(returnValue(false));

    ep->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_SEQ_NO_NOT_MATCHED));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, SyncReceiveFailWithErrDataLen)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReceiveFailWithErrDataLen", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("SyncReceiveFailWithErrDataLen", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);

    mockReq.seqNo = 0;
    mockReq.dataLength = NN_NO2048;
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx {};
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    // data length in header is not equal to dataSize in event
    ep->mDelayHandleReceiveEvent.dataSize = NN_NO1024;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset).stubs().will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    MOCKER(NetFunc::ValidateHeaderCrc32, bool(UBSHcomNetTransHeader *)).stubs().will(returnValue(false));

    ep->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    ep->mExistDelayEvent = true;

    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_INVALID_PARAM));

    delete (ep);
}

TEST_F(TestNetShmEndpoint, SyncReceiveFailWithInvalidHeader)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReceiveFailWithInvalidHeader", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("SyncReceiveFailWithInvalidHeader", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // param init
    mockReq.seqNo = 0;
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx {};
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    ep->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    ep->mDelayHandleReceiveEvent.dataSize = NN_NO1024;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset).stubs().will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree).stubs().will(returnValue(0));
    MOCKER(NetFunc::ValidateHeaderCrc32, bool(UBSHcomNetTransHeader *)).stubs().will(returnValue(false));

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_VALIDATE_HEADER_CRC_INVALID));

    delete (ep);
}

TEST_F(TestNetShmEndpoint, SyncReceiveFailToAllocate)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("SyncReceiveFailToAllocate", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("SyncReceiveFailToAllocate", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // param init
    mockReq.seqNo = 0;
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mockReq.opCode = 0;
    mockReq.headerCrc = NetFunc::CalcHeaderCrc32(mockReq);
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    ep->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    ep->mDelayHandleReceiveEvent.dataSize = NN_NO1024;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER(NetFunc::ValidateHeaderCrc32, bool(UBSHcomNetTransHeader *))
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    ep->mExistDelayEvent = true;
    ep->mIsNeedEncrypt = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_MALLOC_FAILED));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmReceiveFour)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmReceive4", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointShmReceive4", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // param init
    mockReq.seqNo = 0;
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mockReq.opCode = 0;
    mockReq.headerCrc = NetFunc::CalcHeaderCrc32(mockReq);
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    ep->mIsNeedEncrypt = true;
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    ep->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    ep->mDelayHandleReceiveEvent.dataSize = NN_NO1024;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER(NetFunc::ValidateHeaderCrc32, bool(UBSHcomNetTransHeader *))
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&AesGcm128::Decrypt, bool(AesGcm128::*)(NetSecrets &, const void *, uint32_t, void *, uint32_t &))
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_DECRYPT_FAILED));

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, 0);

    delete(ep);
}

TEST_F(TestNetShmEndpoint, NetSyncEndpointShmReceiveFailFive)
{
    int ret;
    // mShmCh create
    ShmChannelPtr ch;
    ShmChannel::CreateAndInit("NetSyncEndpointShmReceive4", 0, NN_NO128, NN_NO4, ch);
    // ShmSyncEndpoint create
    ShmSyncEndpoint *shmEp = new ShmSyncEndpoint("NetSyncEndpointShmReceive4", 0, SHM_EVENT_POLLING);
    // shmEp create
    UBSHcomNetWorkerIndex index;
    ShmMRHandleMap map;
    NetSyncEndpointShm *ep = new NetSyncEndpointShm(ch->Id(), ch.Get(), 0, index, shmEp, map);
    ep->mState.Set(NEP_ESTABLISHED);
    // param init
    mockReq.seqNo = 0;
    mockReq.dataLength = NN_NO1024 - sizeof(UBSHcomNetTransHeader);
    mockReq.opCode = 0;
    mockReq.headerCrc = NetFunc::CalcHeaderCrc32(mockReq);
    int32_t timeout = 0;
    UBSHcomNetResponseContext ctx{};
    ep->mIsNeedEncrypt = false;
    ep->mExistDelayEvent = true;
    ep->mDelayHandleReceiveEvent.peerChannelAddress = reinterpret_cast<uintptr_t>(&mockReq);
    ep->mDelayHandleReceiveEvent.opType = ShmOpContextInfo::SH_RECEIVE;
    ep->mDelayHandleReceiveEvent.dataSize = NN_NO1024;

    MOCKER_CPP(&ShmChannel::GetPeerDataAddressByOffset)
        .stubs()
        .will(invoke(MockGetPeerDataAddressByOffset));
    MOCKER(NetFunc::ValidateHeaderCrc32, bool(UBSHcomNetTransHeader *))
        .stubs()
        .will(returnValue(true));
    MOCKER_CPP(&UBSHcomNetMessage::AllocateIfNeed)
        .stubs()
        .will(returnValue(false))
        .then(returnValue(true));
    MOCKER_CPP(&memcpy_s)
        .stubs()
        .will(returnValue(0))
        .then(returnValue(1));
    MOCKER_CPP(&ShmChannel::DCMarkPeerBuckFree)
        .stubs()
        .will(returnValue(0));

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_MALLOC_FAILED));

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_INVALID_PARAM));

    ep->mExistDelayEvent = true;
    ret = ep->Receive(timeout, ctx);
    EXPECT_EQ(ret, static_cast<int32_t>(NN_INVALID_PARAM));

    delete(ep);
}

TEST_F(TestNetShmEndpoint, ShmWorkerInitializeFail)
{
    UBSHcomNetWorkerIndex indexWorker;
    ShmWorkerOptions options{};
    NetMemPoolFixedPtr opMemPool;
    NetMemPoolFixedPtr opCtxMemPool;
    NetMemPoolFixedPtr sglOpMemPool;
    ShmWorker *worker = new (std::nothrow) ShmWorker("shm", indexWorker, options, opMemPool,
        opCtxMemPool, sglOpMemPool);
 
    MOCKER_CPP(&ShmWorker::Validate).stubs().will(returnValue(1)).then(returnValue(0));
    MOCKER_CPP(&ShmWorker::CreateEventQueue).stubs().will(returnValue(1));
    
    EXPECT_NE(worker->Initialize(), 0);
    EXPECT_NE(worker->Initialize(), 0);
 
    delete worker;
}
}
}