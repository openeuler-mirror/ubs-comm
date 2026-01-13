#include "test_common.h"
#include <random>
#include <iomanip>
#include <sstream>

std::string testTypeToString(TestType type) {
    switch (type) {
        case TEST_READ:      return "TEST_READ";
        case TEST_READV:     return "TEST_READV";
        case TEST_RECV:      return "TEST_RECV";
        case TEST_RECVFROM:  return "TEST_RECVFROM";
        case TEST_WRITE:     return "TEST_WRITE";
        case TEST_WRITEV:    return "TEST_WRITEV";
        case TEST_SEND:      return "TEST_SEND";
        case TEST_SENDTO:    return "TEST_SENDTO";
        case TEST_SENDFILE:  return "TEST_SENDFILE";
        case TEST_SENDFILE64: return "TEST_SENDFILE64";
        case TEST_SENDMSG:   return "TEST_SENDMSG";
        case TEST_RECVMSG:   return "TEST_RECVMSG";
        case TEST_SINGLE_END:   return "TEST_SINGLE_END";
        case TEST_TOTAL_END:   return "TEST_TOTAL_END";
        default:             return "UNKNOWN_TYPE";
    }
}

// 打印单个 TestHeader 结构体的函数
void printTestHeader(const TestHeader& header) {
    std::cout << "TestHeader Content:" << std::endl;
    std::cout << "-------------------" << std::endl;
    
    // 打印魔数（十六进制格式）
    std::cout << "magic: 0x" << std::hex << std::uppercase << std::setw(8) 
              << std::setfill('0') << header.magic << std::dec << std::endl;
    
    // 打印测试类型
    std::cout << "test_type: " << testTypeToString(header.test_type) 
              << " (" << static_cast<int>(header.test_type) << ")" << std::endl;
    
    // 打印数据大小
    std::cout << "data_size: " << header.data_size << " bytes" << std::endl;
    
    // 打印迭代次数
    std::cout << "iteration: " << header.iteration << std::endl;
    
    // 打印时间戳
    std::cout << "timestamp: " << header.timestamp << std::endl;

    // 打印控制标志位
    std::cout << "flags " << header.flags << std::endl;
    
    // 打印校验和（十六进制格式）
    std::cout << "checksum: 0x" << std::hex << std::uppercase << std::setw(8) 
              << std::setfill('0') << header.checksum << std::dec << std::endl;
    
    std::cout << std::endl;
}

bool verify_test_header(const TestHeader& header) {
    if (header.magic != 0xDEADBEEF) {
        return false;
    }
    
    // 保存原始校验和
    uint32_t original_checksum = header.checksum;
    
    // 临时副本用于计算
    TestHeader temp = header;
    temp.checksum = 0;  // 计算时置0
    
    uint32_t calculated = calculate_checksum(temp);
    
    return calculated == original_checksum;
}

uint32_t calculate_checksum(const TestHeader& header) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&header);
    size_t size = sizeof(header);
    uint32_t sum = 0;
    
    // 排除checksum字段本身
    for (size_t i = 0; i < size - sizeof(uint32_t); ++i) {
        sum = (sum + ptr[i]) & 0xFFFFFFFF;
    }
    
    return sum;
}

std::vector<char> generate_test_data(size_t size) {
    std::vector<char> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    // 生成可打印字符模式
    for (size_t i = 0; i < size; ++i) {
        data[i] = static_cast<char>('A' + (i % 26));
    }
    
    return data;
}

bool compare_data(const char* data1, const char* data2, size_t size) {
    return memcmp(data1, data2, size) == 0;
}

void print_test_result(const std::string& test_name, const TestResult& result) {
    std::cout << std::left << std::setw(25) << test_name << ": ";
    
    if (result.success) {
        std::cout << "\033[32mPASS\033[0m";
        std::cout << " | Bytes: " << std::setw(10) << result.bytes_transferred;
        std::cout << " | Time: " << std::setw(6) << result.duration.count() << "ms";
        if (result.duration.count() > 0) {
            double throughput = (result.bytes_transferred * 1000.0) / (result.duration.count() * 1024.0 * 1024.0);
            std::cout << " | Throughput: " << std::fixed << std::setprecision(2) << throughput << " MB/s";
        }
    } else {
        std::cout << "\033[31mFAIL\033[0m";
        std::cout << " | Error: " << result.error_message;
    }
    
    std::cout << std::endl;
}
