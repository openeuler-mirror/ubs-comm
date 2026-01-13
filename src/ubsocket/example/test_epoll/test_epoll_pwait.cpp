/*
* Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
*/
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cerrno>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

#define MAX_EVENTS 10
#define TEST_PORT 12345
#define TEST_DATA "Hello from epoll_pwait test!"
#define TEST_DATA_LEN 29

// 设置非阻塞
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }
    return 0;
}

// 打印 epoll 事件
static const char* epoll_event_str(uint32_t events) {
    static char str[256];
    str[0] = '\0';

    if (events & EPOLLIN) strcat(str, "IN ");
    if (events & EPOLLOUT) strcat(str, "OUT ");
    if (events & EPOLLRDHUP) strcat(str, "RDHUP ");
    if (events & EPOLLPRI) strcat(str, "PRI ");
    if (events & EPOLLERR) strcat(str, "ERR ");
    if (events & EPOLLHUP) strcat(str, "HUP ");
    if (events & EPOLLET) strcat(str, "ET ");
    if (events & EPOLLONESHOT) strcat(str, "ONESHOT ");

    return str;
}

// 信号处理器
static void signal_handler(int sig) {
    std::cout << "[Signal Handler] Received signal: " << sig << std::endl;
}

// 获取当前时间戳
static std::string current_time() {
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    char buf[100];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&now_time));
    return std::string(buf);
}

void test_basic_epoll_pwait() {
    std::cout << "\n=== Test 1: Basic epoll_pwait functionality ===" << std::endl;
    
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return;
    }
    
    // 创建 socket pair
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        perror("socketpair");
        close(epoll_fd);
        return;
    }
    
    // 将 sv[0] 添加到 epoll 中
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // 边缘触发模式
    ev.data.fd = sv[0];
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sv[0], &ev) == -1) {
        perror("epoll_ctl ADD");
        close(sv[0]);
        close(sv[1]);
        close(epoll_fd);
        return;
    }
    
    // 向 sv[1] 写入数据
    const char* test_msg = "Test message";
    ssize_t sent = send(sv[1], test_msg, strlen(test_msg), 0);
    std::cout << "Sent " << sent << " bytes to socket" << std::endl;
    
    // 准备信号屏蔽字
    sigset_t mask, orig_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    
    // 保存原始信号屏蔽字
    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) == -1) {
        perror("sigprocmask");
    }
    
    // 使用 epoll_pwait 等待事件
    struct epoll_event events[MAX_EVENTS];
    std::cout << "Calling epoll_pwait (timeout=1000ms)..." << std::endl;
    
    int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, 1000, &orig_mask);
    
    if (nfds == -1) {
        perror("epoll_pwait");
    } else if (nfds == 0) {
        std::cout << "epoll_pwait timed out" << std::endl;
    } else {
        std::cout << "epoll_pwait returned " << nfds << " events" << std::endl;
        for (int i = 0; i < nfds; i++) {
            std::cout << "  Event " << i << ": fd=" << events[i].data.fd 
                      << ", events=" << epoll_event_str(events[i].events) << std::endl;
            
            if (events[i].events & EPOLLIN) {
                char buffer[1024];
                ssize_t n = recv(events[i].data.fd, buffer, sizeof(buffer) - 1, 0);
                if (n > 0) {
                    buffer[n] = '\0';
                    std::cout << "    Received: " << buffer << std::endl;
                }
            }
        }
    }
    
    // 清理
    close(sv[0]);
    close(sv[1]);
    close(epoll_fd);
}

void test_edge_vs_level_triggered() {
    std::cout << "\n=== Test 3: Edge-triggered vs Level-triggered ===" << std::endl;
    
    // 测试边缘触发
    std::cout << "--- Edge-triggered mode (EPOLLET) ---" << std::endl;
    int epoll_fd_et = epoll_create1(0);
    if (epoll_fd_et == -1) {
        perror("epoll_create1");
        return;
    }
    
    int sv_et[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv_et) == -1) {
        perror("socketpair");
        close(epoll_fd_et);
        return;
    }
    
    set_nonblock(sv_et[0]);
    
    struct epoll_event ev_et;
    ev_et.events = EPOLLIN | EPOLLET;
    ev_et.data.fd = sv_et[0];
    
    if (epoll_ctl(epoll_fd_et, EPOLL_CTL_ADD, sv_et[0], &ev_et) == -1) {
        perror("epoll_ctl ADD");
        close(sv_et[0]);
        close(sv_et[1]);
        close(epoll_fd_et);
        return;
    }
    
    // 写入数据
    send(sv_et[1], "A", 1, 0);
    
    // 第一次 epoll_pwait 应该会触发
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_pwait(epoll_fd_et, events, MAX_EVENTS, 100, NULL);
    std::cout << "First epoll_pwait (ET): returned " << nfds << " events" << std::endl;
    
    // 读取部分数据（不全部读取）
    char buf[10];
    recv(sv_et[0], buf, sizeof(buf), 0);
    
    // 第二次 epoll_pwait 不应该触发（因为没有新数据到达）
    nfds = epoll_pwait(epoll_fd_et, events, MAX_EVENTS, 100, NULL);
    std::cout << "Second epoll_pwait (ET, after partial read): returned " << nfds << " events" << std::endl;
    
    // 写入更多数据
    send(sv_et[1], "B", 1, 0);
    
    // 现在应该再次触发
    nfds = epoll_pwait(epoll_fd_et, events, MAX_EVENTS, 100, NULL);
    std::cout << "Third epoll_pwait (ET, after new data): returned " << nfds << " events" << std::endl;
    
    close(sv_et[0]);
    close(sv_et[1]);
    close(epoll_fd_et);
    
    // 测试水平触发
    std::cout << "\n--- Level-triggered mode (default) ---" << std::endl;
    int epoll_fd_lt = epoll_create1(0);
    if (epoll_fd_lt == -1) {
        perror("epoll_create1");
        return;
    }
    
    int sv_lt[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv_lt) == -1) {
        perror("socketpair");
        close(epoll_fd_lt);
        return;
    }
    
    struct epoll_event ev_lt;
    ev_lt.events = EPOLLIN; // 默认是水平触发
    ev_lt.data.fd = sv_lt[0];
    
    if (epoll_ctl(epoll_fd_lt, EPOLL_CTL_ADD, sv_lt[0], &ev_lt) == -1) {
        perror("epoll_ctl ADD");
        close(sv_lt[0]);
        close(sv_lt[1]);
        close(epoll_fd_lt);
        return;
    }
    
    // 写入数据
    send(sv_lt[1], "C", 1, 0);
    
    // 第一次 epoll_pwait 应该会触发
    nfds = epoll_pwait(epoll_fd_lt, events, MAX_EVENTS, 100, NULL);
    std::cout << "First epoll_pwait (LT): returned " << nfds << " events" << std::endl;
    
    // 不读取数据
    // 第二次 epoll_pwait 应该还会触发（因为数据仍然可读）
    nfds = epoll_pwait(epoll_fd_lt, events, MAX_EVENTS, 100, NULL);
    std::cout << "Second epoll_pwait (LT, without read): returned " << nfds << " events" << std::endl;
    
    // 读取数据
    recv(sv_lt[0], buf, sizeof(buf), 0);
    
    // 第三次 epoll_pwait 不应该触发（因为数据已读完）
    nfds = epoll_pwait(epoll_fd_lt, events, MAX_EVENTS, 100, NULL);
    std::cout << "Third epoll_pwait (LT, after read): returned " << nfds << " events" << std::endl;
    
    close(sv_lt[0]);
    close(sv_lt[1]);
    close(epoll_fd_lt);
}

void test_multiple_fds() {
    std::cout << "\n=== Test 5: Multiple file descriptors ===" << std::endl;
    
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return;
    }
    
    // 创建多个 socket pair
    const int NUM_PAIRS = 5;
    std::vector<int> read_fds, write_fds;
    
    for (int i = 0; i < NUM_PAIRS; i++) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
            perror("socketpair");
            continue;
        }
        
        read_fds.push_back(sv[0]);
        write_fds.push_back(sv[1]);
        
        // 添加到 epoll
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = sv[0];
        ev.data.u32 = i; // 使用 u32 存储索引
        
        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sv[0], &ev) == -1) {
            perror("epoll_ctl ADD");
        } else {
            std::cout << "Added fd " << sv[0] << " (index " << i << ") to epoll" << std::endl;
        }
    }
    
    // 向随机几个 socket 写入数据
    srand(time(NULL));
    int write_count = 0;
    for (int i = 0; i < NUM_PAIRS; i++) {
        if (rand() % 2) { // 50% 的概率
            char msg[50];
            snprintf(msg, sizeof(msg), "Message to fd %d (index %d)", read_fds[i], i);
            send(write_fds[i], msg, strlen(msg), 0);
            write_count++;
            std::cout << "Wrote to index " << i << std::endl;
        }
    }
    
    std::cout << "Wrote to " << write_count << " out of " << NUM_PAIRS << " sockets" << std::endl;
    
    // 使用 epoll_pwait 等待事件
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, 1000, NULL);
    
    if (nfds == -1) {
        perror("epoll_pwait");
    } else {
        std::cout << "\nepoll_pwait returned " << nfds << " events" << std::endl;
        
        for (int i = 0; i < nfds; i++) {
            std::cout << "  Event " << i << ": fd=" << events[i].data.fd 
                      << ", index=" << events[i].data.u32
                      << ", events=" << epoll_event_str(events[i].events) << std::endl;
            
            if (events[i].events & EPOLLIN) {
                char buffer[1024];
                ssize_t n = recv(events[i].data.fd, buffer, sizeof(buffer) - 1, 0);
                if (n > 0) {
                    buffer[n] = '\0';
                    std::cout << "    Received: " << buffer << std::endl;
                }
            }
        }
    }
    
    // 清理
    for (int fd : read_fds) close(fd);
    for (int fd : write_fds) close(fd);
    close(epoll_fd);
}

void test_performance() {
    std::cout << "\n=== Test 6: Performance test ===" << std::endl;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return;
    }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        perror("socketpair");
        close(epoll_fd);
        return;
    }

    set_nonblock(sv[0]);

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = sv[0];

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sv[0], &ev) == -1) {
        perror("epoll_ctl ADD");
        close(sv[0]);
        close(sv[1]);
        close(epoll_fd);
        return;
    }

    // 预热
    for (int i = 0; i < 1000; i++) {
        send(sv[1], "X", 1, 0);
        struct epoll_event events[MAX_EVENTS];
        epoll_pwait(epoll_fd, events, MAX_EVENTS, 0, NULL);
        char buf[10];
        recv(sv[0], buf, sizeof(buf), 0);
    }

    // 性能测试
    const int ITERATIONS = 100000;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; i++) {
        send(sv[1], "X", 1, 0);

        struct epoll_event events[MAX_EVENTS];
        epoll_pwait(epoll_fd, events, MAX_EVENTS, 0, NULL);

        char buf[10];
        recv(sv[0], buf, sizeof(buf), 0);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    std::cout << "Performed " << ITERATIONS << " iterations in "
              << duration.count() << " microseconds" << std::endl;
    std::cout << "Average time per iteration: "
              << (double)duration.count() / ITERATIONS << " microseconds" << std::endl;
    std::cout << "Operations per second: "
              << (ITERATIONS * 1000000.0) / duration.count() << std::endl;

    close(sv[0]);
    close(sv[1]);
    close(epoll_fd);
}

int main(int argc, char* argv[]) {
    std::cout << "Starting epoll_pwait tests..." << std::endl;
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "======================================" << std::endl;
    
    // 设置信号处理器，防止测试被中断
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    // 忽略 SIGPIPE，避免 send 导致进程退出
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction SIGPIPE");
    }
    
    // 运行所有测试
    test_basic_epoll_pwait();
    test_edge_vs_level_triggered();
    test_multiple_fds();
    test_performance();
    
    std::cout << "\n======================================" << std::endl;
    std::cout << "All tests completed!" << std::endl;
    
    return 0;
}
