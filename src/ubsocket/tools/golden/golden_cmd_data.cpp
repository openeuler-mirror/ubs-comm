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
#include <unordered_map>

namespace golden {

constexpr int MAX_EVENTS = 16;
constexpr size_t HEADER_SIZE = sizeof(uint32_t) * 3;
constexpr int64_t DEFAULT_MSG_COUNT = 100;
constexpr int64_t DEFAULT_MSG_SIZE = 1024;
constexpr size_t MAX_MSG_SIZE = 1 * 1024 * 1024;
constexpr int64_t MICROSECONDS_PER_SECOND = 1000000LL;
constexpr int MSG_IOVEC_COUNT = 4;
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

static inline struct sockaddr *AsSockaddr(struct sockaddr_in *addr)
{
    return static_cast<struct sockaddr *>(static_cast<void *>(addr));
}

static uint32_t ReadUint32Le(const uint8_t *buf)
{
    uint32_t val;
    memcpy(&val, buf, sizeof(val));
    return val;
}

static bool IsValidIPv4(const std::string &ip)
{
    struct sockaddr_in addr;
    return inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) == 1;
}

static bool ValidateParamNotEmpty(const std::string &name, const std::string &value)
{
    if (value.empty()) {
        std::cout << "Error: --" << name << " is required for client role" << std::endl;
        return false;
    }
    return true;
}

void SubCommandData::SetRules() noexcept
{
    param_rules_[PARAM_ROLE] = {PARAM_ROLE, PDT_STR_ENUM, true, "", "client|server", ""};
    param_rules_[PARAM_PROTOCOL] = {PARAM_PROTOCOL, PDT_STR_ENUM, true, "tcp", "tcp|ub_rm_rtp|ub_rc_rtp", ""};
    param_rules_[PARAM_IP] = {PARAM_IP, PDT_STR, false, "", "", "Server IP address (client only)"};
    param_rules_[PARAM_PORT] = {PARAM_PORT, PDT_INT64, true, 10001L, 10000, 65535, ""};

    param_rules_["msg-count"] = {
        "msg-count", PDT_INT64, false, DEFAULT_MSG_COUNT, 1, MAX_MSG_COUNT, "number of messages to send (client only)"};
    param_rules_["msg-size"] = {
        "msg-size", PDT_INT64, false, DEFAULT_MSG_SIZE, 1, MAX_MSG_SIZE, "size of each message in bytes (client only)"};
    param_rules_["qps"] = {
        "qps", PDT_INT64, false, 0, 0, MAX_QPS, "QPS limit for sending messages (0=unlimited, client only)"};

    example_.push_back("server: " + program + " " + name_ + " --" + PARAM_ROLE + "=server --" + PARAM_PROTOCOL +
                       "=tcp --" + PARAM_PORT + "=10001");
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

    if (int ret = ValidateCommonParams(); ret != 0) {
        return ret;
    }

    if (role_ == "client") {
        return ValidateClientParams();
    } else {
        return ValidateServerParams();
    }
}

int SubCommandData::ValidateCommonParams() noexcept
{
    if (role_ != "client" && role_ != "server") {
        std::cout << "Error: Invalid role '" << role_ << "', must be 'client' or 'server'" << std::endl;
        return -1;
    }

    if (protocol_ != "tcp" && protocol_ != "ub_rm_rtp" && protocol_ != "ub_rc_rtp") {
        std::cout << "Error: Invalid protocol '" << protocol_ << "', must be 'tcp', 'ub_rm_rtp' or 'ub_rc_rtp'"
                  << std::endl;
        return -1;
    }

    if (port_ < MIN_PORT || port_ > MAX_PORT) {
        std::cout << "Error: Invalid port " << port_ << ", must be between " << MIN_PORT << " and " << MAX_PORT
                  << std::endl;
        return -1;
    }

    return 0;
}

int SubCommandData::ValidateClientParams() noexcept
{
    if (!ValidateParamNotEmpty("ip", ip_)) {
        return -1;
    }
    if (!IsValidIPv4(ip_)) {
        std::cout << "Error: Invalid IPv4 address '" << ip_ << "'" << std::endl;
        return -1;
    }

    if (msgCount_ < 1 || msgCount_ > MAX_MSG_COUNT) {
        std::cout << "Error: msg-count must be between 1 and " << MAX_MSG_COUNT << ", got " << msgCount_ << std::endl;
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

    return 0;
}

int SubCommandData::ValidateServerParams() noexcept
{
    if (!ip_.empty()) {
        std::cout << "Error: --ip is not allowed for server role" << std::endl;
        return -1;
    }
    if (params_.find("msg-count") != params_.end()) {
        std::cout << "Error: --msg-count is not allowed for server role" << std::endl;
        return -1;
    }
    if (params_.find("msg-size") != params_.end()) {
        std::cout << "Error: --msg-size is not allowed for server role" << std::endl;
        return -1;
    }
    if (params_.find("qps") != params_.end()) {
        std::cout << "Error: --qps is not allowed for server role" << std::endl;
        return -1;
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

    int64_t rate_;
    double tokens_;
    int64_t lastUpdate_;
};

struct MessageHeader {
    uint32_t seq;
    uint32_t crc;
    uint32_t msgSize;
};

static ssize_t SendMessage(int fd, uint32_t seq, uint32_t crc, uint32_t msgSize, const uint8_t *msgData)
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

static bool ReadHeader(int fd, uint8_t *buf, ssize_t &offset)
{
    while (offset < static_cast<ssize_t>(HEADER_SIZE)) {
        struct iovec iov[1];
        iov[0].iov_base = buf + offset;
        iov[0].iov_len = HEADER_SIZE - offset;

        ssize_t recvd = ubsocket_readv(fd, iov, 1);
        if (recvd <= 0) {
            return false;
        }
        offset += recvd;
    }
    return true;
}

static bool ReadBody(int fd, uint8_t *buf, ssize_t &offset, size_t msgSize)
{
    size_t totalNeeded = HEADER_SIZE + msgSize;
    while (offset < static_cast<ssize_t>(totalNeeded)) {
        struct iovec iov[1];
        iov[0].iov_base = buf + offset;
        iov[0].iov_len = totalNeeded - offset;

        ssize_t recvd = ubsocket_readv(fd, iov, 1);
        if (recvd <= 0) {
            return false;
        }
        offset += recvd;
    }
    return true;
}

static bool ReceiveMessage(int fd, uint8_t *recvBuf, ssize_t &recvOffset, size_t maxBufSize, MessageHeader &header)
{
    if (!ReadHeader(fd, recvBuf, recvOffset)) {
        return false;
    }

    header.seq = *reinterpret_cast<uint32_t *>(recvBuf);
    header.crc = *reinterpret_cast<uint32_t *>(recvBuf + sizeof(uint32_t));
    header.msgSize = *reinterpret_cast<uint32_t *>(recvBuf + sizeof(uint32_t) * 2);

    if (header.msgSize > MAX_MSG_SIZE) {
        return false;
    }

    if (recvOffset >= static_cast<ssize_t>(maxBufSize)) {
        return false;
    }

    return ReadBody(fd, recvBuf, recvOffset, header.msgSize);
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
    size_t recvBufSize_ = 0;
    int64_t msgSent_ = 0;
    int64_t msgRecv_ = 0;
    int64_t outstanding_ = 0;
    int64_t errorCount_ = 0;
    bool connected_ = false;
    ssize_t recvOffset_ = 0;
    std::vector<bool> acked_;

    void Cleanup();
    int InitSocket();
    int SetupEpoll();
    int HandleConnect();
    int HandleEpollOut(uint64_t &totalBytesSent, TokenBucket &tokenBucket);
    int HandleEpollIn(uint64_t &totalBytesRecv);
    int HandleEpollError();
    int SendOneMessage(TokenBucket &tokenBucket, uint64_t &totalBytesSent);
    int ReceiveResponses(uint64_t &totalBytesRecv);
    int ProcessRecvBuffer();
    int ProcessEvents(epoll_event *events, int nfds, TokenBucket &tokenBucket, uint64_t &totalBytesSent,
                      uint64_t &totalBytesRecv);
    void PrintReport(uint64_t totalBytesSent, uint64_t totalBytesRecv, uint64_t durationUs);
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

int SubCommandData::DataClient::InitSocket()
{
    fd_ = ubsocket_socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        std::cout << "Error: create socket failed, errno: " << errno << std::endl;
        return -errno;
    }

    if (SetNonBlocking(fd_) < 0) {
        std::cout << "Error: set non-blocking failed, errno: " << errno << std::endl;
        Cleanup();
        return -errno;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(cmd_.port_);
    inet_pton(AF_INET, cmd_.ip_.c_str(), &addr.sin_addr);

    int ret = ubsocket_connect(fd_, AsSockaddr(&addr), sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        std::cout << "Error: connect failed, errno: " << errno << std::endl;
        Cleanup();
        return -errno;
    }

    return 0;
}

int SubCommandData::DataClient::SetupEpoll()
{
    epollFd_ = ubsocket_epoll_create1(0);
    if (epollFd_ < 0) {
        std::cout << "Error: create epoll failed, errno: " << errno << std::endl;
        Cleanup();
        return -errno;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd_;
    if (ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd_, &ev) < 0) {
        std::cout << "Error: epoll_ctl add failed, errno: " << errno << std::endl;
        Cleanup();
        return -errno;
    }

    return 0;
}

int SubCommandData::DataClient::HandleConnect()
{
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
    return 0;
}

int SubCommandData::DataClient::HandleEpollOut(uint64_t &totalBytesSent, TokenBucket &tokenBucket)
{
    if (!connected_) {
        int ret = HandleConnect();
        if (ret < 0) {
            return ret;
        }
    }
    while (msgSent_ < cmd_.msgCount_ && outstanding_ < kWindowSize) {
        if (SendOneMessage(tokenBucket, totalBytesSent) < 0) {
            return -1;
        }
    }
    return 0;
}

int SubCommandData::DataClient::HandleEpollIn(uint64_t &totalBytesRecv)
{
    return ReceiveResponses(totalBytesRecv);
}

int SubCommandData::DataClient::HandleEpollError()
{
    std::cout << "Error: epoll error or hangup" << std::endl;
    return -1;
}

int SubCommandData::DataClient::SendOneMessage(TokenBucket &tokenBucket, uint64_t &totalBytesSent)
{
    if (cmd_.qps_ > 0 && !tokenBucket.TryAcquire()) {
        int64_t waitUs = tokenBucket.WaitTimeUs();
        if (waitUs > 0) {
            usleep(waitUs);
        }
        return 0;
    }

    uint32_t crc = CalculateCRC32(sendBuf_, cmd_.msgSize_);
    uint32_t seq = static_cast<uint32_t>(msgSent_);
    uint32_t msgSize = static_cast<uint32_t>(cmd_.msgSize_);

    ssize_t sent = SendMessage(fd_, seq, crc, msgSize, sendBuf_);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        std::cout << "Error: writev failed, errno: " << errno << std::endl;
        return -errno;
    }

    msgSent_++;
    outstanding_++;
    totalBytesSent += sent;
    return 0;
}

int SubCommandData::DataClient::ReceiveResponses(uint64_t &totalBytesRecv)
{
    while (true) {
        if (recvOffset_ >= static_cast<ssize_t>(recvBufSize_)) {
            std::cout << "Error: client receive buffer overflow" << std::endl;
            return -1;
        }

        struct iovec iov[1];
        iov[0].iov_base = recvBuf_ + recvOffset_;
        iov[0].iov_len = recvBufSize_ - static_cast<size_t>(recvOffset_);

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

        int ret = ProcessRecvBuffer();
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

int SubCommandData::DataClient::ProcessRecvBuffer()
{
    while (recvOffset_ >= static_cast<ssize_t>(HEADER_SIZE)) {
        uint32_t seq = ReadUint32Le(recvBuf_);
        uint32_t recvMsgSize = ReadUint32Le(recvBuf_ + sizeof(uint32_t) * 2);
        if (recvMsgSize > MAX_MSG_SIZE) {
            std::cout << "Error: invalid message size " << recvMsgSize << " from server" << std::endl;
            return -1;
        }

        if (recvMsgSize > static_cast<uint32_t>(cmd_.msgSize_)) {
            std::cout << "Error: received message size " << recvMsgSize << " exceeds expected " << cmd_.msgSize_
                      << std::endl;
            return -1;
        }

        if (recvOffset_ < static_cast<ssize_t>(HEADER_SIZE + recvMsgSize)) {
            break;
        }

        if (seq >= static_cast<uint32_t>(cmd_.msgCount_)) {
            std::cout << "Error: invalid sequence number " << seq << " from server" << std::endl;
            return -1;
        }

        uint32_t recvCrc = ReadUint32Le(recvBuf_ + sizeof(uint32_t));
        uint32_t calcCrc = CalculateCRC32(recvBuf_ + HEADER_SIZE, recvMsgSize);
        if (recvCrc != calcCrc) {
            errorCount_++;
            std::cout << "[CLIENT] Warning: CRC mismatch at message " << seq
                      << ", direction: Server->Client, expected: " << calcCrc << ", got: " << recvCrc << std::endl;
        }

        if (!acked_[seq]) {
            acked_[seq] = true;
            outstanding_--;

            while (msgRecv_ < cmd_.msgCount_ && acked_[msgRecv_]) {
                msgRecv_++;
            }
        }

        size_t moveSize = recvOffset_ - (HEADER_SIZE + recvMsgSize);
        if (moveSize > 0) {
            memmove(recvBuf_, recvBuf_ + HEADER_SIZE + recvMsgSize, moveSize);
        }
        recvOffset_ -= (HEADER_SIZE + recvMsgSize);
    }
    return 0;
}

int SubCommandData::DataClient::ProcessEvents(epoll_event *events, int nfds, TokenBucket &tokenBucket,
                                              uint64_t &totalBytesSent, uint64_t &totalBytesRecv)
{
    for (int i = 0; i < nfds; ++i) {
        if (events[i].data.fd != fd_) {
            continue;
        }

        if (events[i].events & EPOLLOUT) {
            int ret = HandleEpollOut(totalBytesSent, tokenBucket);
            if (ret < 0) {
                return ret;
            }
        }

        if (events[i].events & EPOLLIN) {
            int ret = HandleEpollIn(totalBytesRecv);
            if (ret < 0) {
                return ret;
            }
        }

        if (events[i].events & (EPOLLERR | EPOLLHUP)) {
            return HandleEpollError();
        }
    }
    return 0;
}

void SubCommandData::DataClient::PrintReport(uint64_t totalBytesSent, uint64_t totalBytesRecv, uint64_t durationUs)
{
    double durationMs = durationUs / 1000.0;
    double throughputMbps = (totalBytesSent * 8.0) / (1024 * 1024 * durationMs / 1000);

    std::cout << "\n=== Data Client Report ===" << std::endl;
    std::cout << "Messages sent: " << msgSent_ << std::endl;
    std::cout << "Messages received: " << msgRecv_ << std::endl;
    std::cout << "CRC errors: " << errorCount_ << std::endl;
    std::cout << "Total bytes sent: " << totalBytesSent << std::endl;
    std::cout << "Total bytes received: " << totalBytesRecv << std::endl;
    std::cout << "Duration: " << durationMs << " ms" << std::endl;
    std::cout << "Throughput: " << throughputMbps << " Mbps" << std::endl;
}

int SubCommandData::DataClient::Run()
{
    sendBuf_ = reinterpret_cast<uint8_t *>(malloc(cmd_.msgSize_));
    recvBufSize_ = static_cast<size_t>(kWindowSize) * (static_cast<size_t>(cmd_.msgSize_) + HEADER_SIZE);
    recvBuf_ = reinterpret_cast<uint8_t *>(malloc(recvBufSize_));
    acked_.resize(cmd_.msgCount_, false);
    if (!sendBuf_ || !recvBuf_) {
        std::cout << "Error: malloc failed" << std::endl;
        if (sendBuf_) {
            free(sendBuf_);
            sendBuf_ = nullptr;
        }
        if (recvBuf_) {
            free(recvBuf_);
            recvBuf_ = nullptr;
        }
        return -1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    for (size_t i = 0; i < static_cast<size_t>(cmd_.msgSize_); ++i) {
        sendBuf_[i] = static_cast<uint8_t>(dist(gen) & 0xFF);
    }

    if (InitSocket() < 0) {
        return -1;
    }

    if (SetupEpoll() < 0) {
        return -1;
    }

    const int defaultEpollTimeoutMs = 1000;
    const uint64_t defaultTimeoutMs = 60000;
    TokenBucket tokenBucket(cmd_.qps_, cmd_.qps_ > 0 ? cmd_.qps_ : 0);

    auto timeStart = Func::TimeUs();
    auto lastActivityTime = Func::TimeUs();
    uint64_t totalBytesSent = 0;
    uint64_t totalBytesRecv = 0;

    while (msgRecv_ < cmd_.msgCount_) {
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
            uint64_t now = Func::TimeUs();
            if (now - lastActivityTime > defaultTimeoutMs * 1000) {
                std::cout << "Error: timeout, no activity for " << defaultTimeoutMs << " ms" << std::endl;
                return -1;
            }
            continue;
        }

        int ret = ProcessEvents(events, nfds, tokenBucket, totalBytesSent, totalBytesRecv);
        if (ret < 0) {
            return ret;
        }

        lastActivityTime = Func::TimeUs();
    }

    auto timeEnd = Func::TimeUs();
    PrintReport(totalBytesSent, totalBytesRecv, timeEnd - timeStart);

    return 0;
}

struct ClientState {
    uint8_t *recvBuf = nullptr;
    uint8_t *sendBuf = nullptr;
    int64_t msgRecv = 0;
    int64_t msgSent = 0;
    int64_t errorCount = 0;
    ssize_t recvOffset = 0;
    ssize_t pendingSendSize = 0;
    uint64_t totalBytesRecv = 0;
    uint64_t totalBytesSent = 0;
    uint64_t timeStart = 0;
};

class SubCommandData::DataServer {
public:
    explicit DataServer(SubCommandData &cmd) : cmd_(cmd) {}
    ~DataServer();
    int Run();

private:
    SubCommandData &cmd_;
    int listenFd_ = -1;
    int epollFd_ = -1;
    std::unordered_map<int, golden::ClientState> clients_;

    void Cleanup();
    int InitListener();
    int SetupEpoll();
    int AcceptClient();
    int HandleClientOut(int fd, uint64_t &totalBytesSent);
    int HandleClientIn(int fd, uint64_t &totalBytesRecv, uint64_t &totalBytesSent);
    int HandleClientError(int fd);
    int ProcessRecvData(int fd, uint64_t &totalBytesRecv, uint64_t &totalBytesSent);
    int TrySendPending(int fd, uint64_t &totalBytesSent);
    int TrySendEcho(int fd, uint64_t &totalBytesSent);
    int ProcessEvents(epoll_event *events, int nfds, uint64_t &totalBytesRecv, uint64_t &totalBytesSent);
    void ResetClientState(int fd, uint64_t &totalBytesRecv, uint64_t &totalBytesSent);
};

SubCommandData::DataServer::~DataServer()
{
    Cleanup();
}

void SubCommandData::DataServer::Cleanup()
{
    for (auto &pair : clients_) {
        int fd = pair.first;
        golden::ClientState &state = pair.second;
        ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        if (state.recvBuf) {
            free(state.recvBuf);
        }
        if (state.sendBuf) {
            free(state.sendBuf);
        }
    }
    clients_.clear();

    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

int SubCommandData::DataServer::InitListener()
{
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
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (ubsocket_bind(listenFd_, AsSockaddr(&addr), sizeof(addr)) < 0) {
        std::cout << "Error: bind failed, errno: " << errno << std::endl;
        return -errno;
    }

    if (ubsocket_listen(listenFd_, 128) < 0) {
        std::cout << "Error: listen failed, errno: " << errno << std::endl;
        return -errno;
    }

    return 0;
}

int SubCommandData::DataServer::SetupEpoll()
{
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

    return 0;
}

int SubCommandData::DataServer::AcceptClient()
{
    struct sockaddr_in clientAddr;
    socklen_t len = sizeof(clientAddr);
    int fd = ubsocket_accept(listenFd_, AsSockaddr(&clientAddr), &len);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        std::cout << "Error: accept failed, errno: " << errno << std::endl;
        return -errno;
    }

    if (SetNonBlocking(fd) < 0) {
        std::cout << "Error: set non-blocking failed, errno: " << errno << std::endl;
        close(fd);
        return -errno;
    }

    size_t bufSize = static_cast<size_t>(kWindowSize) * (MAX_MSG_SIZE + HEADER_SIZE);
    uint8_t *recvBuf = reinterpret_cast<uint8_t *>(malloc(bufSize));
    uint8_t *sendBuf = reinterpret_cast<uint8_t *>(malloc(bufSize));
    if (!recvBuf || !sendBuf) {
        std::cout << "Error: malloc failed for client " << fd << std::endl;
        close(fd);
        return -1;
    }

    clients_[fd] = {recvBuf, sendBuf, 0, 0, 0, 0, 0, 0, 0, Func::TimeUs()};

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.fd = fd;
    int epollRet = ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
    if (epollRet < 0) {
        std::cout << "Error: epoll_ctl add client failed, errno: " << errno << std::endl;
        int savedFd = fd;
        free(recvBuf);
        free(sendBuf);
        close(fd);
        fd = -1;
        clients_.erase(savedFd);
        return -errno;
    }

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    std::cout << "Client connected: " << ipStr << ":" << ntohs(clientAddr.sin_port) << std::endl;

    return 1;
}

int SubCommandData::DataServer::HandleClientOut(int fd, uint64_t &totalBytesSent)
{
    return TrySendPending(fd, totalBytesSent);
}

int SubCommandData::DataServer::HandleClientIn(int fd, uint64_t &totalBytesRecv, uint64_t &totalBytesSent)
{
    return ProcessRecvData(fd, totalBytesRecv, totalBytesSent);
}

int SubCommandData::DataServer::HandleClientError(int fd)
{
    std::cout << "Client " << fd << " disconnected (error/hangup)" << std::endl;
    return -1;
}

int SubCommandData::DataServer::TrySendPending(int fd, uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        std::cout << "Error: client " << fd << " not found" << std::endl;
        return -1;
    }

    golden::ClientState &state = it->second;

    while (state.pendingSendSize > 0) {
        struct iovec sendIov[1];
        sendIov[0].iov_base = state.sendBuf;
        sendIov[0].iov_len = state.pendingSendSize;

        ssize_t sent = ubsocket_writev(fd, sendIov, 1);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::cout << "Error: writev failed, errno: " << errno << std::endl;
            return -errno;
        }

        totalBytesSent += sent;
        state.totalBytesSent += sent;
        state.msgSent++;
        state.pendingSendSize -= sent;
    }
    return 0;
}

int SubCommandData::DataServer::TrySendEcho(int fd, uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        std::cout << "Error: client " << fd << " not found" << std::endl;
        return -1;
    }

    golden::ClientState &state = it->second;

    while (state.recvOffset >= static_cast<ssize_t>(HEADER_SIZE)) {
        uint32_t msgSize = ReadUint32Le(state.recvBuf + sizeof(uint32_t) * 2);
        if (msgSize > MAX_MSG_SIZE) {
            std::cout << "Error: message size " << msgSize << " exceeds maximum" << std::endl;
            return -1;
        }

        if (state.recvOffset < static_cast<ssize_t>(HEADER_SIZE + msgSize)) {
            break;
        }

        uint32_t recvCrc = ReadUint32Le(state.recvBuf + sizeof(uint32_t));
        uint32_t calcCrc = CalculateCRC32(state.recvBuf + HEADER_SIZE, msgSize);
        if (recvCrc != calcCrc) {
            state.errorCount++;
            std::cout << "[SERVER] Warning: CRC mismatch at message " << state.msgRecv
                      << ", direction: Client->Server, expected: " << calcCrc << ", got: " << recvCrc << std::endl;
        }

        size_t copySize = HEADER_SIZE + msgSize;
        if (copySize > 0) {
            memcpy(state.sendBuf, state.recvBuf, copySize);
        }
        state.pendingSendSize = copySize;

        int ret = TrySendPending(fd, totalBytesSent);
        if (ret < 0) {
            return ret;
        }

        if (state.pendingSendSize > 0) {
            break;
        }

        state.msgRecv++;

        size_t moveSize = state.recvOffset - (HEADER_SIZE + msgSize);
        if (moveSize > 0) {
            memmove(state.recvBuf, state.recvBuf + HEADER_SIZE + msgSize, moveSize);
        }
        state.recvOffset -= (HEADER_SIZE + msgSize);
    }
    return 0;
}

int SubCommandData::DataServer::ProcessRecvData(int fd, uint64_t &totalBytesRecv, uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        std::cout << "Error: client " << fd << " not found" << std::endl;
        return -1;
    }

    golden::ClientState &state = it->second;
    size_t recvBufSize = static_cast<size_t>(kWindowSize) * (MAX_MSG_SIZE + HEADER_SIZE);
    bool keepReading = true;
    while (keepReading) {
        if (state.recvOffset >= static_cast<ssize_t>(recvBufSize)) {
            std::cout << "Error: recvOffset " << state.recvOffset << " exceeds buffer size " << recvBufSize
                      << std::endl;
            return -1;
        }

        struct iovec iov[1];
        iov[0].iov_base = state.recvBuf + state.recvOffset;
        iov[0].iov_len = recvBufSize - static_cast<size_t>(state.recvOffset);

        ssize_t recvd = ubsocket_readv(fd, iov, 1);
        if (recvd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                keepReading = false;
                continue;
            }
            std::cout << "Error: readv failed, errno: " << errno << std::endl;
            return -errno;
        }
        if (recvd == 0) {
            std::cout << "Client " << fd << " disconnected" << std::endl;
            return -1;
        }

        state.recvOffset += recvd;
        state.totalBytesRecv += recvd;
        totalBytesRecv += recvd;

        int ret = TrySendEcho(fd, totalBytesSent);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

void SubCommandData::DataServer::ResetClientState(int fd, uint64_t &totalBytesRecv, uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    golden::ClientState &state = it->second;
    uint64_t durationUs = Func::TimeUs() - state.timeStart;

    std::cout << "\n=== Data Server Report (client " << fd << ") ===" << std::endl;
    std::cout << "Messages received: " << state.msgRecv << std::endl;
    std::cout << "Messages sent: " << state.msgSent << std::endl;
    std::cout << "CRC errors: " << state.errorCount << std::endl;
    std::cout << "Total bytes received: " << state.totalBytesRecv << std::endl;
    std::cout << "Total bytes sent: " << state.totalBytesSent << std::endl;
    std::cout << "Duration: " << durationUs / 1000.0 << " ms" << std::endl;

    ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);

    if (state.recvBuf) {
        free(state.recvBuf);
    }
    if (state.sendBuf) {
        free(state.sendBuf);
    }

    clients_.erase(it);
}

int SubCommandData::DataServer::ProcessEvents(epoll_event *events, int nfds, uint64_t &totalBytesRecv,
                                              uint64_t &totalBytesSent)
{
    for (int i = 0; i < nfds; ++i) {
        int fd = events[i].data.fd;

        if (fd == listenFd_) {
            int ret = AcceptClient();
            if (ret < 0) {
                return ret;
            }
            continue;
        }

        auto it = clients_.find(fd);
        if (it == clients_.end()) {
            continue;
        }

        bool clientDisconnected = false;

        if (events[i].events & EPOLLOUT) {
            int ret = HandleClientOut(fd, totalBytesSent);
            if (ret < 0) {
                clientDisconnected = true;
            }
        }

        if (!clientDisconnected && (events[i].events & EPOLLIN)) {
            int ret = HandleClientIn(fd, totalBytesRecv, totalBytesSent);
            if (ret < 0) {
                clientDisconnected = true;
            }
        }

        if (!clientDisconnected && (events[i].events & (EPOLLERR | EPOLLHUP))) {
            int ret = HandleClientError(fd);
            if (ret < 0) {
                clientDisconnected = true;
            }
        }

        if (clientDisconnected) {
            ResetClientState(fd, totalBytesRecv, totalBytesSent);
        }
    }
    return 0;
}

int SubCommandData::DataServer::Run()
{
    if (InitListener() < 0) {
        return -1;
    }

    if (SetupEpoll() < 0) {
        return -1;
    }

    std::cout << "Data server listening on " << cmd_.ip_ << ":" << cmd_.port_ << std::endl;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = HandleSignal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    uint64_t totalBytesRecv = 0;
    uint64_t totalBytesSent = 0;

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

        int ret = ProcessEvents(events, nfds, totalBytesRecv, totalBytesSent);
        if (ret < 0) {
            return ret;
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
