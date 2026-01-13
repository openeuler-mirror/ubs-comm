#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <cstring>
#include <iostream>

// ==================== 测试开关宏定义 ====================
#define ENABLE_TEST_READ_WRITE      1
#define ENABLE_TEST_READV_WRITEV    0
#define ENABLE_TEST_RECV_SEND       1
#define ENABLE_TEST_RECVFROM_SENDTO 1
#define ENABLE_TEST_SENDFILE        0
#define ENABLE_TEST_SENDFILE64      0
#define ENABLE_TEST_SENDMSG_RECVMSG 0

// 测试模式宏定义
#define ENABLE_TEST_MODE_SMALL      1
#define ENABLE_TEST_MODE_LARGE      1
#define ENABLE_TEST_MODE_STRESS     1

// 定义标志位掩码
#define TEST_FLAG_RUNNING_TEST          0x00000001  // 测试进行
#define TEST_FLAG_END_OF_TEST           0x00000002  // 单测试结束
#define TEST_FLAG_END_OF_ALL_TEST       0x00000003  // 所有测试结束

// ==================== 常量定义 ====================
constexpr uint16_t BASE_PORT = 9000;  // 所有测试使用同一个端口
constexpr size_t DEFAULT_DATA_SIZE = 512;      // 512Bytes
constexpr size_t SMALL_DATA_SIZE = 1024;      // 1KB
constexpr size_t MIDDLE_DATA_SIZE = 2048;      // 2KB
constexpr size_t LARGE_DATA_SIZE = 4096;  // 4KB
constexpr size_t STRESS_ITERATIONS = 1000;    // 压力测试迭代次数

// ==================== 数据结构 ====================
enum TestType {
    TEST_READ = 1,
    TEST_READV,
    TEST_RECV,
    TEST_RECVFROM,
    TEST_WRITE,
    TEST_WRITEV,
    TEST_SEND,
    TEST_SENDTO,
    TEST_SENDFILE,
    TEST_SENDFILE64,
    TEST_SENDMSG,
    TEST_RECVMSG,
    TEST_RECVED,
    TEST_SINGLE_END,
    TEST_TOTAL_END
};

struct TestHeader {
    uint32_t magic;           // 魔数 0xDEADBEEF
    TestType test_type;       // 测试类型
    uint32_t data_size;       // 数据大小
    uint32_t iteration;       // 迭代次数
    uint64_t timestamp;       // 时间戳
    uint32_t flags;           // 控制标志位
    uint32_t checksum;        // 头部校验和
};

struct TestResult {
    bool success;
    size_t bytes_transferred;
    std::chrono::milliseconds duration;
    std::string error_message;
};

// ==================== 函数声明 ====================
std::string testTypeToString(TestType type);
void printTestHeader(const TestHeader& header);
bool verify_test_header(const TestHeader& header);
uint32_t calculate_checksum(const TestHeader& header);
std::vector<char> generate_test_data(size_t size);
bool compare_data(const char* data1, const char* data2, size_t size);
void print_test_result(const std::string& test_name, const TestResult& result);

// 端口计算 - 现在所有测试使用同一个端口
inline uint16_t get_port_for_test(TestType type) {
    (void)type;  // 避免未使用参数警告
    return BASE_PORT;
}

#endif // TEST_COMMON_H
