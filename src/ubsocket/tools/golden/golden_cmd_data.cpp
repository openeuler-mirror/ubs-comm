/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v2. You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE. See the
 * Mulan PSL v2 for more details.
 */
#include "golden_cmd_data.h"
#include "golden_crc.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <csignal>
#include <cstring>
#include <ctime>
#include <random>
#include <string>

namespace golden {

constexpr int MAX_EVENTS = 16;
constexpr size_t HEADER_SIZE = sizeof(uint32_t) * 3;
constexpr int64_t DEFAULT_MSG_COUNT = 100;
constexpr int64_t DEFAULT_MSG_SIZE = 1024;
constexpr size_t MAX_MSG_SIZE = 1 * 1024 * 1024;       // 1 MB
constexpr int64_t MICROSECONDS_PER_SECOND = 1000000LL; // 每秒微秒数
constexpr int MSG_IOVEC_COUNT = 4;                     // 消息头(seq, crc, msgSize) + 消息体
constexpr int MIN_PORT = 10000;
constexpr int MAX_PORT = 65535;
constexpr int64_t MAX_MSG_COUNT = 100000;
constexpr int64_t MAX_QPS = 100000;

static volatile sig_atomic_t g_quitFlag = 0;

static void HandleSignal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM) {
        g_quitFlag = 1;
    }
}

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

static uint32_t CalculateCRC32(const uint8_t *data, size_t len)
{
    return CRC::Crc32(data, len);
}

static bool IsValidIPv4(const std::string &ip)
{
    struct sockaddr_in addr;
    return inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) == 1;
}

void SubCommandData::SetRules() noexcept
{
    param_rules_[PARAM_ROLE] = {PARAM_ROLE, PDT_STR_ENUM, true, "", "client|server", ""};
    param_rules_[PARAM_PROTOCOL] = {PARAM_PROTOCOL, PDT_STR_ENUM, true, "tcp", "tcp|ub_rm_rtp|ub_rc_rtp", ""};
    param_rules_[PARAM_IP] = {PARAM_IP, PDT_STR, true, "", "", ""};
    param_rules_[PARAM_PORT] = {PARAM_PORT, PDT_INT64, true, 10001L, 10000, 65535, ""};

    param_rules_["msg-count"] = {
        "msg-count", PDT_INT64, false, DEFAULT_MSG_COUNT, 1, MAX_MSG_COUNT, "number of messages to send (client only)"};
    param_rules_["msg-size"] = {
        "msg-size", PDT_INT64, false, DEFAULT_MSG_SIZE, 1, MAX_MSG_SIZE, "size of each message in bytes (client only)"};
    param_rules_["qps"] = {
        "qps", PDT_INT64, false, 0, 0, MAX_QPS, "QPS limit for sending messages (0=unlimited, client only)"};

    example_.push_back("server: " + program + " " + name_ + " --" + PARAM_ROLE + "=server --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001");
    example_.push_back("client: " + program + " " + name_ + " --" + PARAM_ROLE + "=client --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001" +
                       " --msg-count=100 --msg-size=1024 --qps=1000");
}

int SubCommandData::DoInitialize() noexcept
{
    role_ = param_rules_[PARAM_ROLE].strRule.value;
    protocol_ = param_rules_[PARAM_PROTOCOL].strRule.value;
    ip_ = param_rules_[PARAM_IP].strRule.value;
    port_ = param_rules_[PARAM_PORT].int64Rule.value;
    msgCount_ = param_rules_["msg-count"].int64Rule.value;
    msgSize_ = param_rules_["msg-size"].int64Rule.value;
    qps_ = param_rules_["qps"].int64Rule.value;

    if (role_ != "client" && role_ != "server") {
        std::cout << "Error: Invalid role '" << role_ << "', must be 'client' or 'server'" << std::endl;
        return -1;
    }

    if (protocol_ != "tcp" && protocol_ != "ub_rm_rtp" && protocol_ != "ub_rc_rtp") {
        std::cout << "Error: Invalid protocol '" << protocol_ << "', must be 'tcp', 'ub_rm_rtp' or 'ub_rc_rtp'"
                  << std::endl;
        return -1;
    }

    if (!IsValidIPv4(ip_)) {
        std::cout << "Error: Invalid IPv4 address '" << ip_ << "'" << std::endl;
        return -1;
    }

    if (port_ < MIN_PORT || port_ > MAX_PORT) {
        std::cout << "Error: Invalid port " << port_ << ", must be between " << MIN_PORT << " and " << MAX_PORT
                  << std::endl;
        return -1;
    }

    if (role_ == "client") {
        if (msgCount_ < 1 || msgCount_ > MAX_MSG_COUNT) {
            std::cout << "Error: msg-count must be between 1 and " << MAX_MSG_COUNT << ", got " << msgCount_
                      << std::endl;
            return -1;
        }
        if (msgSize_ < 1 || msgSize_ > static_cast<int64_t>(MAX_MSG_SIZE)) {
            std::cout << "Error: msg-size must be between 1 and " << MAX_MSG_SIZE << ", got " << msgSize_ << std::endl;
            return -1;
        }
        if (qps_ < 0 || qps_ > MAX_QPS) {
            std::cout << "Error: qps must be between 0 and " << MAX_QPS << ", got " << qps_ << std::endl;
            return -1;
        }
    }

    return 0;
}

class TokenBucket {
public:
    explicit TokenBucket(int64_t rate, int64_t burst = 0)
        : rate_(rate <= 0 ? INT64_MAX : rate),
          tokens_(burst > 0 ? burst : (rate <= 0 ? INT64_MAX : rate)),
          lastUpdate_(GetTimeUs())
    {
    }

    bool TryAcquire()
    {
        if (rate_ == INT64_MAX) {
            return true;
        }

        RefillTokens();
        if (tokens_ >= 1) {
            tokens_ -= 1;
            return true;
        }
        return false;
    }

    int64_t WaitTimeUs() const
    {
        if (rate_ == INT64_MAX) {
            return 0;
        }

        if (tokens_ >= 1) {
            return 0;
        }

        double intervalUs = 1000000.0 / rate_;
        return static_cast<int64_t>(intervalUs - (GetTimeUs() - lastUpdate_));
    }

private:
    void RefillTokens()
    {
        if (rate_ == INT64_MAX) {
            return;
        }

        int64_t now = GetTimeUs();
        double elapsedUs = now - lastUpdate_;
        double tokensToAdd = (elapsedUs / 1000000.0) * rate_;

        tokens_ += tokensToAdd;
        lastUpdate_ = now;

        double maxTokens = rate_;
        if (tokens_ > maxTokens) {
            tokens_ = maxTokens;
        }
    }

    static int64_t GetTimeUs()
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * MICROSECONDS_PER_SECOND + ts.tv_nsec / 1000;
    }

    int64_t rate_;       // 每秒生成的令牌数（QPS）
    double tokens_;      // 当前令牌数
    int64_t lastUpdate_; // 上次更新时间（微秒）
};

// 消息头结构
struct MessageHeader {
    uint32_t seq;
    uint32_t crc;
    uint32_t msgSize;
};

// 发送单个消息
ssize_t SendMessage(int fd, uint32_t seq, uint32_t crc, uint32_t msgSize, const uint8_t *msgData)
{
    struct iovec iov[MSG_IOVEC_COUNT];
    iov[0].iov_base = &seq;
    iov[0].iov_len = sizeof(uint32_t);
    iov[1].iov_base = &crc;
    iov[1].iov_len = sizeof(uint32_t);
    iov[2].iov_base = &msgSize;
    iov[2].iov_len = sizeof(uint32_t);
    iov[3].iov_base = const_cast<uint8_t *>(msgData);
    iov[3].iov_len = msgSize;

    return ubsocket_writev(fd, iov, MSG_IOVEC_COUNT);
}

// 接收消息并解析
bool ReceiveMessage(int fd, uint8_t *recvBuf, ssize_t &recvOffset, size_t maxBufSize, MessageHeader &header)
{
    while (recvOffset < static_cast<ssize_t>(HEADER_SIZE)) {
        struct iovec iov[1];
        iov[0].iov_base = recvBuf + recvOffset;
        iov[0].iov_len = HEADER_SIZE - recvOffset;

        ssize_t recvd = ubsocket_readv(fd, iov, 1);
        if (recvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;
            }
            return false;
        }
        if (recvd == 0) {
            return false;
        }
        recvOffset += recvd;
    }

    // 解析消息头
    header.seq = *reinterpret_cast<uint32_t *>(recvBuf);
    header.crc = *reinterpret_cast<uint32_t *>(recvBuf + sizeof(uint32_t));
    header.msgSize = *reinterpret_cast<uint32_t *>(recvBuf + sizeof(uint32_t) * 2);

    // 检查消息大小是否合法
    if (header.msgSize > MAX_MSG_SIZE) {
        return false;
    }

    // 接收消息体
    size_t totalNeeded = HEADER_SIZE + header.msgSize;
    while (recvOffset < static_cast<ssize_t>(totalNeeded)) {
        if (recvOffset >= static_cast<ssize_t>(maxBufSize)) {
            return false;
        }

        struct iovec iov[1];
        iov[0].iov_base = recvBuf + recvOffset;
        iov[0].iov_len = totalNeeded - recvOffset;

        ssize_t recvd = ubsocket_readv(fd, iov, 1);
        if (recvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;
            }
            return false;
        }
        if (recvd == 0) {
            return false;
        }
        recvOffset += recvd;
    }

    return true;
}

class SubCommandData::DataClient {
public:
    explicit DataClient(SubCommandData &cmd) : cmd_(cmd) {}
    ~DataClient();
    int Run();

private:
    SubCommandData &cmd_;
    int fd_ = -1;
    int epollFd_ = -1;
    uint8_t *sendBuf_ = nullptr;
    uint8_t *recvBuf_ = nullptr;
    int64_t msgSent_ = 0;
    int64_t msgRecv_ = 0;
    int64_t errorCount_ = 0;
    bool connected_ = false;
    ssize_t recvOffset_ = 0;

    void Cleanup();
    int SendMessages(TokenBucket &tokenBucket, uint64_t &totalBytesSent);
    int ReceiveResponses(uint64_t &totalBytesRecv);
};

SubCommandData::DataClient::~DataClient()
{
    Cleanup();
}

void SubCommandData::DataClient::Cleanup()
{
    if (sendBuf_) {
        free(sendBuf_);
        sendBuf_ = nullptr;
    }
    if (recvBuf_) {
        free(recvBuf_);
        recvBuf_ = nullptr;
    }
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

int SubCommandData::DataClient::Run()
{
    sendBuf_ = reinterpret_cast<uint8_t *>(malloc(cmd_.msgSize_));
    recvBuf_ = reinterpret_cast<uint8_t *>(malloc(cmd_.msgSize_ + HEADER_SIZE));
    if (!sendBuf_ || !recvBuf_) {
        std::cout << "Error: malloc failed" << std::endl;
        return -1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    for (size_t i = 0; i < static_cast<size_t>(cmd_.msgSize_); ++i) {
        sendBuf_[i] = static_cast<uint8_t>(dist(gen) & 0xFF);
    }

    fd_ = ubsocket_socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cout << "Error: create socket failed, errno: " << errno << std::endl;
        return -errno;
    }

    if (SetNonBlocking(fd_) < 0) {
        std::cout << "Error: set non-blocking failed, errno: " << errno << std::endl;
        return -errno;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cmd_.port_);
    inet_pton(AF_INET, cmd_.ip_.c_str(), &addr.sin_addr);

    int ret = ubsocket_connect(fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        std::cout << "Error: connect failed, errno: " << errno << std::endl;
        return -errno;
    }

    epollFd_ = ubsocket_epoll_create1(0);
    if (epollFd_ < 0) {
        std::cout << "Error: create epoll failed, errno: " << errno << std::endl;
        return -errno;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET; // 使用边缘触发
    ev.data.fd = fd_;
    if (ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd_, &ev) < 0) {
        std::cout << "Error: epoll_ctl add failed, errno: " << errno << std::endl;
        return -errno;
    }

    // 默认超时时间，用于等待响应
    const int defaultEpollTimeoutMs = 1000;

    // 创建令牌桶（类似 brpc 的 QPS 控制）
    TokenBucket tokenBucket(cmd_.qps_, cmd_.qps_ > 0 ? cmd_.qps_ : 0);

    auto timeStart = Func::TimeUs();
    uint64_t totalBytesSent = 0;
    uint64_t totalBytesRecv = 0;

    // 异步模式：不等待响应，持续发送消息
    while (msgRecv_ < cmd_.msgCount_) {
        // 异步发送：尽可能多地发送消息
        if (connected_ && msgSent_ < cmd_.msgCount_) {
            int sendRet = SendMessages(tokenBucket, totalBytesSent);
            if (sendRet < 0) {
                return sendRet;
            }
        }

        struct epoll_event events[MAX_EVENTS];
        int nfds = ubsocket_epoll_wait(epollFd_, events, MAX_EVENTS, defaultEpollTimeoutMs);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cout << "Error: epoll_wait failed, errno: " << errno << std::endl;
            return -errno;
        }

        if (nfds == 0) {
            // 继续循环尝试发送更多消息
            continue;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == fd_) {
                if (events[i].events & EPOLLOUT) {
                    // 异步模式：处理连接建立或继续发送消息
                    if (!connected_) {
                        int error = 0;
                        socklen_t len = sizeof(error);
                        if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                            std::cout << "Error: getsockopt failed, errno: " << errno << std::endl;
                            return -errno;
                        }
                        if (error != 0) {
                            std::cout << "Error: connect failed, error: " << error << std::endl;
                            return -error;
                        }
                        connected_ = true;
                    }

                    // 继续发送消息（异步模式，不等待响应）
                    int sendRet2 = SendMessages(tokenBucket, totalBytesSent);
                    if (sendRet2 < 0) {
                        return sendRet2;
                    }
                }

                if (events[i].events & EPOLLIN) {
                    // 接收响应
                    int recvRet = ReceiveResponses(totalBytesRecv);
                    if (recvRet < 0) {
                        return recvRet;
                    }
                }

                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    std::cout << "Error: epoll error or hangup" << std::endl;
                    return -1;
                }
            }
        }
    }

    auto timeEnd = Func::TimeUs();
    double durationMs = (timeEnd - timeStart) / 1000.0;
    double throughputMbps = (totalBytesSent * 8.0) / (1024 * 1024 * durationMs / 1000);

    std::cout << "\n=== Data Client Report ===" << std::endl;
    std::cout << "Messages sent: " << msgSent_ << std::endl;
    std::cout << "Messages received: " << msgRecv_ << std::endl;
    std::cout << "CRC errors: " << errorCount_ << std::endl;
    std::cout << "Total bytes sent: " << totalBytesSent << std::endl;
    std::cout << "Total bytes received: " << totalBytesRecv << std::endl;
    std::cout << "Duration: " << durationMs << " ms" << std::endl;
    std::cout << "Throughput: " << throughputMbps << " Mbps" << std::endl;

    return 0;
}

class SubCommandData::DataServer {
public:
    explicit DataServer(SubCommandData &cmd) : cmd_(cmd) {}
    ~DataServer();
    int Run();

private:
    SubCommandData &cmd_;
    int listenFd_ = -1;
    int epollFd_ = -1;
    int clientFd_ = -1;
    uint8_t *recvBuf_ = nullptr;
    uint8_t *sendBuf_ = nullptr;
    int64_t msgRecv_ = 0;
    int64_t msgSent_ = 0;
    int64_t errorCount_ = 0;
    ssize_t recvOffset_ = 0;
    ssize_t pendingSendSize_ = 0;

    void Cleanup();
};

SubCommandData::DataServer::~DataServer()
{
    Cleanup();
}

// DataClient::SendMessages - 发送消息（带 QPS 控制）
int SubCommandData::DataClient::SendMessages(TokenBucket &tokenBucket, uint64_t &totalBytesSent)
{
    const int defaultEpollTimeoutMs = 1000;

    while (connected_ && msgSent_ < cmd_.msgCount_) {
        // 使用令牌桶控制 QPS
        if (cmd_.qps_ > 0) {
            if (!tokenBucket.TryAcquire()) {
                // 没有令牌，计算需要等待多久
                int64_t waitUs = tokenBucket.WaitTimeUs();
                if (waitUs > 0) {
                    // 等待令牌生成
                    usleep(waitUs);
                }
                continue;
            }
        }

        uint32_t crc = CalculateCRC32(sendBuf_, cmd_.msgSize_);
        uint32_t seq = static_cast<uint32_t>(msgSent_);
        uint32_t msgSize = static_cast<uint32_t>(cmd_.msgSize_);

        ssize_t sent = SendMessage(fd_, seq, crc, msgSize, sendBuf_);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cout << "Error: writev failed, errno: " << errno << std::endl;
            return -errno;
        }

        msgSent_++;
        totalBytesSent += sent;
    }
    return 0;
}

// DataClient::ReceiveResponses - 接收并处理响应
int SubCommandData::DataClient::ReceiveResponses(uint64_t &totalBytesRecv)
{
    while (true) {
        struct iovec iov[2];
        iov[0].iov_base = recvBuf_ + recvOffset_;
        iov[0].iov_len = HEADER_SIZE + cmd_.msgSize_ - recvOffset_;

        ssize_t recvd = ubsocket_readv(fd_, iov, 1);
        if (recvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cout << "Error: readv failed, errno: " << errno << std::endl;
            return -errno;
        }
        if (recvd == 0) {
            std::cout << "[CLIENT] Error: connection closed by peer" << std::endl;
            return -1;
        }

        recvOffset_ += recvd;
        totalBytesRecv += recvd;

        // 解析完整的消息
        while (recvOffset_ >= static_cast<ssize_t>(HEADER_SIZE)) {
            uint32_t recvMsgSize = *reinterpret_cast<uint32_t *>(recvBuf_ + sizeof(uint32_t) * 2);

            if (recvMsgSize > MAX_MSG_SIZE) {
                std::cout << "Error: invalid message size " << recvMsgSize << " from server" << std::endl;
                return -1;
            }

            if (recvOffset_ < static_cast<ssize_t>(HEADER_SIZE + recvMsgSize)) {
                break; // 数据不完整，等待更多数据
            }

            // CRC32 校验
            uint32_t recvCrc = *reinterpret_cast<uint32_t *>(recvBuf_ + sizeof(uint32_t));
            uint32_t calcCrc = CalculateCRC32(recvBuf_ + HEADER_SIZE, recvMsgSize);
            if (recvCrc != calcCrc) {
                errorCount_++;
                std::cout << "[CLIENT] Warning: CRC mismatch at message " << msgRecv_
                          << ", direction: Server->Client, expected: " << calcCrc << ", got: " << recvCrc << std::endl;
            }
            msgRecv_++;

            // 移动剩余数据到缓冲区开头
            size_t moveSize = recvOffset_ - (HEADER_SIZE + recvMsgSize);
            if (moveSize > 0) {
                memmove(recvBuf_, recvBuf_ + HEADER_SIZE + recvMsgSize, moveSize);
            }
            recvOffset_ -= (HEADER_SIZE + recvMsgSize);
        }
    }
    return 0;
}

void SubCommandData::DataServer::Cleanup()
{
    if (recvBuf_) {
        free(recvBuf_);
        recvBuf_ = nullptr;
    }
    if (sendBuf_) {
        free(sendBuf_);
        sendBuf_ = nullptr;
    }
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
    if (clientFd_ >= 0) {
        close(clientFd_);
        clientFd_ = -1;
    }
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

int SubCommandData::DataServer::Run()
{
    sendBuf_ = reinterpret_cast<uint8_t *>(malloc(MAX_MSG_SIZE + HEADER_SIZE));
    recvBuf_ = reinterpret_cast<uint8_t *>(malloc(MAX_MSG_SIZE + HEADER_SIZE));
    if (!sendBuf_ || !recvBuf_) {
        std::cout << "Error: malloc failed" << std::endl;
        return -1;
    }

    listenFd_ = ubsocket_socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cout << "Error: create socket failed, errno: " << errno << std::endl;
        return -errno;
    }

    int opt = 1;
    if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cout << "Error: setsockopt failed, errno: " << errno << std::endl;
        return -errno;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cmd_.port_);
    inet_pton(AF_INET, cmd_.ip_.c_str(), &addr.sin_addr);

    if (ubsocket_bind(listenFd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cout << "Error: bind failed, errno: " << errno << std::endl;
        return -errno;
    }

    if (ubsocket_listen(listenFd_, 1) < 0) {
        std::cout << "Error: listen failed, errno: " << errno << std::endl;
        return -errno;
    }

    epollFd_ = ubsocket_epoll_create1(0);
    if (epollFd_ < 0) {
        std::cout << "Error: create epoll failed, errno: " << errno << std::endl;
        return -errno;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;
    if (ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev) < 0) {
        std::cout << "Error: epoll_ctl add listen failed, errno: " << errno << std::endl;
        return -errno;
    }

    std::cout << "Data server listening on " << cmd_.ip_ << ":" << cmd_.port_ << std::endl;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = HandleSignal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    uint64_t totalBytesRecv = 0;
    uint64_t totalBytesSent = 0;
    auto timeStart = Func::TimeUs();

    while (!g_quitFlag) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = ubsocket_epoll_wait(epollFd_, events, MAX_EVENTS, 1000);
        if (nfds == 0) {
            continue;
        }
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cout << "Error: epoll_wait failed, errno: " << errno << std::endl;
            return -errno;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listenFd_) {
                while (true) {
                    struct sockaddr_in clientAddr;
                    socklen_t len = sizeof(clientAddr);
                    int fd = ubsocket_accept(listenFd_, reinterpret_cast<struct sockaddr *>(&clientAddr), &len);
                    if (fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 立即退出 accept 循环，不要继续调用 accept
                        }
                        std::cout << "Error: accept failed, errno: " << errno << std::endl;
                        return -errno;
                    }

                    if (clientFd_ >= 0) {
                        close(clientFd_);
                    }
                    clientFd_ = fd;

                    // 重置时间起点，只计算实际处理消息的时间
                    timeStart = Func::TimeUs();

                    if (SetNonBlocking(clientFd_) < 0) {
                        std::cout << "Error: set non-blocking failed, errno: " << errno << std::endl;
                        return -errno;
                    }

                    ev.events = EPOLLIN | EPOLLOUT | EPOLLET; // 使用边缘触发
                    ev.data.fd = clientFd_;
                    int epollRet = ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, clientFd_, &ev);
                    if (epollRet < 0) {
                        std::cout << "Error: epoll_ctl add client failed, errno: " << errno << std::endl;
                        return -errno;
                    }

                    char ipStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
                    std::cout << "Client connected: " << ipStr << ":" << ntohs(clientAddr.sin_port) << std::endl;
                    break; // 退出 accept 循环，等待客户端事件
                }
            }
            if (events[i].data.fd == clientFd_) {
                if (events[i].events & EPOLLOUT) {
                    // 边缘触发：使用 while 循环持续发送直到 EAGAIN 或发送完成
                    while (pendingSendSize_ > 0) {
                        struct iovec sendIov[1];
                        sendIov[0].iov_base = sendBuf_;
                        sendIov[0].iov_len = pendingSendSize_;

                        ssize_t sent = ubsocket_writev(clientFd_, sendIov, 1);
                        if (sent < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                break; // 缓冲区满了，等待下一次 EPOLLOUT 事件
                            }
                            std::cout << "Error: writev failed, errno: " << errno << std::endl;
                            return -errno;
                        } else {
                            totalBytesSent += sent;
                            msgSent_++;
                            pendingSendSize_ -= sent;
                        }
                    }
                }

                if (events[i].events & EPOLLIN) {
                    while (true) {
                        if (recvOffset_ >= static_cast<ssize_t>(MAX_MSG_SIZE + HEADER_SIZE)) {
                            std::cout << "Error: recvOffset_ " << recvOffset_ << " exceeds buffer size "
                                      << (MAX_MSG_SIZE + HEADER_SIZE) << std::endl;
                            return -1;
                        }

                        struct iovec iov[1];
                        iov[0].iov_base = recvBuf_ + recvOffset_;
                        iov[0].iov_len = MAX_MSG_SIZE + HEADER_SIZE - recvOffset_;

                        ssize_t recvd = ubsocket_readv(clientFd_, iov, 1);
                        if (recvd < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                break;
                            }
                            std::cout << "Error: readv failed, errno: " << errno << std::endl;
                            return -errno;
                        }
                        if (recvd == 0) {
                            std::cout << "Client disconnected" << std::endl;

                            auto timeEnd = Func::TimeUs();
                            double durationMs = (timeEnd - timeStart) / 1000.0;
                            std::cout << "\n=== Data Server Report ===" << std::endl;
                            std::cout << "Messages received: " << msgRecv_ << std::endl;
                            std::cout << "Messages sent: " << msgSent_ << std::endl;
                            std::cout << "CRC errors: " << errorCount_ << std::endl;
                            std::cout << "Total bytes received: " << totalBytesRecv << std::endl;
                            std::cout << "Total bytes sent: " << totalBytesSent << std::endl;
                            std::cout << "Duration: " << durationMs << " ms" << std::endl;

                            msgRecv_ = 0;
                            msgSent_ = 0;
                            errorCount_ = 0;
                            totalBytesRecv = 0;
                            totalBytesSent = 0;
                            timeStart = Func::TimeUs();

                            ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_DEL, clientFd_, nullptr);
                            close(clientFd_);
                            clientFd_ = -1;
                            recvOffset_ = 0;
                            break;
                        }

                        recvOffset_ += recvd;
                        totalBytesRecv += recvd;

                        while (recvOffset_ >= static_cast<ssize_t>(HEADER_SIZE)) {
                            uint32_t msgSize = *reinterpret_cast<uint32_t *>(recvBuf_ + sizeof(uint32_t) * 2);
                            if (msgSize > MAX_MSG_SIZE) {
                                std::cout << "Error: message size " << msgSize << " exceeds maximum" << std::endl;
                                return -1;
                            }

                            if (recvOffset_ < static_cast<ssize_t>(HEADER_SIZE + msgSize)) {
                                break;
                            }

                            uint32_t recvCrc = *reinterpret_cast<uint32_t *>(recvBuf_ + sizeof(uint32_t));
                            uint32_t calcCrc = CalculateCRC32(recvBuf_ + HEADER_SIZE, msgSize);
                            if (recvCrc != calcCrc) {
                                errorCount_++;
                                std::cout << "[SERVER] Warning: CRC mismatch at message " << msgRecv_
                                          << ", direction: Client->Server, expected: " << calcCrc
                                          << ", got: " << recvCrc << std::endl;
                            }

                            size_t copySize = HEADER_SIZE + msgSize;
                            if (copySize > 0) {
                                memcpy(sendBuf_, recvBuf_, copySize);
                            }

                            pendingSendSize_ = HEADER_SIZE + msgSize;

                            // 边缘触发：尝试立即发送，使用 while 循环持续发送直到 EAGAIN
                            while (pendingSendSize_ > 0) {
                                struct iovec sendIov[1];
                                sendIov[0].iov_base = sendBuf_;
                                sendIov[0].iov_len = pendingSendSize_;

                                ssize_t sent = ubsocket_writev(clientFd_, sendIov, 1);
                                if (sent < 0) {
                                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                        break;
                                    }
                                    std::cout << "Error: writev failed, errno: " << errno << std::endl;
                                    return -errno;
                                } else {
                                    totalBytesSent += sent;
                                    msgSent_++;
                                    pendingSendSize_ -= sent;
                                }
                            }

                            // 如果发送没有完成，等待下一次 EPOLLOUT 事件
                            if (pendingSendSize_ > 0) {
                                break;
                            }

                            msgRecv_++;

                            size_t moveSize = recvOffset_ - (HEADER_SIZE + msgSize);
                            if (moveSize > 0) {
                                memmove(recvBuf_, recvBuf_ + HEADER_SIZE + msgSize, moveSize);
                            }
                            recvOffset_ -= (HEADER_SIZE + msgSize);
                        }
                    }

                    if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                        std::cout << "Client disconnected (error/hangup)" << std::endl;

                        auto timeEnd = Func::TimeUs();
                        double durationMs = (timeEnd - timeStart) / 1000.0;
                        std::cout << "\n=== Data Server Report ===" << std::endl;
                        std::cout << "Messages received: " << msgRecv_ << std::endl;
                        std::cout << "Messages sent: " << msgSent_ << std::endl;
                        std::cout << "CRC errors: " << errorCount_ << std::endl;
                        std::cout << "Total bytes received: " << totalBytesRecv << std::endl;
                        std::cout << "Total bytes sent: " << totalBytesSent << std::endl;
                        std::cout << "Duration: " << durationMs << " ms" << std::endl;

                        msgRecv_ = 0;
                        msgSent_ = 0;
                        errorCount_ = 0;
                        totalBytesRecv = 0;
                        totalBytesSent = 0;
                        timeStart = Func::TimeUs();

                        ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_DEL, clientFd_, nullptr);
                        close(clientFd_);
                        clientFd_ = -1;
                        recvOffset_ = 0;
                    }
                }
            }
        }
    }

    return 0;
}

int SubCommandData::DoExecute() noexcept
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
        DataClient client(*this);
        return client.Run();
    } else if (role_ == "server") {
        DataServer server(*this);
        return server.Run();
    }

    std::cout << "Invalid role" << std::endl;
    return -1;
}

SubCommand *CreateData(const ParamMap &params)
{
    return new (std::nothrow) SubCommandData(SUB_CMD_DATA, params);
}

} // namespace golden