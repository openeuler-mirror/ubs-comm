#include <gtest/gtest.h>
#include <chrono>
#include <mockcpp/mockcpp.hpp>
#include <thread>
#include <vector>
#include "ubsocket_prof.h"

class UProfilingTest : public testing::Test {
public:
    virtual void SetUp();
    virtual void TearDown();
};

enum ProfilingTPId : uint32_t {
    TP0 = 0,
    TP1,
    TP2,
    TP3,
    TP4,
};

constexpr const char *DEFAULT_DUMP_PATH = "/tmp/ubsocket/unitest/profiling";
constexpr uint16_t INTERVAL_DEFAULT_MIN = 1;
constexpr uint32_t TP_MAX = 5;

void UProfilingTest::SetUp()
{
    ubsocket_prof_option_t option;
    option.tracepoint_count = TP_MAX;
    option.enable_dump = 1;
    option.dump_file_path = DEFAULT_DUMP_PATH;
    option.dump_interval_min = INTERVAL_DEFAULT_MIN;
    ubsocket_prof_init(&option);
}

void UProfilingTest::TearDown()
{
    ubsocket_prof_uninit();
    GlobalMockObject::verify();
}

void test_thread()
{
    // 所有线程都操作同一个全局 recorder
    PROF_START(TP0);
    PROF_END(TP0, true);
}

TEST_F(UProfilingTest, InitAddSum)
{
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back(test_thread);
    }

    for (auto &t : threads) {
        t.join();
    }
}