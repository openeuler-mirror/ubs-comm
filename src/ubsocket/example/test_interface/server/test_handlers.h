#ifndef TEST_HANDLERS_H
#define TEST_HANDLERS_H

#include <vector>
#include <string>
#include "../common/test_common.h"

class TestHandlers {
public:
    // 读取并验证测试头
    static TestHeader read_test_header(int sockfd);
    
    // 各种测试的处理函数
    static TestResult handle_read_test(int sockfd, const TestHeader& header);
    static TestResult handle_readv_test(int sockfd, const TestHeader& header);
    static TestResult handle_recv_test(int sockfd, const TestHeader& header);
    static TestResult handle_recvfrom_test(int sockfd, const TestHeader& header);
    static TestResult handle_sendmsg_test(int sockfd, const TestHeader& header);
    
    // 文件相关测试
    static TestResult handle_sendfile_test(int sockfd, const TestHeader& header);
};

#endif // TEST_HANDLERS_H
