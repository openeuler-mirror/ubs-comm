/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
*/

#include "test_cases.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <iomanip>
#include <sstream>

int TestCases::connect_to_server(const std::string& server_ip, uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << server_ip << std::endl;
        close(sockfd);
        return -1;
    }
    
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect failed");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}

TestHeader TestCases::create_test_header(TestType type, size_t data_size) {
    TestHeader header;
    header.magic = 0xDEADBEEF;
    header.test_type = type;
    header.data_size = data_size;
    header.iteration = 0;
    if (type == TEST_SINGLE_END) {
        header.flags = TEST_FLAG_END_OF_TEST;
    } else if (type == TEST_TOTAL_END) {
        header.flags = TEST_FLAG_END_OF_ALL_TEST;
    } else {
        header.flags = TEST_FLAG_RUNNING_TEST;
    }
    header.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // 先计算校验和（不包括checksum字段）
    header.checksum = 0;  // 先置0
    header.checksum = calculate_checksum(header);
    
    return header;
}

bool TestCases::send_test_header(int sockfd, const TestHeader& header) {
    ssize_t n = send(sockfd, &header, sizeof(header), 0);
    return n == sizeof(header);
}

TestResult TestCases::test_write(int sockfd, size_t data_size) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    // 创建测试头
    TestHeader header = create_test_header(TEST_WRITE, data_size);
    if (!send_test_header(sockfd, header)) {
        result.success = false;
        result.error_message = "Failed to send test header";
        return result;
    }

    // 等待服务器响应头
    TestHeader head_response;
    ssize_t head_response_ret = recv(sockfd, &head_response, sizeof(head_response), 0);

    if (head_response_ret != sizeof(head_response) || !verify_test_header(head_response)) {
        result.success = false;
        result.error_message = "Invalid response from server";
        return result;
    }
    
    // 生成测试数据
    std::vector<char> test_data = generate_test_data(data_size);
    
    // 使用write发送数据
    ssize_t total_written = 0;
    while (total_written < data_size) {
        ssize_t n = write(sockfd, test_data.data() + total_written, data_size - total_written);
        if (n <= 0) {
            result.success = false;
            result.error_message = "write failed: " + std::string(strerror(errno));
            return result;
        }
        total_written += n;
    }
    
    // 等待服务器响应
    TestHeader response;
    ssize_t n = recv(sockfd, &response, sizeof(response), 0);
    
    auto end = std::chrono::high_resolution_clock::now();
    
    if (n != sizeof(response) || !verify_test_header(response)) {
        result.success = false;
        result.error_message = "Invalid response from server";
    } else {
        result.success = true;
        result.bytes_transferred = total_written;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestCases::test_writev(int sockfd, size_t data_size) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    TestHeader header = create_test_header(TEST_WRITE, data_size);
    if (!send_test_header(sockfd, header)) {
        result.success = false;
        result.error_message = "Failed to send test header";
        return result;
    }
    
    const size_t num_buffers = 4;
    const size_t buffer_size = data_size / num_buffers;
    
    std::vector<char> data1 = generate_test_data(buffer_size);
    std::vector<char> data2 = generate_test_data(buffer_size);
    std::vector<char> data3 = generate_test_data(buffer_size);
    std::vector<char> data4 = generate_test_data(buffer_size);
    
    struct iovec iov[num_buffers];
    iov[0].iov_base = data1.data();
    iov[0].iov_len = data1.size();
    iov[1].iov_base = data2.data();
    iov[1].iov_len = data2.size();
    iov[2].iov_base = data3.data();
    iov[2].iov_len = data3.size();
    iov[3].iov_base = data4.data();
    iov[3].iov_len = data4.size();
    
    ssize_t n = writev(sockfd, iov, num_buffers);
    
    auto end = std::chrono::high_resolution_clock::now();
    
    if (n < 0) {
        result.success = false;
        result.error_message = "writev failed: " + std::string(strerror(errno));
    } else {
        result.success = true;
        result.bytes_transferred = n;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestCases::test_send(int sockfd, size_t data_size) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    TestHeader header = create_test_header(TEST_SEND, data_size);

    if (!send_test_header(sockfd, header)) {
        result.success = false;
        result.error_message = "Failed to send test header";
        return result;
    }

    // 等待服务器响应头
    TestHeader head_response;
    ssize_t head_response_ret = recv(sockfd, &head_response, sizeof(head_response), 0);

    if (head_response_ret != sizeof(head_response) || !verify_test_header(head_response)) {
        result.success = false;
        result.error_message = "Invalid response from server";
        return result;
    }
    
    std::vector<char> test_data = generate_test_data(data_size);
    
    ssize_t total_sent = 0;
    while (total_sent < data_size) {
        ssize_t n = send(sockfd, test_data.data() + total_sent, data_size - total_sent, 0);
        if (n <= 0) {
            result.success = false;
            result.error_message = "send failed: " + std::string(strerror(errno));
            return result;
        }
        total_sent += n;
    }
    
    // 等待服务器响应
    TestHeader response;
    ssize_t n = recv(sockfd, &response, sizeof(response), 0);
    
    auto end = std::chrono::high_resolution_clock::now();

    if (n != sizeof(response) || !verify_test_header(response)) {
        result.success = false;
        result.error_message = "Invalid response from server";
    } else {
        result.success = true;
        result.bytes_transferred = total_sent;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestCases::test_sendto(int sockfd, size_t data_size) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    TestHeader header = create_test_header(TEST_SENDTO, data_size);
    if (!send_test_header(sockfd, header)) {
        result.success = false;
        result.error_message = "Failed to send test header";
        return result;
    }

    // 等待服务器响应头
    TestHeader head_response;
    ssize_t head_response_ret = recv(sockfd, &head_response, sizeof(head_response), 0);

    if (head_response_ret != sizeof(head_response) || !verify_test_header(head_response)) {
        result.success = false;
        result.error_message = "Invalid response from server";
        return result;
    }
    
    std::vector<char> test_data = generate_test_data(data_size);
    
    // 获取对端地址
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);
    getpeername(sockfd, (struct sockaddr*)&server_addr, &addr_len);
    
    ssize_t total_sendto = sendto(sockfd, test_data.data(), test_data.size(), 0,
                      (struct sockaddr*)&server_addr, addr_len);
    
    // 等待服务器响应
    TestHeader response;
    ssize_t n = recv(sockfd, &response, sizeof(response), 0);

    auto end = std::chrono::high_resolution_clock::now();
    
    if (n < 0) {
        result.success = false;
        result.error_message = "sendto failed: " + std::string(strerror(errno));
    } else {
        result.success = true;
        result.bytes_transferred = total_sendto;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestCases::test_sendmsg_recvmsg(int sockfd, size_t data_size) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    TestHeader header = create_test_header(TEST_SENDMSG, data_size);
    if (!send_test_header(sockfd, header)) {
        result.success = false;
        result.error_message = "Failed to send test header";
        return result;
    }
    
    std::vector<char> test_data = generate_test_data(data_size);
    struct iovec iov = { test_data.data(), test_data.size() };
    
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    ssize_t n = sendmsg(sockfd, &msg, 0);
    
    auto end = std::chrono::high_resolution_clock::now();
    
    if (n < 0) {
        result.success = false;
        result.error_message = "sendmsg failed: " + std::string(strerror(errno));
    } else {
        result.success = true;
        result.bytes_transferred = n;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestCases::test_sendfile(int sockfd, size_t data_size) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    TestHeader header = create_test_header(TEST_SENDFILE, data_size);
    if (!send_test_header(sockfd, header)) {
        result.success = false;
        result.error_message = "Failed to send test header";
        return result;
    }
    
    // 创建临时文件
    char filename[] = "/tmp/test_sendfile_XXXXXX";
    int file_fd = mkstemp(filename);
    if (file_fd < 0) {
        result.success = false;
        result.error_message = "Failed to create temp file";
        return result;
    }
    
    // 写入测试数据
    std::vector<char> test_data = generate_test_data(data_size);
    write(file_fd, test_data.data(), test_data.size());
    lseek(file_fd, 0, SEEK_SET);
    
    // 接收文件
    std::vector<char> buffer(data_size);
    ssize_t total_received = 0;
    
    while (total_received < data_size) {
        ssize_t n = read(sockfd, buffer.data() + total_received, data_size - total_received);
        if (n <= 0) {
            result.success = false;
            result.error_message = "Failed to receive file data";
            break;
        }
        total_received += n;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    close(file_fd);
    unlink(filename);
    
    if (compare_data(buffer.data(), test_data.data(), data_size)) {
        result.success = true;
        result.bytes_transferred = total_received;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    } else {
        result.success = false;
        result.error_message = "Data mismatch";
    }
    
    return result;
}

// 对于 sendfile64，使用相同的实现
TestResult TestCases::test_sendfile64(int sockfd, size_t data_size) {
    // sendfile64 与 sendfile 功能相同
    return test_sendfile(sockfd, data_size);
}

// 其他未使用的函数实现（空实现）
TestResult TestCases::test_read_write(int sockfd, size_t data_size) {
    TestResult result;
    result.success = false;
    result.error_message = "test_read_write not implemented";
    return result;
}

TestResult TestCases::test_readv_writev(int sockfd, size_t data_size) {
    TestResult result;
    result.success = false;
    result.error_message = "test_readv_writev not implemented";
    return result;
}

TestResult TestCases::test_recv_send(int sockfd, size_t data_size) {
    TestResult result;
    result.success = false;
    result.error_message = "test_recv_send not implemented";
    return result;
}

TestResult TestCases::test_recvfrom_sendto(int sockfd, size_t data_size) {
    TestResult result;
    result.success = false;
    result.error_message = "test_recvfrom_sendto not implemented";
    return result;
}

void TestCases::run_all_tests(const std::string& server_ip) {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "Running All TCP Interface Tests" << std::endl;
    std::cout << "Server IP: " << server_ip << std::endl;
    std::cout << "Port: " << BASE_PORT << std::endl;
    std::cout << "==========================================" << std::endl;
    
    // 测试不同数据大小
    std::vector<std::pair<std::string, size_t>> test_sizes = {
        {"Small", SMALL_DATA_SIZE},
        {"Large", LARGE_DATA_SIZE}
    };
    
    for (const auto& size_test : test_sizes) {
        std::cout << "\n[" << size_test.first << " Data Test - " 
                  << size_test.second << " bytes]" << std::endl;
        
#if ENABLE_TEST_READ_WRITE
        {
            int sockfd = connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                TestResult result = test_write(sockfd, size_test.second);
                print_test_result("write", result);
                TestHeader header = create_test_header(TEST_SINGLE_END, DEFAULT_DATA_SIZE);
                send_test_header(sockfd, header);
                close(sockfd);
            }
        }
#endif
        
#if ENABLE_TEST_READV_WRITEV
        {
            int sockfd = connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                TestResult result = test_writev(sockfd, size_test.second);
                print_test_result("writev", result);
                close(sockfd);
            }
        }
#endif
        
#if ENABLE_TEST_RECV_SEND
        {
            int sockfd = connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                TestResult result = test_send(sockfd, size_test.second);
                print_test_result("send", result);
                TestHeader header = create_test_header(TEST_SINGLE_END, DEFAULT_DATA_SIZE);
                send_test_header(sockfd, header);
                close(sockfd);
            }
        }
#endif
        
#if ENABLE_TEST_RECVFROM_SENDTO
        {
            int sockfd = connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                TestResult result = test_sendto(sockfd, size_test.second);
                print_test_result("sendto", result);
                TestHeader header = create_test_header(TEST_SINGLE_END, DEFAULT_DATA_SIZE);
                send_test_header(sockfd, header);
                close(sockfd);
            }
        }
#endif
        
#if ENABLE_TEST_SENDMSG_RECVMSG
        {
            int sockfd = connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                TestResult result = test_sendmsg_recvmsg(sockfd, size_test.second);
                print_test_result("sendmsg", result);
                close(sockfd);
            }
        }
#endif
        
#if ENABLE_TEST_SENDFILE
        {
            int sockfd = connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                TestResult result = test_sendfile(sockfd, size_test.second);
                print_test_result("sendfile", result);
                close(sockfd);
            }
        }
#endif
        
#if ENABLE_TEST_SENDFILE64
        {
            int sockfd = connect_to_server(server_ip, BASE_PORT);
            if (sockfd >= 0) {
                TestResult result = test_sendfile64(sockfd, size_test.second);
                print_test_result("sendfile64", result);
                close(sockfd);
            }
        }
#endif
    }

    int sockfd = connect_to_server(server_ip, BASE_PORT);
    if (sockfd >= 0) {
        TestHeader header = create_test_header(TEST_TOTAL_END, DEFAULT_DATA_SIZE);
        send_test_header(sockfd, header);
        close(sockfd);
    }
}
