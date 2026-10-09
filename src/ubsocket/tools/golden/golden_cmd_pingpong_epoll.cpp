/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include "golden_cmd_pingpong_epoll.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <string>

namespace golden {

#define MAX_EVENTS 16
#define PING_MSG "ping"
#define PONG_MSG "pong"
#define MSG_LEN 4

static int SetNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -errno;
    }
    if (flags & O_NONBLOCK) {
        return 0;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void SubCommandPingpongEpoll::SetRules() noexcept
{
    param_rules_[PARAM_ROLE] = {PARAM_ROLE, PDT_STR_ENUM, true, "", "client|server", ""};
    param_rules_[PARAM_PROTOCOL] = {PARAM_PROTOCOL, PDT_STR_ENUM, true, "tcp", "tcp|ub_rm_rtp|ub_rc_rtp", ""};
    param_rules_[PARAM_IP] = {PARAM_IP, PDT_STR, true, "", "", ""};
    param_rules_[PARAM_PORT] = {PARAM_PORT, PDT_INT64, true, 10001L, 10000, 65535, ""};

    example_.push_back("server: " + program + " " + name_ + " --" + PARAM_ROLE + "=server --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001");
    example_.push_back("client: " + program + " " + name_ + " --" + PARAM_ROLE + "=client --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001");
}

int SubCommandPingpongEpoll::DoInitialize() noexcept
{
    role_ = param_rules_[PARAM_ROLE].strRule.value;
    protocol_ = param_rules_[PARAM_PROTOCOL].strRule.value;
    ip_ = param_rules_[PARAM_IP].strRule.value;
    port_ = param_rules_[PARAM_PORT].int64Rule.value;

    struct sockaddr_in addr;
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) <= 0) {
        std::cout << "Invalid ip '" << ip_ << "'" << std::endl;
        return -1;
    }
    return 0;
}

int SubCommandPingpongEpoll::DoExecute() noexcept
{
    u_init_options_t options;
    if (ubsocket_init_options(&options) != 0) {
        std::cout << "Inner error: set ubsocket options failed" << std::endl;
        return -1;
    }
    options.allowed_protocol = Func::ProtocolFromString(protocol_);
    if (ubsocket_init(&options) != 0) {
        std::cout << "Inner error: initialize ubsocket failed" << std::endl;
        return -1;
    }

    if (role_ == "client") {
        EpollClient client(*this);
        return client.Run();
    } else if (role_ == "server") {
        EpollServer server(*this);
        return server.Run();
    }
    std::cout << "Invalid role" << std::endl;
    return -1;
}

EpollClient::~EpollClient()
{
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

int EpollClient::Run()
{
    LOG_DEBUG("start epoll client (non-blocking + edge-triggered)");

    fd_ = ubsocket_socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        return -errno;
    }
    if (SetNonBlocking(fd_) < 0) {
        return -errno;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cmd_.port_);
    inet_pton(AF_INET, cmd_.ip_.c_str(), &addr.sin_addr);

    if (ubsocket_connect(fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0 && errno != EINPROGRESS) {
        std::cout << "Connect failed" << std::endl;
        return -errno;
    }

    epoll_fd_ = ubsocket_epoll_create1(0);
    if (epoll_fd_ < 0) {
        return -errno;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd_;
    ubsocket_epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd_, &ev);

    char ping_buf[] = PING_MSG;
    char pong_buf[MSG_LEN];
    struct iovec send_iov = {ping_buf, MSG_LEN};
    ssize_t recv_len = 0;

    int round = 0;
    bool connected = false;
    bool wait_pong = false;
    auto time_start = Func::TimeUs();

    while (round < cmd_.loop_times_) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = ubsocket_epoll_wait(epoll_fd_, events, MAX_EVENTS, cmd_.epoll_timeout_ms_);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (nfds == 0) {
            std::cout << "epoll_wait timeout" << std::endl;
            return -1;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd != fd_) {
                continue;
            }

            uint32_t evs = events[i].events;
            if (evs & (EPOLLERR | EPOLLHUP)) {
                int err = 0;
                socklen_t len = sizeof(err);
                getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
                std::cout << "socket error: " << err << std::endl;
                return -1;
            }

            if (evs & EPOLLOUT) {
                if (!connected) {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &len);
                    if (err != 0 && err != EINPROGRESS) {
                        std::cout << "connect error: " << err << std::endl;
                        return -1;
                    }
                    connected = true;
                    LOG_DEBUG("connected");
                }

                if (connected && !wait_pong) {
                    ssize_t n = ubsocket_writev(fd_, &send_iov, 1);
                    if (n < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            return -errno;
                        }
                    } else if (n == MSG_LEN) {
                        LOG_DEBUG("sent ping #" << (round + 1));
                        wait_pong = true;
                    }
                }
            }

            if (evs & EPOLLIN && wait_pong) {
                while (true) {
                    struct iovec recv_iov = {pong_buf + recv_len, static_cast<size_t>(MSG_LEN - recv_len)};
                    ssize_t n = ubsocket_readv(fd_, &recv_iov, 1);
                    if (n < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        return -errno;
                    } else if (n == 0) {
                        std::cout << "peer closed" << std::endl;
                        return -1;
                    }
                    recv_len += n;
                    if (recv_len == MSG_LEN) {
                        if (memcmp(pong_buf, PONG_MSG, MSG_LEN) != 0) {
                            std::cout << "recv error: expected '" << PONG_MSG << "', got '"
                                      << std::string(pong_buf, MSG_LEN) << "'" << std::endl;
                            return -1;
                        }
                        round++;
                        LOG_DEBUG("received pong #" << round);
                        wait_pong = false;
                        recv_len = 0;

                        if (round < cmd_.loop_times_) {
                            ssize_t sent = ubsocket_writev(fd_, &send_iov, 1);
                            if (sent == MSG_LEN) {
                                LOG_DEBUG("sent ping #" << (round + 1));
                                wait_pong = true;
                            } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                                return -errno;
                            }
                        }
                    }
                }
            }
        }
    }

    auto time_end = Func::TimeUs();
    std::cout << "Client finished: " << cmd_.loop_times_ << " pingpong in " << (time_end - time_start) << "us"
              << std::endl;
    return 0;
}

EpollServer::~EpollServer()
{
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
    if (client_fd_ >= 0) {
        close(client_fd_);
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
}

int EpollServer::Run()
{
    LOG_DEBUG("start epoll server (non-blocking + edge-triggered)");

    listen_fd_ = ubsocket_socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return -errno;
    }
    if (SetNonBlocking(listen_fd_) < 0) {
        return -errno;
    }

    int opt = 1;
    ubsocket_setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ubsocket_setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cmd_.port_);
    inet_pton(AF_INET, cmd_.ip_.c_str(), &addr.sin_addr);

    if (ubsocket_bind(listen_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        return -errno;
    }
    if (ubsocket_listen(listen_fd_, 3) < 0) {
        return -errno;
    }

    epoll_fd_ = ubsocket_epoll_create1(0);
    if (epoll_fd_ < 0) {
        return -errno;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = listen_fd_;
    ubsocket_epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev);

    char ping_buf[MSG_LEN];
    char pong_buf[] = PONG_MSG;
    ssize_t recv_len = 0;
    bool pending_pong = false;

    int round = 0;

    while (round < cmd_.loop_times_) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = ubsocket_epoll_wait(epoll_fd_, events, MAX_EVENTS, cmd_.epoll_timeout_ms_);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -errno;
        }
        if (nfds == 0) {
            continue;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t evs = events[i].events;

            if (evs & (EPOLLERR | EPOLLHUP)) {
                if (fd == client_fd_) {
                    return -1;
                }
                continue;
            }

            if (fd == listen_fd_) {
                struct sockaddr_in client_addr;
                socklen_t len = sizeof(client_addr);
                client_fd_ = ubsocket_accept(listen_fd_, (struct sockaddr *)&client_addr, &len);
                if (client_fd_ < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                    return -errno;
                }
                if (SetNonBlocking(client_fd_) < 0) {
                    close(client_fd_);
                    client_fd_ = -1;
                    return -errno;
                }

                struct epoll_event cev;
                cev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                cev.data.fd = client_fd_;
                ubsocket_epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd_, &cev);
                LOG_DEBUG("accepted client fd=" << client_fd_);
            } else if (fd == client_fd_) {
                if (evs & EPOLLIN) {
                    while (true) {
                        struct iovec recv_iov = {ping_buf + recv_len, static_cast<size_t>(MSG_LEN - recv_len)};
                        ssize_t n = ubsocket_readv(client_fd_, &recv_iov, 1);
                        if (n < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                break;
                            }
                            return -errno;
                        } else if (n == 0) {
                            std::cout << "client closed" << std::endl;
                            return -1;
                        }
                        recv_len += n;
                        if (recv_len == MSG_LEN) {
                            if (memcmp(ping_buf, PING_MSG, MSG_LEN) != 0) {
                                std::cout << "recv error: expected '" << PING_MSG << "', got '"
                                          << std::string(ping_buf, MSG_LEN) << "'" << std::endl;
                                return -1;
                            }
                            round++;
                            LOG_DEBUG("received ping #" << round);
                            recv_len = 0;

                            struct iovec send_iov = {pong_buf, MSG_LEN};
                            ssize_t sent = ubsocket_writev(client_fd_, &send_iov, 1);
                            if (sent < 0) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    pending_pong = true;
                                } else {
                                    return -errno;
                                }
                            } else if (sent == MSG_LEN) {
                                LOG_DEBUG("sent pong #" << round);
                            }
                        }
                    }
                }

                if (evs & EPOLLOUT && pending_pong) {
                    struct iovec send_iov = {pong_buf, MSG_LEN};
                    ssize_t sent = ubsocket_writev(client_fd_, &send_iov, 1);
                    if (sent < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            return -errno;
                        }
                    } else if (sent == MSG_LEN) {
                        LOG_DEBUG("sent pong #" << round);
                        pending_pong = false;
                    }
                }
            }
        }
    }

    std::cout << "Server finished: " << cmd_.loop_times_ << " pingpong" << std::endl;
    return 0;
}

} // namespace golden