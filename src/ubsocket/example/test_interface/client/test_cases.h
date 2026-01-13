/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
*/
#ifndef TEST_CASES_H
#define TEST_CASES_H

#include "../common/test_common.h"
#include <string>
#include <vector>

class TestCases {
public:
    // 连接到测试服务器
    static int connect_to_server(const std::string& server_ip, uint16_t port);
    
    // 各种测试用例
    static TestResult test_read_write(int sockfd, size_t data_size);
    static TestResult test_readv_writev(int sockfd, size_t data_size);
    static TestResult test_recv_send(int sockfd, size_t data_size);
    static TestResult test_recvfrom_sendto(int sockfd, size_t data_size);
    static TestResult test_write(int sockfd, size_t data_size);
    static TestResult test_writev(int sockfd, size_t data_size);
    static TestResult test_send(int sockfd, size_t data_size);
    static TestResult test_sendto(int sockfd, size_t data_size);
    static TestResult test_sendmsg_recvmsg(int sockfd, size_t data_size);
    static TestResult test_sendfile(int sockfd, size_t data_size);
    static TestResult test_sendfile64(int sockfd, size_t data_size);
    
    // 批量测试
    static void run_all_tests(const std::string& server_ip);
    
private:
    static TestHeader create_test_header(TestType type, size_t data_size);
    static bool send_test_header(int sockfd, const TestHeader& header);
};

#endif // TEST_CASES_H
