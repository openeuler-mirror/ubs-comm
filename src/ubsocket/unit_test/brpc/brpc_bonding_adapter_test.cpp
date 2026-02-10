#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>

#include "brpc_context.h"

class BrpcBondingAdapterTest : public testing::Test {
public:
    void SetUp() override
    {
        setenv("RPC_ADPT_USE_ZCOPY", "true", 1);
        setenv("RPC_ADPT_UB_FORCE", "1", 1);
        setenv("RPC_ADPT_TRANS_MODE", "UB", 1);
        RpcAdptSetLogCtx(ubsocket::UTIL_VLOG_LEVEL_INFO);
    }

    void TearDown() override
    {
        unsetenv("RPC_ADPT_USE_ZCOPY");
        unsetenv("RPC_ADPT_UB_FORCE");
        unsetenv("RPC_ADPT_TRANS_MODE");
    }
};

static int MockUmqDevInfoGet1(char* devName, umq_trans_mode_t umq_trans_mode, umq_dev_info_t* umq_dev_info)
{
    umq_eid_t eid = {0};
    const char* eidStr = "BEID\000\000\000\000\000\000\000\000\001\000\000";
    memcpy(eid.raw, eidStr, sizeof(eid.raw));
    umq_eid_info_t eid_info = {0};
    eid_info.eid = eid;
    eid_info.eid_index = 0;

    umq_dev_info->ub.eid_cnt = 1;
    umq_dev_info->ub.eid_list[0] = eid_info;
    return 0;
}

TEST_F(BrpcBondingAdapterTest, TestSetDeviceInfoSucceed)
{
    umq_eid_t eid = {0};
    const char* eidStr = "BEID\000\000\000\000\000\000\000\000\001\000\000";
    memcpy(eid.raw, eidStr, sizeof(eid.raw));
    MOCKER_CPP(umq_init)
            .stubs()
            .will(returnValue(int(0)));
    MOCKER_CPP(umq_dev_info_get)
            .stubs()
            .will(invoke(MockUmqDevInfoGet1));
    Brpc::Context *context = new Brpc::Context();

    EXPECT_EQ(context->GetDevNameStr(), nullptr);
    EXPECT_EQ(context->GetDevIpStr(), nullptr);

    delete context;
    context = nullptr;
    GlobalMockObject::verify();
}

static int MockUmqDevInfoGet2(char* devName, umq_trans_mode_t umq_trans_mode, umq_dev_info_t* umq_dev_info)
{
    return -1;
}

TEST_F(BrpcBondingAdapterTest, TestGetDeviceInfoFailed)
{
    MOCKER_CPP(umq_init)
            .stubs()
            .will(returnValue(int(0)));

    MOCKER_CPP(umq_dev_info_get)
            .stubs()
            .will(invoke(MockUmqDevInfoGet2));
    MOCKER(&Brpc::Context::ResetBrpcAllocator)
            .stubs();
    Brpc::Context *context = new Brpc::Context();

    EXPECT_EQ(context->GetDevNameStr(), nullptr);
    EXPECT_EQ(context->GetDevIpStr(), nullptr);
    EXPECT_EQ(context->m_socket_fd_trans_mode, ConfigSettings::socket_fd_trans_mode::SOCKET_FD_TRANS_MODE_TCP);

    delete context;
    context = nullptr;
    GlobalMockObject::verify();
}

static int MockUmqDevInfoGet3(char* devName, umq_trans_mode_t umq_trans_mode, umq_dev_info_t* umq_dev_info)
{
    umq_eid_t eid = {0};
    const char* eidStr = "BEID\000\000\000\000\000\000\000\000\001\000\000";
    memcpy(eid.raw, eidStr, sizeof(eid.raw));
    umq_eid_info_t eid_info = {0};
    eid_info.eid = eid;
    eid_info.eid_index = 0;

    umq_dev_info->ub.eid_cnt = 0;
    umq_dev_info->ub.eid_list[0] = eid_info;
    return 0;
}

TEST_F(BrpcBondingAdapterTest, TestGetEidFailed)
{
    MOCKER_CPP(umq_init)
            .stubs()
            .will(returnValue(int(0)));

    MOCKER_CPP(umq_dev_info_get)
            .stubs()
            .will(invoke(MockUmqDevInfoGet3));
    MOCKER(&Brpc::Context::ResetBrpcAllocator)
            .stubs();
    Brpc::Context *context = new Brpc::Context();

    EXPECT_EQ(context->GetDevNameStr(), nullptr);
    EXPECT_EQ(context->GetDevIpStr(), nullptr);
    EXPECT_EQ(context->m_socket_fd_trans_mode, ConfigSettings::socket_fd_trans_mode::SOCKET_FD_TRANS_MODE_TCP);

    delete context;
    context = nullptr;
    GlobalMockObject::verify();
}