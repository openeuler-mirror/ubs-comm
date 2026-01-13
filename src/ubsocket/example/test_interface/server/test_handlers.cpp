#include "test_handlers.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <arpa/inet.h>

TestHeader TestHandlers::read_test_header(int sockfd) {
    TestHeader header;
    ssize_t n = recv(sockfd, &header, sizeof(TestHeader), MSG_WAITALL);

    if (n != sizeof(TestHeader)) {
        header.magic = 0;  // 标记为无效
    }
    
    return header;
}

TestResult TestHandlers::handle_read_test(int sockfd, const TestHeader& header) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<char> buffer(header.data_size);
    ssize_t total_read = 0;
    
    while (total_read < header.data_size) {
        ssize_t n = read(sockfd, buffer.data() + total_read, header.data_size - total_read);
        if (n <= 0) {
            result.success = false;
            result.error_message = "read failed: " + std::string(strerror(errno));
            return result;
        }
        total_read += n;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    result.success = true;
    result.bytes_transferred = total_read;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    return result;
}

TestResult TestHandlers::handle_readv_test(int sockfd, const TestHeader& header) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    const size_t num_buffers = 4;
    const size_t buffer_size = header.data_size / num_buffers;
    
    std::vector<char> data1(buffer_size);
    std::vector<char> data2(buffer_size);
    std::vector<char> data3(buffer_size);
    std::vector<char> data4(buffer_size);
    
    struct iovec iov[num_buffers];
    iov[0].iov_base = data1.data();
    iov[0].iov_len = data1.size();
    iov[1].iov_base = data2.data();
    iov[1].iov_len = data2.size();
    iov[2].iov_base = data3.data();
    iov[2].iov_len = data3.size();
    iov[3].iov_base = data4.data();
    iov[3].iov_len = data4.size();
    
    ssize_t n = readv(sockfd, iov, num_buffers);
    
    auto end = std::chrono::high_resolution_clock::now();
    
    if (n < 0) {
        result.success = false;
        result.error_message = "readv failed: " + std::string(strerror(errno));
    } else {
        result.success = true;
        result.bytes_transferred = n;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestHandlers::handle_recv_test(int sockfd, const TestHeader& header) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<char> buffer(header.data_size);
    ssize_t total_read = 0;
    
    while (total_read < header.data_size) {
        ssize_t n = recv(sockfd, buffer.data() + total_read, header.data_size - total_read, 0);
        if (n <= 0) {
            result.success = false;
            result.error_message = "recv failed: " + std::string(strerror(errno));
            return result;
        }
        total_read += n;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    result.success = true;
    result.bytes_transferred = total_read;
    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    return result;
}

TestResult TestHandlers::handle_recvfrom_test(int sockfd, const TestHeader& header) {
    TestResult result;
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<char> buffer(header.data_size);
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    ssize_t n = recvfrom(sockfd, buffer.data(), buffer.size(), 0,
                        (struct sockaddr*)&client_addr, &addr_len);
    
    auto end = std::chrono::high_resolution_clock::now();
    
    if (n < 0) {
        result.success = false;
        result.error_message = "recvfrom failed: " + std::string(strerror(errno));
    } else {
        result.success = true;
        result.bytes_transferred = n;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestHandlers::handle_sendmsg_test(int sockfd, const TestHeader& header) {
    TestResult result;
    
    // 接收消息
    std::vector<char> buffer(header.data_size);
    struct iovec iov = { buffer.data(), buffer.size() };
    
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    
    auto start = std::chrono::high_resolution_clock::now();
    ssize_t n = recvmsg(sockfd, &msg, 0);
    auto end = std::chrono::high_resolution_clock::now();
    
    if (n < 0) {
        result.success = false;
        result.error_message = "recvmsg failed: " + std::string(strerror(errno));
    } else {
        result.success = true;
        result.bytes_transferred = n;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}

TestResult TestHandlers::handle_sendfile_test(int sockfd, const TestHeader& header) {
    TestResult result;
    
    // 创建临时文件
    char filename[] = "/tmp/test_sendfile_XXXXXX";
    int fd = mkstemp(filename);
    if (fd < 0) {
        result.success = false;
        result.error_message = "Failed to create temp file";
        return result;
    }
    
    // 写入测试数据
    std::vector<char> test_data = generate_test_data(header.data_size);
    write(fd, test_data.data(), test_data.size());
    lseek(fd, 0, SEEK_SET);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // 使用sendfile发送文件
    off_t offset = 0;
    ssize_t n = sendfile(sockfd, fd, &offset, header.data_size);
    
    auto end = std::chrono::high_resolution_clock::now();
    
    close(fd);
    unlink(filename);
    
    if (n < 0) {
        result.success = false;
        result.error_message = "sendfile failed: " + std::string(strerror(errno));
    } else {
        result.success = true;
        result.bytes_transferred = n;
        result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
    
    return result;
}
