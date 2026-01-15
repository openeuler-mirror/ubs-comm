/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
*/
#include <iostream>
#include <vector>
#include <atomic>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <random>
#include <iomanip>
#include <sstream>
#include "test_handlers.h"
#include "../common/test_common.h"

std::atomic<bool> running(true);

void signal_handler(int sig) {
    (void)sig;  // 避免未使用参数警告
    running = false;
    std::cout << "\nShutting down server..." << std::endl;
    return;
}

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " [-s IP_ADDRESS]\n";
    std::cerr << "  -s IP_ADDRESS : Server bind IP (default: 0.0.0.0)\n";
    std::cerr << "Example: " << prog_name << " -s 192.168.1.10\n";
}

int main(int argc, char* argv[]) {
    setenv("RPC_ADPT_USE_ZCOPY", "TRUE", 1);
    (void)argc;  // 避免未使用参数警告
    (void)argv;  // 避免未使用参数警告

    // 默认监听所有接口（0.0.0.0）
    const char* server_ip = "0.0.0.0";

    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "s")) != -1) {
        switch (opt) {
            case 's':
                server_ip = optarg;
                break;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "==========================================" << std::endl;
    std::cout << "TCP All Interface Test Server" << std::endl;
    std::cout << "Server IP: " << server_ip << std::endl;
    std::cout << "Listening Port: " << BASE_PORT << std::endl;
    std::cout << "Mode: Single thread, single port" << std::endl;
    std::cout << "Enabled tests:" << std::endl;
#if ENABLE_TEST_READ_WRITE
    std::cout << "  - read/write" << std::endl;
#endif
#if ENABLE_TEST_READV_WRITEV
    std::cout << "  - readv/writev" << std::endl;
#endif
#if ENABLE_TEST_RECV_SEND
    std::cout << "  - recv/send" << std::endl;
#endif
#if ENABLE_TEST_RECVFROM_SENDTO
    std::cout << "  - recvfrom/sendto" << std::endl;
#endif
#if ENABLE_TEST_SENDFILE
    std::cout << "  - sendfile" << std::endl;
#endif
#if ENABLE_TEST_SENDFILE64
    std::cout << "  - sendfile64" << std::endl;
#endif
#if ENABLE_TEST_SENDMSG_RECVMSG
    std::cout << "  - sendmsg/recvmsg" << std::endl;
#endif
    std::cout << "==========================================" << std::endl;
    
    // 创建服务器socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return -1;
    }
    
    // 设置SO_REUSEADDR
    opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        close(server_fd);
        return -1;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, server_ip, &addr.sin_addr) <= 0) {
        std::cerr << "Invalid IP address: " << server_ip << std::endl;
        close(server_fd);
        return -1;
    }
    addr.sin_port = htons(BASE_PORT);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        return -1;
    }
    
    std::cout << "[SERVER] Server started successfully" << std::endl;
    std::cout << "[SERVER] Listening on port " << BASE_PORT << std::endl;
    std::cout << "[SERVER] Waiting for client connections..." << std::endl;
    
    // 连接计数器
    int connection_count = 0;
    
    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // 设置非阻塞accept
        int flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
        
        int client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);

        // 恢复阻塞模式
        fcntl(server_fd, F_SETFL, flags);

        if (client_sock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有新连接，等待100ms
                usleep(100000);
                continue;
            }
            
            if (running) {
                perror("[SERVER] accept failed");
            }
            usleep(100000);
            continue;
        }
        
        // 新客户端连接成功
        connection_count++;
        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        uint16_t client_port = ntohs(client_addr.sin_port);
        
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << "[SERVER] NEW CLIENT CONNECTED!" << std::endl;
        std::cout << "[CLIENT " << connection_count << "] IP: " << client_ip << std::endl;
        std::cout << "[CLIENT " << connection_count << "] Port: " << client_port << std::endl;
        std::cout << "[CLIENT " << connection_count << "] Socket FD: " << client_sock << std::endl;
        std::cout << std::string(60, '=') << "\n" << std::endl;
        
        // 处理客户端请求
        bool client_running = true;
        int request_count = 0;
        // int flag = 0;
        
        while (client_running && running) {
            // 读取测试头
            TestHeader header = TestHandlers::read_test_header(client_sock);
            if (!verify_test_header(header)) {

                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 没有数据，等待1ms后重试
                    usleep(1000);
                    continue;
                }
                // 连接错误或断开
                std::cout << "\n[CLIENT " << connection_count << "] ";
                if (errno == 0) {
                    std::cout << "Connection closed by client" << std::endl;
                } else {
                    std::cout << "Error reading header: " << strerror(errno) << std::endl;
                }
                std::cout << "[CLIENT " << connection_count << "] Total requests processed: " 
                          << request_count << std::endl;
                std::cout << std::string(60, '-') << std::endl;
                break;
            }

            // 成功接收到请求头
            request_count++;
            
            std::cout << "[CLIENT " << connection_count << "] REQUEST #" << request_count 
                      << " RECEIVED" << std::endl;
            std::cout << "[CLIENT " << connection_count << "] Request Details:" << std::endl;
            std::cout << "  Test Type: " << testTypeToString(header.test_type) 
                      << " (" << static_cast<int>(header.test_type) << ")" << std::endl;
            std::cout << "  Data Size: " << header.data_size << " bytes" << std::endl;
            std::cout << "  Iterations: " << header.iteration << std::endl;
            std::cout << "  Timestamp: " << header.timestamp << std::endl;
            std::cout << "  flags: " << header.flags << std::endl;
            std::cout << "  Checksum: 0x" << std::hex << header.checksum << std::dec << std::endl;

            // 答复客户端
            TestHeader response = header;
            response.iteration++;
            response.checksum = 0;
            response.checksum = calculate_checksum(response);
            send(client_sock, &response, sizeof(response), 0);
            
            // 根据测试类型调用相应的处理函数
            TestResult result;
            bool send_response = true;
            
            std::cout << "[CLIENT " << connection_count << "] Processing request..." << std::endl;
            
            switch (header.test_type) {
                case TEST_READ:
#if ENABLE_TEST_READ_WRITE
                    result = TestHandlers::handle_read_test(client_sock, header);
                    send_response = true;
#endif
                    break;
                    
                case TEST_READV:
#if ENABLE_TEST_READV_WRITEV
                    result = TestHandlers::handle_readv_test(client_sock, header);
                    send_response = true;
#endif
                    break;
                    
                case TEST_RECV:
#if ENABLE_TEST_RECV_SEND
                    result = TestHandlers::handle_recv_test(client_sock, header);
                    send_response = true;
#endif
                    break;
                    
                case TEST_SENDTO:
#if ENABLE_TEST_RECVFROM_SENDTO
                    result = TestHandlers::handle_recvfrom_test(client_sock, header);
                    send_response = true;
#endif              
                    break;
                    
                case TEST_WRITE:
                    // 由客户端测试，服务器只需要接收
                    // 服务器读取数据并发送响应
                    {
                        std::vector<char> buffer(header.data_size);
                        ssize_t total_read = 0;
                        while (total_read < header.data_size) {
                            ssize_t n = read(client_sock, buffer.data() + total_read, header.data_size - total_read);
                            if (n <= 0) {
                                client_running = false;
                                break;
                            }
                            total_read += n;
                        }
                        send_response = true;
                    }
                    // continue;  // 跳过打印
                    break;
                    
                case TEST_SEND:
                    // 由客户端测试，服务器只需要接收
                    // 服务器读取数据并发送响应
                    {
                        std::vector<char> buffer(header.data_size);
                        ssize_t total_read = 0;
                        while (total_read < header.data_size) {
                            ssize_t n = recv(client_sock, buffer.data() + total_read, header.data_size - total_read, 0);
                            if (n <= 0) {
                                client_running = false;
                                break;
                            }
                            total_read += n;
                        }
                        send_response = true;
                    }
                    // continue;  // 跳过打印
                    break;
                    
                case TEST_SENDMSG:
#if ENABLE_TEST_SENDMSG_RECVMSG
                    result = TestHandlers::handle_sendmsg_test(client_sock, header);
                    send_response = true;
#endif
                    break;
                    
                case TEST_SENDFILE:
#if ENABLE_TEST_SENDFILE
                    result = TestHandlers::handle_sendfile_test(client_sock, header);
                    send_response = false;  // sendfile不需要额外响应
#endif
                    break;
                    
                case TEST_SENDFILE64:
#if ENABLE_TEST_SENDFILE64
                    // 与sendfile相同处理
                    result = TestHandlers::handle_sendfile_test(client_sock, header);
                    send_response = false;
#endif
                    break;
                case TEST_SINGLE_END:
                    client_running = false;
                    break;
                case TEST_TOTAL_END:
                    client_running = false;
                    running = false;
                    break;
                    
                default:
                    result.success = false;
                    result.error_message = "Unknown test type";
                    break;
            }
            // 如果需要，发送响应
            if (send_response) {
                TestHeader response = header;
                response.iteration++;
                response.checksum = 0;
                response.checksum = calculate_checksum(response);
                send(client_sock, &response, sizeof(response), 0);
            }

            // 打印测试结果
            std::string test_name;
            switch (header.test_type) {
                case TEST_READ: test_name = "read"; break;
                case TEST_READV: test_name = "readv"; break;
                case TEST_RECV: test_name = "recv"; break;
                case TEST_RECVFROM: test_name = "recvfrom"; break;
                case TEST_SENDMSG: test_name = "sendmsg"; break;
                case TEST_SENDFILE: test_name = "sendfile"; break;
                case TEST_SENDFILE64: test_name = "sendfile64"; break;
                default: test_name = "unknown"; break;
            }

            if (!test_name.empty() && test_name != "unknown") {
                std::cout << "  ";
                print_test_result(test_name, result);
            }
            
            // 检查是否需要断开连接
            if (header.iteration > 10) {  // 简单的保护机制
                client_running = false;
            }
            if (client_running == false)
            close(client_sock);
        }
        std::cout << "Connection closed from " << client_ip << ":" << client_port << std::endl;
    }
    
    close(server_fd);
    std::cout << "\nServer shutting down." << std::endl;
    unsetenv("RPC_ADPT_USE_ZCOPY");
    return 0;
}
