/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <iostream>
#include <cstring>
#include <vector>

static constexpr int ARGS_NUM_2 = 2;
static constexpr int ARGS_NUM_3 = 3;
std::string g_ip;
int32_t g_port;

static int OsdParseArgs(int argc, char *argv[])
{
    if (argc != ARGS_NUM_3) {
        printf("invalid args, usage: %s <port>\n", argv[0]);
        return -1;
    }
    g_ip = std::string(argv[1]);
    g_port = atoi(argv[ARGS_NUM_2]);
    return 0;
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int set_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}


static int CreateSocket(int &sockfd, struct sockaddr_in &addr, bool isBlock)
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return -1;
    }
    std::cout << "socket" << std::endl;
    if (isBlock) {
        set_blocking(sockfd);
    } else {
        set_nonblocking(sockfd);
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    inet_pton(AF_INET, g_ip.c_str(), &addr.sin_addr);
    return 0;
}

static int Connect(bool isBlock)
{
    std::cout << "Test Connect " << (isBlock ? "[block] mode" : "[non-block] mode") << std::endl;
    int sockfd = -1;
    struct sockaddr_in addr {};
    if (CreateSocket(sockfd, addr, isBlock) < 0) {
        return -1;
    }

    std::cout << "connect isBlock " << isBlock << ", sockfd fd " << sockfd << ", server ip:port " << g_ip << ":"
              << g_port << std::endl;
    int ret = connect(sockfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (ret != 0) {
        perror("connect failed");
        return -1;
    }
    printf("connect ret %d, errno %d, %s\n", ret, errno,
            strerror(errno));  // 期待输出connect ret 0, errno 0, Success
            
    int32_t buf = 1;
    ret = write(sockfd, &buf, sizeof(int32_t));
    if (ret != sizeof(int32_t)) {
        perror("write failed");
        return -1;
    }
    std::cout << "write to server: " << buf << std::endl;

    sleep(1);

    ret = read(sockfd, &buf, sizeof(int32_t));
    if (ret != sizeof(int32_t)) {
        perror("read failed");
        return -1;
    }
    std::cout << "read from server: " << buf << std::endl;

    ret = shutdown(sockfd,0);
    if (ret != 0) {
        perror("shutdown failed");
        return -1;
    }
    std::cout << "shutdown" << std::endl;

    ret = close(sockfd);
    if (ret != 0) {
        perror("close failed");
        return -1;
    }
    std::cout << "close" << std::endl;
    return 0;
}

int main(int argc, char *argv[])
{
    OsdParseArgs(argc, argv);
    Connect(true);
    sleep(1);
    Connect(false);
}