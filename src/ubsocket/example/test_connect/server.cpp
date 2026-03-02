/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
*/
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <iostream>
#include <cstring>
#include <vector>

static constexpr int32_t ARGS_NUM = 4;
static constexpr int ARGS_2 = 2;
static constexpr int ARGS_3 = 3;
static constexpr int32_t BACKLOG = 1024;
static constexpr int32_t MAX_EVENTS = 16;
int32_t g_port;
int32_t g_isBlock; // 0为false，1为true
int32_t g_acceptType; // 0为accept，1为accept4

static int OsdParseArgs(int argc, char* argv[])
{
    if (argc != ARGS_NUM) {
        printf("invalid args, usage: %s <port>\n", argv[0]);
        return -1;
    }
    g_port = atoi(argv[1]);
    g_isBlock = atoi(argv[ARGS_2]);
    g_acceptType = atoi(argv[ARGS_3]);
    return 0;
}

static int SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int SetBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

static void AcceptHandler(int sockfd, int &newfd, int epollFd)
{
    if (!g_acceptType) {
        newfd = accept(sockfd, nullptr, nullptr);
        if (newfd == -1) {
            perror("accept failed");
            return;
        }
        std::cout << "accept fd " << newfd << std::endl;
    } else {
        newfd = accept4(sockfd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (newfd == -1) {
            perror("accept failed");
            return;
        }
        std::cout << "accept4 fd " << newfd << std::endl;
    }
    struct sockaddr_in cliaddr {};
    socklen_t len = sizeof(cliaddr);
    if (getpeername(newfd, reinterpret_cast<struct sockaddr *>(&cliaddr), &len) < 0) {
        perror("getpeername failed");
        return;
    }
    std::cout << inet_ntoa(cliaddr.sin_addr) << " " << ntohs(cliaddr.sin_port) << std::endl;
    struct epoll_event ev {};
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
    ev.data.fd = newfd;
    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, newfd, &ev);
    if (ret == -1) {
        perror("epoll_ctl: add newfd failed");
    }
}

static void ReadHandler(int newfd)
{
    int32_t buf = 0;
    int ret = read(newfd, &buf, sizeof(int32_t));
    if (ret != sizeof(int32_t)) {
        perror("read failed");
        return;
    }
    std::cout << "read from client: " << buf << std::endl;
    buf = ARGS_NUM;
    ret = write(newfd, &buf, sizeof(int32_t));
    if (ret != sizeof(int32_t)) {
        perror("write failed");
        return;
    }
    std::cout << "write to client: " << buf << std::endl;
}

static void EventHandle(struct epoll_event &event, int epollFd, int sockfd, int &newfd)
{
    if (event.data.fd == sockfd && (event.events & EPOLLIN)) {  // new connection
        std::cout << "fd " << sockfd << " new connection" << std::endl;
        AcceptHandler(sockfd, newfd, epollFd);
    } else if (event.data.fd == newfd) {
        if (event.events & EPOLLRDHUP) {  // data peerhangup
            std::cout << "client disconnected" << std::endl;
            epoll_ctl(epollFd, EPOLL_CTL_DEL, newfd, nullptr);
            close(newfd);
            newfd = -1;
        } else if (event.events & EPOLLIN) {  // data readble
            std::cout << "fd " << newfd << " data readable" << std::endl;
            ReadHandler(newfd);
        }
    }
}

static void EventLoop(int sockfd)
{
    int epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd == -1) {
        perror("epoll_create1 failed");
        return;
    }
    struct epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = sockfd;
    int ret = epoll_ctl(epollFd, EPOLL_CTL_ADD, sockfd, &ev);
    if (ret == -1) {
        perror("epoll_ctl: add sockfd failed");
        close(sockfd);
        return;
    }
    int newfd = -1;
    while (true) {  // loop
        struct epoll_event events[MAX_EVENTS];
        int eventCount = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        if (eventCount == -1) {
            perror("epoll_wait failed");
            return;
        }
        for (int i = 0; i < eventCount; ++i) {
            EventHandle(events[i], epollFd, sockfd, newfd);
        }
    }
}

int CreateSockfd()
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket failed");
        return -1;
    }
    if (g_isBlock) {
        SetBlocking(sockfd);
    } else {
        SetNonBlocking(sockfd);
    }
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, reinterpret_cast<void *>(&on), sizeof(on));
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sockfd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1) {
        perror("bind error");
        return -1;
    }
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen failed");
        return -1;
    }
    return sockfd;
}

int main(int argc, char *argv[])
{
    OsdParseArgs(argc, argv);

    int sockfd = CreateSockfd();
    std::cout << "create socket success, listening on port " << g_port
              << " sockfd " << sockfd << " isBlock " << g_isBlock << std::endl;
    EventLoop(sockfd);
}