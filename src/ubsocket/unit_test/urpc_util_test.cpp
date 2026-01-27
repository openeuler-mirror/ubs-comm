#include "urpc_util.h"

#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>



class UrpcUtilTest : public testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

int FakeRand(uint8_t *p, uint32_t num)
{
    if (num < 2) {
        return -1;
    }

    p[0] = 0x11;
    p[1] = 0x22;
    p[2] = 0x11;
    p[3] = 0x11;
    return 0;
}

TEST_F(UrpcUtilTest, Rand)
{
    int x = 0;
    int ret = 0;

    ret = ubsocket::urpc_rand_generate((uint8_t *)&x, sizeof(x));
    EXPECT_EQ(ret, 0);
    EXPECT_NE(x, 0);

    MOCKER_CPP(&ubsocket::urpc_rand_generate).stubs().will(invoke(FakeRand));
    ret = ubsocket::urpc_rand_generate((uint8_t *)&x, 1);
    EXPECT_EQ(ret, -1);

    ret = ubsocket::urpc_rand_generate((uint8_t *)&x, 4);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(x, 0x11112211);
}
