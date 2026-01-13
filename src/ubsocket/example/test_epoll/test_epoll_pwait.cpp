/*
* Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_EVENTS 10
#define TEST_MSG "Test message"

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    printf("=== Test 1: Basic epoll_pwait functionality ===\n");
    
    // 创建 epoll 实例
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1 failed");
        exit(EXIT_FAILURE);
    }

    // 使用 socketpair 创建建链（参考您的代码）
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair failed");
        close(epfd);
        exit(EXIT_FAILURE);
    }

    int read_fd = sv[0];
    int write_fd = sv[1];

    // 设置非阻塞（参考您的做法）
    if (set_nonblock(read_fd) < 0) {
        perror("set_nonblock failed");
        close(epfd);
        close(sv[0]);
        close(sv[1]);
        exit(EXIT_FAILURE);
    }

    // 添加到 epoll（使用 LT 模式避免复杂性）
    struct epoll_event event;
    event.events = EPOLLIN;  // 使用 LT 模式而不是 ET
    event.data.fd = read_fd;
    
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, read_fd, &event) < 0) {
        perror("epoll_ctl failed");
        close(epfd);
        close(sv[0]);
        close(sv[1]);
        exit(EXIT_FAILURE);
    }

    // 发送数据
    ssize_t sent = send(write_fd, TEST_MSG, strlen(TEST_MSG), 0);
    if (sent < 0) {
        perror("send failed");
        close(epfd);
        close(sv[0]);
        close(sv[1]);
        exit(EXIT_FAILURE);
    }
    printf("Sent %zd bytes to socket\n", sent);

    // 等待事件
    struct epoll_event events[MAX_EVENTS];
    printf("Calling epoll_pwait (timeout=1000ms)...\n");
    
    int nfds = epoll_pwait(epfd, events, MAX_EVENTS, 1000, NULL);
    if (nfds < 0) {
        perror("epoll_pwait failed");
        close(epfd);
        close(sv[0]);
        close(sv[1]);
        exit(EXIT_FAILURE);
    }

    printf("epoll_pwait returned %d events\n", nfds);
    for (int i = 0; i < nfds; i++) {
        printf("  Event %d: fd=%d, events=IN \n", i, events[i].data.fd);
        
        char buffer[100];
        ssize_t bytes_received = recv(events[i].data.fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("    Received: %s\n", buffer);
        }
    }

    // 清理资源
    close(epfd);
    close(sv[0]);
    close(sv[1]);
    
    printf("\n======================================\n");
    printf("All tests completed!\n");
    
    return 0;
}