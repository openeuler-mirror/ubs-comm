/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
*/
#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include "test_cases.h"
#include "../common/test_common.h"

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -s, --server IP        Server IP address (default: 141.61.84.79)" << std::endl;
    std::cout << "  -t, --test TESTNAME    Run specific test" << std::endl;
    std::cout << "  -a, --all              Run all tests" << std::endl;
    std::cout << "  -l, --list             List available tests" << std::endl;
    std::cout << "  -h, --help             Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Available tests:" << std::endl;
    std::cout << "  write                  Test write interface" << std::endl;
    std::cout << "  writev                 Test writev interface" << std::endl;
    std::cout << "  send                   Test send interface" << std::endl;
    std::cout << "  sendto                 Test sendto interface" << std::endl;
    std::cout << "  sendmsg                Test sendmsg interface" << std::endl;
    std::cout << "  sendfile               Test sendfile interface" << std::endl;
    std::cout << "  sendfile64             Test sendfile64 interface" << std::endl;
    std::cout << "  all                    Run all enabled tests" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string server_ip;
    std::string test_name = "all";
    setenv("RPC_ADPT_USE_ZCOPY", "TRUE", 1);

    // 解析命令行参数
    bool server_provided = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-s" || arg == "--server") {
            if (i + 1 < argc) {
                server_ip = argv[++i];
                server_provided = true;
            } else {
                std::cerr << "Error: --server requires an argument" << std::endl;
                return 1;
            }
        } else if (arg == "-t" || arg == "--test") {
            if (i + 1 < argc) {
                test_name = argv[++i];
            } else {
                std::cerr << "Error: --test requires an argument" << std::endl;
                return 1;
            }
        } else if (arg == "-a" || arg == "--all") {
            test_name = "all";
        } else if (arg == "-l" || arg == "--list") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // 检查是否提供了 server IP
    if (!server_provided) {
        std::cerr << "Error: --server/-s is required." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "==========================================" << std::endl;
    std::cout << "TCP All Interface Test Client" << std::endl;
    std::cout << "Server: " << server_ip << ":" << BASE_PORT << std::endl;
    std::cout << "Test: " << test_name << std::endl;
    std::cout << "Enabled tests:" << std::endl;
#if ENABLE_TEST_READ_WRITE
    std::cout << "  ✓ write" << std::endl;
#endif
#if ENABLE_TEST_READV_WRITEV
    std::cout << "  ✓ writev" << std::endl;
#endif
#if ENABLE_TEST_RECV_SEND
    std::cout << "  ✓ send" << std::endl;
#endif
#if ENABLE_TEST_RECVFROM_SENDTO
    std::cout << "  ✓ sendto" << std::endl;
#endif
#if ENABLE_TEST_SENDFILE
    std::cout << "  ✓ sendfile" << std::endl;
#endif
#if ENABLE_TEST_SENDFILE64
    std::cout << "  ✓ sendfile64" << std::endl;
#endif
#if ENABLE_TEST_SENDMSG_RECVMSG
    std::cout << "  ✓ sendmsg" << std::endl;
#endif
    std::cout << "==========================================" << std::endl;
    
    if (test_name == "all") {
        TestCases::run_all_tests(server_ip);
    } else {
        // 运行单个测试
        std::cout << "\nRunning single test: " << test_name << std::endl;
        
        int sockfd = -1;
        TestResult result;
        
        if (test_name == "write") {
#if ENABLE_TEST_READ_WRITE
            sockfd = TestCases::connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                result = TestCases::test_write(sockfd, SMALL_DATA_SIZE);
                print_test_result(test_name, result);
                close(sockfd);
            }
#endif
        } else if (test_name == "writev") {
#if ENABLE_TEST_READV_WRITEV
            sockfd = TestCases::connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                result = TestCases::test_writev(sockfd, SMALL_DATA_SIZE);
                print_test_result(test_name, result);
                close(sockfd);
            }
#endif
        } else if (test_name == "send") {
#if ENABLE_TEST_RECV_SEND
            sockfd = TestCases::connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                result = TestCases::test_send(sockfd, SMALL_DATA_SIZE);
                print_test_result(test_name, result);
                close(sockfd);
            }
#endif
        } else if (test_name == "sendto") {
#if ENABLE_TEST_RECVFROM_SENDTO
            sockfd = TestCases::connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                result = TestCases::test_sendto(sockfd, SMALL_DATA_SIZE);
                print_test_result(test_name, result);
                close(sockfd);
            }
#endif
        } else if (test_name == "sendmsg") {
#if ENABLE_TEST_SENDMSG_RECVMSG
            sockfd = TestCases::connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                result = TestCases::test_sendmsg_recvmsg(sockfd, SMALL_DATA_SIZE);
                print_test_result(test_name, result);
                close(sockfd);
            }
#endif
        } else if (test_name == "sendfile") {
#if ENABLE_TEST_SENDFILE
            sockfd = TestCases::connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                result = TestCases::test_sendfile(sockfd, SMALL_DATA_SIZE);
                print_test_result(test_name, result);
                close(sockfd);
            }
#endif
        } else if (test_name == "sendfile64") {
#if ENABLE_TEST_SENDFILE64
            sockfd = TestCases::connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                result = TestCases::test_sendfile64(sockfd, SMALL_DATA_SIZE);
                print_test_result(test_name, result);
                close(sockfd);
            }
#endif
        } else {
            std::cerr << "Unknown test: " << test_name << std::endl;
            return 1;
        }
        
        if (sockfd >= 0) {
            close(sockfd);
        } else {
            std::cerr << "Failed to connect to server" << std::endl;
        }
    }
    
    std::cout << "\n==========================================" << std::endl;
    std::cout << "All tests completed!" << std::endl;
    std::cout << "==========================================" << std::endl;
    
    unsetenv("RPC_ADPT_USE_ZCOPY");
    return 0;
}
