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
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_EVENTS 10
#define LOCAL_PORT 38472

int main() {
    printf("=== Test: epoll_pwait with TCP connection establishment only ===\n");

    // ===== 1. 创建并绑定监听 socket =====
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int reuse = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LOCAL_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, 1) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // ===== 2. 创建 epoll 实例并添加 listen_fd =====
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;          // 监听可读：有新连接到达
    ev.data.fd = listen_fd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        perror("epoll_ctl ADD");
        close(epfd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // ===== 3. 启动客户端连接（触发 listen_fd 可读） =====
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("client socket");
        close(epfd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Client connecting to 127.0.0.1:%d...\n", LOCAL_PORT);
    if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(client_fd);
        close(epfd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    printf("Client connected successfully.\n");

    // ===== 4. 使用 epoll_pwait 等待 listen_fd 上的事件 =====
    printf("Calling epoll_pwait (waiting for incoming connection event)...\n");
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_pwait(epfd, events, MAX_EVENTS, 2000, NULL); // 2秒超时

    if (nfds < 0) {
        perror("epoll_pwait failed");
        close(client_fd);
        close(epfd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    } else if (nfds == 0) {
        fprintf(stderr, "Error: epoll_pwait timed out (no connection event)\n");
        close(client_fd);
        close(epfd);
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("epoll_pwait returned %d event(s)\n", nfds);
    for (int i = 0; i < nfds; i++) {
        if (events[i].data.fd == listen_fd && (events[i].events & EPOLLIN)) {
            printf("  Event: New connection ready on listen_fd\n");
            // 注意：我们这里只验证事件触发，**不调用 accept() 也可以**
            // （但通常你会 accept；不过按你要求，连 accept 都可以不做）
        }
    }

    // ===== 5. 清理（注意：未 accept，但连接已建立在 backlog 中）=====
    close(client_fd);
    close(epfd);
    close(listen_fd);

    printf("\n======================================\n");
    printf("TCP connection establishment test completed!\n");
    return 0;
}