/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan
 * PSL v2. You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE. See the
 * Mulan PSL v2 for more details.
 */

/*
 * UB native (one-sided) data-plane data consistency tool.
 *
 * This is the one-sided counterpart of golden_cmd_data.cpp: instead of the
 * byte-stream writev/readv two-sided path it moves data through the adaptive
 * bigdata data-plane APIs ubs_post / ubs_poll (see include/ubsocket_data.h).
 * The bigdata engine routes every segment adaptively — small segments inline as
 * SMALL_DATA SEND (two-sided), large ones pulled by the peer via RDMA READ
 * (one-sided) — so a single tool exercises both.
 *
 * The command line and parameters are identical to the two-sided data command;
 * only the transport set is reduced to `tcp` (plain byte-stream baseline, for
 * comparison without UB hardware) and `ub_rm_ctp` (UB native data plane).
 *
 * Message framing is shared with the two-sided tool so results are comparable:
 *   [seq(4) | crc(4) | msgSize(4) | payload(msgSize)]
 * CRC-32 is computed over the payload only.  The server verifies the CRC of
 * every received message and echoes the exact bytes back; the client verifies
 * the echo and reports any mismatch.
 */
#include "golden_cmd_ub_native_data.h"
#include "golden_crc.h"

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/uio.h>
#include <unistd.h>
#include <algorithm>
#include <csignal>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "iobuf/ubsocket_iobuf.h"
#include "include/ubsocket_data.h"
#include "under_api/dl_umq_api.h"
#include <core/umq/umq_setting.h>

namespace golden {

using namespace ock::ubs;

/* Close a socket fd through the matching protocol lifecycle.  Epoll fds must
 * remain on the native close path. */
static void CloseSocketByProtocol(int fd, bool isUbProtocol)
{
    const int savedErrno = errno;
    if (isUbProtocol) {
        (void)ubsocket_close(fd);
    } else {
        (void)::close(fd);
    }
    errno = savedErrno;
}

/* Locate the Block header that owns a UB page data pointer.  AllocUbBuf places
 * the Block at the page floor, so the floor of any data pointer is the Block. */
static void *PtrFloorToBoundary(void *ptr)
{
    return (void *)((uint64_t)ptr & ~umq::UmqSetting::FloorMask());
}

/* Allocate a single UB page (8K).  The returned pointer is the Block data area,
 * which the bigdata engine treats as a UMQ-registered zero-copy source: small
 * segments are SEND with buf_data repointed at it (umq_data_to_head recovers
 * the owning qbuf), large segments are READ_OFFER-pinned.  Caller holds the
 * initial Block ref and must release it with FreeUbBuf once ubs_post has taken
 * over (IncRef / pin) the accepted segments. */
static void *AllocUbBuf(size_t dataSize)
{
    size_t pageSize = umq::UmqSetting::GetIOBufSize() + IOBUF_DIFF;
    (void)dataSize;  // dataSize is validated by caller; we always allocate a full page
    ubs_iobuf_alloc_option_t option = {};
    option.flag = UBS_IOBUF_ALLOC_FLAG_POOL_TYPE;
    option.pool_type = UBS_IOBUF_POOL_NORMAL;
    void *raw = ubsocket_iobuf_allocate(pageSize, &option);
    if (raw == nullptr) {
        return nullptr;
    }
    Block *block = new (raw)
        Block(reinterpret_cast<char *>(raw) + sizeof(Block), static_cast<uint32_t>(pageSize - sizeof(Block)));
    return block->data;
}

static void FreeUbBuf(void *data)
{
    if (data == nullptr) {
        return;
    }
    Block *block = reinterpret_cast<Block *>(PtrFloorToBoundary(data));
    block->DecRef();
}

/*
 * Post a full message over the UB native data plane (ubs_post).  The message is
 * split into page-sized segments, each backed by a UB-allocated Block; the
 * adaptive bigdata engine routes small segments inline (SMALL_DATA SEND) and
 * large ones through READ_OFFER / RDMA READ.
 *
 * ubs_post accepts a contiguous segment prefix (all-or-nothing on the happy
 * path) and returns the accepted segment count, or -1/EAGAIN when flow control
 * or SQ pressure rejects the batch.  This helper re-posts only the rejected
 * tail after a short back-off, and releases golden's own Block refs as soon as
 * the engine takes over each accepted segment — mirroring the writev staging
 * pattern in the two-sided SendDataUb.
 *
 * Returns 0 on complete send, -1 on fatal error (errno set).
 */
static int PostUbMessage(int fd, const uint8_t *msgBuf, size_t totalLen)
{
    const size_t chunkSize = umq::UmqSetting::GetIOBufSize();
    const size_t numChunks = (totalLen + chunkSize - 1) / chunkSize;

    std::vector<void *> blocks;
    std::vector<ubs_segment_t> segs;
    blocks.reserve(numChunks);
    segs.reserve(numChunks);

    for (size_t i = 0; i < numChunks; ++i) {
        size_t thisChunk = std::min(chunkSize, totalLen - i * chunkSize);
        void *ubPage = AllocUbBuf(thisChunk);
        if (ubPage == nullptr) {
            for (size_t j = 0; j < blocks.size(); ++j) {
                FreeUbBuf(blocks[j]);
            }
            errno = ENOMEM;
            return -1;
        }
        memcpy(ubPage, msgBuf + i * chunkSize, thisChunk);
        blocks.push_back(ubPage);

        ubs_segment_t seg;
        seg.block = PtrFloorToBoundary(ubPage);
        seg.offset = 0;
        seg.len = static_cast<uint32_t>(thisChunk);
        seg.start_pos = ubPage;
        seg.user_ctx = 0;
        segs.push_back(seg);
    }

    size_t head = 0;
    while (head < numChunks) {
        ubs_data_list_t list;
        list.segments = segs.data() + head;
        list.nsegs = static_cast<uint16_t>(numChunks - head);

        ssize_t accepted = ubs_post(fd, &list);
        if (accepted < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                usleep(100);  // brief back-off while TX credits / SQ slots free up
                continue;
            }
            /* Fatal — release golden's refs on the not-yet-accepted blocks. */
            for (size_t i = head; i < numChunks; ++i) {
                FreeUbBuf(blocks[i]);
            }
            return -1;
        }

        /* Accepted segments are now co-owned by the engine (IncRef / pin);
         * release golden's own refs so the blocks are reclaimed on TX CQE. */
        for (size_t i = 0; i < static_cast<size_t>(accepted); ++i) {
            FreeUbBuf(blocks[head + i]);
        }
        head += static_cast<size_t>(accepted);
        if (head < numChunks) {
            usleep(100);  // partial accept — let the SQ drain before re-posting the tail
        }
    }
    return 0;
}

constexpr int MAX_EVENTS = 16;
constexpr size_t HEADER_SIZE = sizeof(uint32_t) * 3;
constexpr int64_t DEFAULT_MSG_COUNT = 100;
constexpr int64_t DEFAULT_MSG_SIZE = 1024;
constexpr size_t MAX_MSG_SIZE = 1 * 1024 * 1024;
constexpr int64_t MICROSECONDS_PER_SECOND = 1000000LL;
constexpr int MIN_PORT = 10000;
constexpr int MAX_PORT = 65535;
constexpr int64_t MAX_MSG_COUNT = 100000;
constexpr int64_t MAX_QPS = 100000;
/* Max segments one ubs_poll batch returns (bRPC design §12.4 scratch buffer). */
constexpr uint16_t kUbNativePollMaxSegs = 64;

static uint64_t MonotonicTimeUs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * static_cast<uint64_t>(MICROSECONDS_PER_SECOND) +
           static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

static uint64_t CalculatePercentile(const std::vector<uint64_t> &sortedSamples, double percentile)
{
    size_t index = static_cast<size_t>(sortedSamples.size() * percentile);
    if (index >= sortedSamples.size()) {
        index = sortedSamples.size() - 1;
    }
    return sortedSamples[index];
}

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

void SubCommandUbNativeData::SetRules() noexcept
{
    param_rules_[PARAM_ROLE] = {PARAM_ROLE, PDT_STR_ENUM, true, "", "client|server", ""};
    param_rules_[PARAM_PROTOCOL] = {PARAM_PROTOCOL, PDT_STR_ENUM, true, "tcp", "tcp|ub_rm_ctp", ""};
    param_rules_[PARAM_IP] = {PARAM_IP, PDT_STR, false, "", "", "Server IP address (client only)"};
    param_rules_[PARAM_PORT] = {PARAM_PORT, PDT_INT64, true, 10001L, 10000, 65535, ""};

    param_rules_["msg-count"] = {
        "msg-count", PDT_INT64, false, DEFAULT_MSG_COUNT, 1, MAX_MSG_COUNT, "number of messages to send (client only)"};
    param_rules_["msg-size"] = {
        "msg-size", PDT_INT64, false, DEFAULT_MSG_SIZE, 1, MAX_MSG_SIZE, "size of each message in bytes (client only)"};
    param_rules_["qps"] = {
        "qps", PDT_INT64, false, 0, 0, MAX_QPS, "QPS limit for sending messages (0=unlimited, client only)"};

    example_.push_back("server: " + program + " " + name_ + " --" + PARAM_ROLE + "=server --" + PARAM_PROTOCOL +
                       "=ub_rm_ctp --" + PARAM_PORT + "=10001");
    example_.push_back("client: " + program + " " + name_ + " --" + PARAM_ROLE + "=client --" + PARAM_PROTOCOL +
                       "=ub_rm_ctp --" + PARAM_IP + "=127.0.0.1 --" + PARAM_PORT + "=10001" +
                       " --msg-count=100 --msg-size=1024 --qps=1000");
}

int SubCommandUbNativeData::DoInitialize() noexcept
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

int SubCommandUbNativeData::ValidateCommonParams() noexcept
{
    if (role_ != "client" && role_ != "server") {
        std::cout << "Error: Invalid role '" << role_ << "', must be 'client' or 'server'" << std::endl;
        return -1;
    }

    if (protocol_ != "tcp" && protocol_ != "ub_rm_ctp") {
        std::cout << "Error: Invalid protocol '" << protocol_ << "', must be 'tcp' or 'ub_rm_ctp'" << std::endl;
        return -1;
    }

    if (port_ < MIN_PORT || port_ > MAX_PORT) {
        std::cout << "Error: Invalid port " << port_ << ", must be between " << MIN_PORT << " and " << MAX_PORT
                  << std::endl;
        return -1;
    }

    return 0;
}

int SubCommandUbNativeData::ValidateClientParams() noexcept
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

int SubCommandUbNativeData::ValidateServerParams() noexcept
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

class SubCommandUbNativeData::UbNativeDataClient {
public:
    explicit UbNativeDataClient(SubCommandUbNativeData &cmd) : cmd_(cmd) {}
    ~UbNativeDataClient();
    int Run();

private:
    SubCommandUbNativeData &cmd_;
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
    size_t sendOffset_ = 0;  // tracks partial TCP writev progress for the current message
    std::vector<bool> acked_;
    std::vector<uint64_t> sendStartTimes_;
    std::vector<uint64_t> rttSamples_;

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

SubCommandUbNativeData::UbNativeDataClient::~UbNativeDataClient()
{
    Cleanup();
}

void SubCommandUbNativeData::UbNativeDataClient::Cleanup()
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
        if (cmd_.IsUbProtocol()) {
            ubsocket_shutdown(fd_, SHUT_RDWR);
        }
        CloseSocketByProtocol(fd_, cmd_.IsUbProtocol());
        fd_ = -1;
    }
}

int SubCommandUbNativeData::UbNativeDataClient::InitSocket()
{
    if (cmd_.IsUbProtocol()) {
        fd_ = ubsocket_socket(AF_SMC, SOCK_STREAM, 0);
    } else {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    }
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

    int ret = cmd_.IsUbProtocol() ? ubsocket_connect(fd_, AsSockaddr(&addr), sizeof(addr))
                                  : ::connect(fd_, AsSockaddr(&addr), sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        std::cout << "Error: connect failed, errno: " << errno << std::endl;
        Cleanup();
        return -errno;
    }

    return 0;
}

int SubCommandUbNativeData::UbNativeDataClient::SetupEpoll()
{
    if (cmd_.IsUbProtocol()) {
        epollFd_ = ubsocket_epoll_create1(0);
    } else {
        epollFd_ = ::epoll_create1(0);
    }
    if (epollFd_ < 0) {
        std::cout << "Error: create epoll failed, errno: " << errno << std::endl;
        Cleanup();
        return -errno;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd_;
    if (cmd_.IsUbProtocol() ? ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd_, &ev) < 0
                            : ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd_, &ev) < 0) {
        std::cout << "Error: epoll_ctl add failed, errno: " << errno << std::endl;
        Cleanup();
        return -errno;
    }

    return 0;
}

int SubCommandUbNativeData::UbNativeDataClient::HandleConnect()
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

int SubCommandUbNativeData::UbNativeDataClient::HandleEpollOut(uint64_t &totalBytesSent, TokenBucket &tokenBucket)
{
    if (!connected_) {
        int ret = HandleConnect();
        if (ret < 0) {
            return ret;
        }
    }
    while (msgSent_ < cmd_.msgCount_ && outstanding_ < kUbNativeWindowSize) {
        int64_t before = msgSent_;
        int ret = SendOneMessage(tokenBucket, totalBytesSent);
        if (ret < 0) {
            return -1;
        }
        if (msgSent_ == before) {
            // No message was sent (QPS-limited); stop and wait for next event
            break;
        }

        usleep(10);
    }
    return 0;
}

int SubCommandUbNativeData::UbNativeDataClient::HandleEpollIn(uint64_t &totalBytesRecv)
{
    return ReceiveResponses(totalBytesRecv);
}

int SubCommandUbNativeData::UbNativeDataClient::HandleEpollError()
{
    std::cout << "Error: epoll error or hangup" << std::endl;
    return -1;
}

int SubCommandUbNativeData::UbNativeDataClient::SendOneMessage(TokenBucket &tokenBucket, uint64_t &totalBytesSent)
{
    if (cmd_.qps_ > 0 && !tokenBucket.TryAcquire()) {
        int64_t waitUs = tokenBucket.WaitTimeUs();
        if (waitUs > 0) {
            usleep(waitUs);
        }
        return 0;
    }

    uint32_t seq = static_cast<uint32_t>(msgSent_);
    if (seq < sendStartTimes_.size() && sendStartTimes_[seq] == 0) {
        sendStartTimes_[seq] = MonotonicTimeUs();
    }

    uint32_t crc = CalculateCRC32(sendBuf_ + HEADER_SIZE, static_cast<size_t>(cmd_.msgSize_));
    uint32_t msgSize = static_cast<uint32_t>(cmd_.msgSize_);

    // Pack header at the beginning of sendBuf_ (idempotent — safe to call on each retry)
    memcpy(sendBuf_, &seq, sizeof(uint32_t));
    memcpy(sendBuf_ + sizeof(uint32_t), &crc, sizeof(uint32_t));
    memcpy(sendBuf_ + sizeof(uint32_t) * 2, &msgSize, sizeof(uint32_t));

    size_t totalLen = HEADER_SIZE + static_cast<size_t>(msgSize);
    if (cmd_.IsUbProtocol()) {
        // One-sided path: ubs_post the whole message (segments built internally).
        int ret = PostUbMessage(fd_, sendBuf_, totalLen);
        if (ret < 0) {
            std::cout << "Error: ubs_post failed, errno: " << errno << std::endl;
            return -errno;
        }
        totalBytesSent += totalLen;
        msgSent_++;
        outstanding_++;
    } else {
        // TCP stream socket: writev may return a partial count for large messages.
        // Loop until all data is written, tracking progress in sendOffset_.
        // If the send buffer fills up (EAGAIN), return so the epoll loop can wait
        // for the next EPOLLOUT, then resume from sendOffset_.
        while (sendOffset_ < totalLen) {
            struct iovec iov[1];
            iov[0].iov_base = sendBuf_ + sendOffset_;
            iov[0].iov_len = totalLen - sendOffset_;
            ssize_t n = ::writev(fd_, iov, 1);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    return 0;  // partial send — resume on next EPOLLOUT
                }
                std::cout << "Error: writev failed, errno: " << errno << std::endl;
                sendOffset_ = 0;
                return -errno;
            }
            sendOffset_ += static_cast<size_t>(n);
            totalBytesSent += static_cast<uint64_t>(n);
        }
        // Full message sent
        msgSent_++;
        outstanding_++;
        sendOffset_ = 0;
    }
    return 0;
}

int SubCommandUbNativeData::UbNativeDataClient::ReceiveResponses(uint64_t &totalBytesRecv)
{
    while (true) {
        if (cmd_.IsUbProtocol()) {
            // One-sided path: ubs_poll returns SN-ordered completed segments.
            // Copy each segment's payload into the flat recvBuf_, then release
            // the umq qbuf backing it.  The byte stream is identical to TCP, so
            // ProcessRecvBuffer parses messages uniformly.
            ubs_segment_t segments[kUbNativePollMaxSegs];
            ubs_data_list_t out;
            out.segments = segments;
            out.nsegs = kUbNativePollMaxSegs;

            int rc = ubs_poll(fd_, &out);
            if (rc == 0) {
                std::cout << "[CLIENT] Error: connection closed by peer" << std::endl;
                return -1;
            }
            if (rc < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    break;
                }
                std::cout << "Error: ubs_poll failed, errno: " << errno << std::endl;
                return -errno;
            }

            for (uint16_t i = 0; i < out.nsegs; ++i) {
                const ubs_segment_t &seg = out.segments[i];
                if (seg.len == 0 || seg.start_pos == nullptr) {
                    continue;
                }
                if (recvOffset_ + static_cast<ssize_t>(seg.len) > static_cast<ssize_t>(recvBufSize_)) {
                    std::cout << "Error: client receive buffer overflow" << std::endl;
                    ubsocket_iobuf_deallocate(seg.block);
                    return -1;
                }
                memcpy(recvBuf_ + recvOffset_, seg.start_pos, seg.len);
                recvOffset_ += static_cast<ssize_t>(seg.len);
                totalBytesRecv += seg.len;
                UmqApi::umq_buf_free(reinterpret_cast<umq_buf_t *>(seg.block));
            }

            int ret = ProcessRecvBuffer();
            if (ret < 0) {
                return ret;
            }
        } else {
            if (recvOffset_ >= static_cast<ssize_t>(recvBufSize_)) {
                std::cout << "Error: client receive buffer overflow" << std::endl;
                return -1;
            }

            struct iovec iov[1];
            iov[0].iov_base = recvBuf_ + recvOffset_;
            iov[0].iov_len = recvBufSize_ - static_cast<size_t>(recvOffset_);

            ssize_t recvd = ::readv(fd_, iov, 1);
            if (recvd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
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
            totalBytesRecv += static_cast<uint64_t>(recvd);

            int ret = ProcessRecvBuffer();
            if (ret < 0) {
                return ret;
            }
        }
    }
    return 0;
}

int SubCommandUbNativeData::UbNativeDataClient::ProcessRecvBuffer()
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
        bool crcValid = recvCrc == calcCrc;
        if (!crcValid) {
            errorCount_++;
            std::cout << "[CLIENT] Warning: CRC mismatch at message " << seq
                      << ", direction: Server->Client, expected: " << calcCrc << ", got: " << recvCrc << std::endl;
        }

        bool firstEcho = !acked_[seq];
        if (firstEcho && crcValid && sendStartTimes_[seq] != 0) {
            uint64_t receiveTime = MonotonicTimeUs();
            if (receiveTime >= sendStartTimes_[seq]) {
                rttSamples_.push_back(receiveTime - sendStartTimes_[seq]);
            }
        }

        if (firstEcho) {
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

int SubCommandUbNativeData::UbNativeDataClient::ProcessEvents(epoll_event *events, int nfds, TokenBucket &tokenBucket,
                                                             uint64_t &totalBytesSent, uint64_t &totalBytesRecv)
{
    for (int i = 0; i < nfds; ++i) {
        if (events[i].data.fd != fd_) {
            continue;
        }

        // Check for disconnect / error signals BEFORE attempting to write, so we don't
        // corrupt UMQ state by writing to a peer that has already closed the connection.
        if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
            return HandleEpollError();
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

        // Edge-triggered epoll: EPOLLOUT may have been processed above while the window was full.
        // After EPOLLIN acks freed window slots, we must try sending again — the socket has been
        // writable all along, so no new EPOLLOUT transition will fire.
        if (msgSent_ < cmd_.msgCount_ && outstanding_ < kUbNativeWindowSize) {
            int ret = HandleEpollOut(totalBytesSent, tokenBucket);
            if (ret < 0) {
                return ret;
            }
        }
    }
    return 0;
}

void SubCommandUbNativeData::UbNativeDataClient::PrintReport(uint64_t totalBytesSent, uint64_t totalBytesRecv,
                                                             uint64_t durationUs)
{
    double durationMs = durationUs / 1000.0;
    double throughputMbps = (totalBytesSent * 8.0) / (1024 * 1024 * durationMs / 1000);

    std::cout << "\n=== UB Native Data Client Report ===" << std::endl;
    std::cout << "Messages sent: " << msgSent_ << std::endl;
    std::cout << "Messages received: " << msgRecv_ << std::endl;
    std::cout << "CRC errors: " << errorCount_ << std::endl;
    std::cout << "Total bytes sent: " << totalBytesSent << std::endl;
    std::cout << "Total bytes received: " << totalBytesRecv << std::endl;
    std::cout << "Duration: " << durationMs << " ms" << std::endl;
    std::cout << "Throughput: " << throughputMbps << " Mbps" << std::endl;

    if (rttSamples_.empty()) {
        std::cout << "Avg-Latency: N/A" << std::endl;
        std::cout << "Min-Latency: N/A" << std::endl;
        std::cout << "50th-Latency: N/A" << std::endl;
        std::cout << "90th-Latency: N/A" << std::endl;
        std::cout << "99th-Latency: N/A" << std::endl;
        std::cout << "99.9th-Latency: N/A" << std::endl;
        std::cout << "99.99th-Latency: N/A" << std::endl;
        std::cout << "Max-Latency: N/A" << std::endl;
        return;
    }

    uint64_t totalRttUs = 0;
    for (uint64_t rttUs : rttSamples_) {
        totalRttUs += rttUs;
    }

    std::sort(rttSamples_.begin(), rttSamples_.end());
    uint64_t avgRttUs = totalRttUs / rttSamples_.size();
    std::cout << "Avg-Latency: " << avgRttUs << "us" << std::endl;
    std::cout << "Min-Latency: " << rttSamples_.front() << "us" << std::endl;
    std::cout << "50th-Latency: " << CalculatePercentile(rttSamples_, 0.5) << "us" << std::endl;
    std::cout << "90th-Latency: " << CalculatePercentile(rttSamples_, 0.9) << "us" << std::endl;
    std::cout << "99th-Latency: " << CalculatePercentile(rttSamples_, 0.99) << "us" << std::endl;
    std::cout << "99.9th-Latency: " << CalculatePercentile(rttSamples_, 0.999) << "us" << std::endl;
    std::cout << "99.99th-Latency: " << CalculatePercentile(rttSamples_, 0.9999) << "us" << std::endl;
    std::cout << "Max-Latency: " << rttSamples_.back() << "us" << std::endl;
}

int SubCommandUbNativeData::UbNativeDataClient::Run()
{
    // Create socket and connect first — required to initialize UMQ before buffer allocation
    if (InitSocket() < 0) {
        return -1;
    }

    if (SetupEpoll() < 0) {
        return -1;
    }

    // recvBuf_ is a malloc'd flat buffer for the sliding window; both transports
    // (TCP readv and UB ubs_poll) land their bytes here for uniform parsing.
    recvBufSize_ = static_cast<size_t>(kUbNativeWindowSize) * (static_cast<size_t>(cmd_.msgSize_) + HEADER_SIZE);
    recvBuf_ = reinterpret_cast<uint8_t *>(malloc(recvBufSize_));

    size_t sendDataSize = HEADER_SIZE + static_cast<size_t>(cmd_.msgSize_);
    sendBuf_ = reinterpret_cast<uint8_t *>(malloc(sendDataSize));

    acked_.resize(cmd_.msgCount_, false);
    sendStartTimes_.assign(static_cast<size_t>(cmd_.msgCount_), 0);
    rttSamples_.clear();
    rttSamples_.reserve(static_cast<size_t>(cmd_.msgCount_));
    if (!sendBuf_ || !recvBuf_) {
        std::cout << "Error: failed to allocate buffer" << std::endl;
        if (sendBuf_) {
            free(sendBuf_);
            sendBuf_ = nullptr;
        }
        if (recvBuf_) {
            free(recvBuf_);
            recvBuf_ = nullptr;
        }
        Cleanup();
        return -1;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    for (size_t i = 0; i < static_cast<size_t>(cmd_.msgSize_); ++i) {
        sendBuf_[HEADER_SIZE + i] = static_cast<uint8_t>(dist(gen) & 0xFF);
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
        int nfds = cmd_.IsUbProtocol() ? ubsocket_epoll_wait(epollFd_, events, MAX_EVENTS, defaultEpollTimeoutMs)
                                        : ::epoll_wait(epollFd_, events, MAX_EVENTS, defaultEpollTimeoutMs);
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

struct UbNativeClientState {
    uint8_t *recvBuf = nullptr;
    uint8_t *sendBuf = nullptr;
    int64_t msgRecv = 0;
    int64_t msgSent = 0;
    int64_t errorCount = 0;
    ssize_t recvOffset = 0;
    ssize_t pendingSendSize = 0;
    ssize_t pendingSendOffset = 0;  // bytes already sent for the current pending TCP message
    uint64_t totalBytesRecv = 0;
    uint64_t totalBytesSent = 0;
    uint64_t timeStart = 0;
};

class SubCommandUbNativeData::UbNativeDataServer {
public:
    explicit UbNativeDataServer(SubCommandUbNativeData &cmd) : cmd_(cmd) {}
    ~UbNativeDataServer();
    int Run();

private:
    SubCommandUbNativeData &cmd_;
    int listenFd_ = -1;
    int epollFd_ = -1;
    std::unordered_map<int, golden::UbNativeClientState> clients_;

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
    int ProcessEvents(epoll_event *events, int nfds, uint64_t &totalBytesRecv,
                      uint64_t &totalBytesSent);
    void ResetClientState(int fd, uint64_t &totalBytesRecv, uint64_t &totalBytesSent);
};

SubCommandUbNativeData::UbNativeDataServer::~UbNativeDataServer()
{
    Cleanup();
}

void SubCommandUbNativeData::UbNativeDataServer::Cleanup()
{
    for (auto &pair : clients_) {
        int fd = pair.first;
        golden::UbNativeClientState &state = pair.second;
        if (cmd_.IsUbProtocol()) {
            ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
            ubsocket_shutdown(fd, SHUT_RDWR);
        } else {
            ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
        }
        CloseSocketByProtocol(fd, cmd_.IsUbProtocol());
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
        CloseSocketByProtocol(listenFd_, cmd_.IsUbProtocol());
        listenFd_ = -1;
    }
}

int SubCommandUbNativeData::UbNativeDataServer::InitListener()
{
    if (cmd_.IsUbProtocol()) {
        listenFd_ = ubsocket_socket(AF_SMC, SOCK_STREAM, 0);
    } else {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    }
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

    if (cmd_.IsUbProtocol() ? ubsocket_bind(listenFd_, AsSockaddr(&addr), sizeof(addr)) < 0
                            : ::bind(listenFd_, AsSockaddr(&addr), sizeof(addr)) < 0) {
        std::cout << "Error: bind failed, errno: " << errno << std::endl;
        return -errno;
    }

    if (cmd_.IsUbProtocol() ? ubsocket_listen(listenFd_, 128) < 0
                            : ::listen(listenFd_, 128) < 0) {
        std::cout << "Error: listen failed, errno: " << errno << std::endl;
        return -errno;
    }

    return 0;
}

int SubCommandUbNativeData::UbNativeDataServer::SetupEpoll()
{
    if (cmd_.IsUbProtocol()) {
        epollFd_ = ubsocket_epoll_create1(0);
    } else {
        epollFd_ = ::epoll_create1(0);
    }
    if (epollFd_ < 0) {
        std::cout << "Error: create epoll failed, errno: " << errno << std::endl;
        return -errno;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;
    if (cmd_.IsUbProtocol() ? ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev) < 0
                            : ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev) < 0) {
        std::cout << "Error: epoll_ctl add listen failed, errno: " << errno << std::endl;
        return -errno;
    }

    return 0;
}

int SubCommandUbNativeData::UbNativeDataServer::AcceptClient()
{
    struct sockaddr_in clientAddr;
    socklen_t len = sizeof(clientAddr);
    int fd = cmd_.IsUbProtocol() ? ubsocket_accept(listenFd_, AsSockaddr(&clientAddr), &len)
                                 : ::accept(listenFd_, AsSockaddr(&clientAddr), &len);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        std::cout << "Error: accept failed, errno: " << errno << std::endl;
        return -errno;
    }

    if (SetNonBlocking(fd) < 0) {
        std::cout << "Error: set non-blocking failed, errno: " << errno << std::endl;
        CloseSocketByProtocol(fd, cmd_.IsUbProtocol());
        return -errno;
    }

    size_t bufSize = static_cast<size_t>(kUbNativeWindowSize) * (MAX_MSG_SIZE + HEADER_SIZE);
    uint8_t *recvBuf = reinterpret_cast<uint8_t *>(malloc(bufSize));
    uint8_t *sendBuf = reinterpret_cast<uint8_t *>(malloc(bufSize));
    if (!recvBuf || !sendBuf) {
        std::cout << "Error: failed to allocate buffer for client " << fd << std::endl;
        if (recvBuf) {
            free(recvBuf);
        }
        if (sendBuf) {
            free(sendBuf);
        }
        CloseSocketByProtocol(fd, cmd_.IsUbProtocol());
        return -1;
    }

    clients_[fd] = {recvBuf, sendBuf, 0, 0, 0, 0, 0, 0, 0, 0, Func::TimeUs()};

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ev.data.fd = fd;
    int epollRet = cmd_.IsUbProtocol() ? ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev)
                                        : ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
    if (epollRet < 0) {
        std::cout << "Error: epoll_ctl add client failed, errno: " << errno << std::endl;
        free(recvBuf);
        free(sendBuf);
        CloseSocketByProtocol(fd, cmd_.IsUbProtocol());
        clients_.erase(fd);
        return -errno;
    }

    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    std::cout << "Client connected: " << ipStr << ":" << ntohs(clientAddr.sin_port) << std::endl;

    return 1;
}

int SubCommandUbNativeData::UbNativeDataServer::HandleClientOut(int fd, uint64_t &totalBytesSent)
{
    return TrySendPending(fd, totalBytesSent);
}

int SubCommandUbNativeData::UbNativeDataServer::HandleClientIn(int fd, uint64_t &totalBytesRecv,
                                                               uint64_t &totalBytesSent)
{
    return ProcessRecvData(fd, totalBytesRecv, totalBytesSent);
}

int SubCommandUbNativeData::UbNativeDataServer::HandleClientError(int fd)
{
    std::cout << "Client " << fd << " disconnected (error/hangup/close)" << std::endl;
    return -1;
}

int SubCommandUbNativeData::UbNativeDataServer::TrySendPending(int fd, uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        std::cout << "Error: client " << fd << " not found" << std::endl;
        return -1;
    }

    golden::UbNativeClientState &state = it->second;

    if (state.pendingSendSize <= 0) {
        return 0;
    }

    // TCP stream socket: writev may return a partial count for large messages.
    // Use pendingSendOffset to track progress so we don't re-send old data on retry.
    while (state.pendingSendSize > 0) {
        struct iovec sendIov[1];
        sendIov[0].iov_base = state.sendBuf + state.pendingSendOffset;
        sendIov[0].iov_len = static_cast<size_t>(state.pendingSendSize);
        ssize_t n = ::writev(fd, sendIov, 1);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return 0;  // resume on next EPOLLOUT
            }
            std::cout << "Error: writev failed, errno: " << errno << std::endl;
            return -errno;
        }
        totalBytesSent += static_cast<uint64_t>(n);
        state.totalBytesSent += static_cast<uint64_t>(n);
        state.pendingSendOffset += n;
        state.pendingSendSize -= n;
    }
    // Full message sent
    state.msgSent++;
    state.pendingSendOffset = 0;
    return 0;
}

int SubCommandUbNativeData::UbNativeDataServer::TrySendEcho(int fd, uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        std::cout << "Error: client " << fd << " not found" << std::endl;
        return -1;
    }

    golden::UbNativeClientState &state = it->second;

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
        if (cmd_.IsUbProtocol()) {
            // One-sided path: echo via ubs_post.  PostUbMessage retries on
            // EAGAIN internally and fully drains the message before returning,
            // so no pending state is needed here.
            int ret = PostUbMessage(fd, state.recvBuf, copySize);
            if (ret < 0) {
                std::cout << "Error: ubs_post echo failed, errno: " << errno << std::endl;
                return -errno;
            }
            totalBytesSent += copySize;
            state.totalBytesSent += copySize;
            state.msgSent++;
        } else {
            // TCP stream socket: echo through the pending-send path.  If a
            // previous message is still draining its pending copy in sendBuf,
            // finish that first — overwriting sendBuf here would corrupt the
            // in-flight echo.  Once drained, queue the current front message
            // below.
            if (state.pendingSendSize > 0) {
                int ret = TrySendPending(fd, totalBytesSent);
                if (ret < 0) {
                    return ret;
                }
                if (state.pendingSendSize > 0) {
                    break;  // still sending — wait for the next EPOLLOUT
                }
            }

            if (copySize > 0) {
                memcpy(state.sendBuf, state.recvBuf, copySize);
            }
            state.pendingSendSize = copySize;
            state.pendingSendOffset = 0;  // new message, start from the beginning

            int ret = TrySendPending(fd, totalBytesSent);
            if (ret < 0) {
                return ret;
            }
        }

        // The message has been fully received (parsed + CRC verified) and its
        // echo queued.  Account it now and consume it from recvBuf — a large
        // TCP echo may still be draining across multiple EPOLLOUT rounds via
        // sendBuf/pendingSendSize.  Deferring msgRecv++ until the pending
        // fully drains would leave the counter stuck when the peer disconnects
        // mid-echo (and would also re-echo the message on the next EPOLLIN).
        state.msgRecv++;

        size_t moveSize = state.recvOffset - (HEADER_SIZE + msgSize);
        if (moveSize > 0) {
            memmove(state.recvBuf, state.recvBuf + HEADER_SIZE + msgSize, moveSize);
        }
        state.recvOffset -= (HEADER_SIZE + msgSize);
    }
    return 0;
}

int SubCommandUbNativeData::UbNativeDataServer::ProcessRecvData(int fd, uint64_t &totalBytesRecv,
                                                                uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        std::cout << "Error: client " << fd << " not found" << std::endl;
        return -1;
    }

    golden::UbNativeClientState &state = it->second;
    size_t recvBufSize = static_cast<size_t>(kUbNativeWindowSize) * (MAX_MSG_SIZE + HEADER_SIZE);
    while (true) {
        if (state.recvOffset >= static_cast<ssize_t>(recvBufSize)) {
            std::cout << "Error: recvOffset " << state.recvOffset << " exceeds buffer size " << recvBufSize
                      << std::endl;
            return -1;
        }

        if (cmd_.IsUbProtocol()) {
            // One-sided path: ubs_poll returns SN-ordered completed segments.
            // Copy each segment's payload into the flat recvBuf_, then release the
            // umq qbuf backing it.  The byte stream is identical to TCP, so
            // TrySendEcho parses and echoes messages uniformly.
            ubs_segment_t segments[kUbNativePollMaxSegs];
            ubs_data_list_t out;
            out.segments = segments;
            out.nsegs = kUbNativePollMaxSegs;

            int rc = ubs_poll(fd, &out);
            if (rc == 0) {
                std::cout << "Client " << fd << " disconnected" << std::endl;
                return -1;
            }
            if (rc < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    break;
                }
                std::cout << "Error: ubs_poll failed, errno: " << errno << std::endl;
                return -errno;
            }

            for (uint16_t i = 0; i < out.nsegs; ++i) {
                const ubs_segment_t &seg = out.segments[i];
                if (seg.len == 0 || seg.start_pos == nullptr) {
                    continue;
                }
                if (state.recvOffset + static_cast<ssize_t>(seg.len) > static_cast<ssize_t>(recvBufSize)) {
                    std::cout << "Error: server receive buffer overflow" << std::endl;
                    ubsocket_iobuf_deallocate(seg.block);
                    return -1;
                }
                memcpy(state.recvBuf + state.recvOffset, seg.start_pos, seg.len);
                state.recvOffset += static_cast<ssize_t>(seg.len);
                state.totalBytesRecv += seg.len;
                totalBytesRecv += seg.len;
                UmqApi::umq_buf_free(reinterpret_cast<umq_buf_t *>(seg.block));
            }

            int ret = TrySendEcho(fd, totalBytesSent);
            if (ret < 0) {
                return ret;
            }
        } else {
            struct iovec iov[1];
            iov[0].iov_base = state.recvBuf + state.recvOffset;
            iov[0].iov_len = recvBufSize - static_cast<size_t>(state.recvOffset);

            ssize_t recvd = ::readv(fd, iov, 1);
            if (recvd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    break;
                }
                std::cout << "Error: readv failed, errno: " << errno << std::endl;
                return -errno;
            }
            if (recvd == 0) {
                std::cout << "Client " << fd << " disconnected" << std::endl;
                return -1;
            }

            state.recvOffset += recvd;
            state.totalBytesRecv += static_cast<uint64_t>(recvd);
            totalBytesRecv += static_cast<uint64_t>(recvd);

            int ret = TrySendEcho(fd, totalBytesSent);
            if (ret < 0) {
                return ret;
            }
        }
    }
    return 0;
}

void SubCommandUbNativeData::UbNativeDataServer::ResetClientState(int fd, uint64_t &totalBytesRecv,
                                                                  uint64_t &totalBytesSent)
{
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    golden::UbNativeClientState &state = it->second;
    uint64_t durationUs = Func::TimeUs() - state.timeStart;

    std::cout << "\n=== UB Native Data Server Report (client " << fd << ") ===" << std::endl;
    std::cout << "Messages received: " << state.msgRecv << std::endl;
    std::cout << "Messages sent: " << state.msgSent << std::endl;
    std::cout << "CRC errors: " << state.errorCount << std::endl;
    std::cout << "Total bytes received: " << state.totalBytesRecv << std::endl;
    std::cout << "Total bytes sent: " << state.totalBytesSent << std::endl;
    std::cout << "Duration: " << durationUs / 1000.0 << " ms" << std::endl;

    if (cmd_.IsUbProtocol()) {
        ubsocket_epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
        // Shut down the UB connection gracefully before closing.  This allows the
        // UMQ layer to flush in-flight TX completions and DecRef the associated
        // Blocks, returning UB pages to the memory pool.
        ubsocket_shutdown(fd, SHUT_RDWR);
        usleep(100000);  // 100 ms — give EpollRunner time to drain TX CQEs
    } else {
        ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    }
    CloseSocketByProtocol(fd, cmd_.IsUbProtocol());

    if (state.recvBuf) {
        free(state.recvBuf);
    }
    if (state.sendBuf) {
        free(state.sendBuf);
    }

    clients_.erase(it);
}

int SubCommandUbNativeData::UbNativeDataServer::ProcessEvents(epoll_event *events, int nfds, uint64_t &totalBytesRecv,
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

        // Check for disconnect / error signals BEFORE attempting to write.  If the peer
        // has already closed the connection, calling ubs_post may corrupt UMQ state before
        // we get a chance to clean up — which then poisons the next client connection.
        if (events[i].events & (EPOLLERR | EPOLLHUP
#ifdef EPOLLRDHUP
            | EPOLLRDHUP
#endif
            )) {
            int ret = HandleClientError(fd);
            if (ret < 0) {
                clientDisconnected = true;
            }
        }

        if (!clientDisconnected && (events[i].events & EPOLLOUT)) {
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

        if (clientDisconnected) {
            ResetClientState(fd, totalBytesRecv, totalBytesSent);
        }
    }
    return 0;
}

int SubCommandUbNativeData::UbNativeDataServer::Run()
{
    if (InitListener() < 0) {
        return -1;
    }

    if (SetupEpoll() < 0) {
        return -1;
    }

    std::cout << "UB native data server listening on " << cmd_.ip_ << ":" << cmd_.port_ << std::endl;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = HandleSignal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    uint64_t totalBytesRecv = 0;
    uint64_t totalBytesSent = 0;

    while (!g_quitFlag) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = cmd_.IsUbProtocol() ? ubsocket_epoll_wait(epollFd_, events, MAX_EVENTS, 1000)
                                        : ::epoll_wait(epollFd_, events, MAX_EVENTS, 1000);
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

int SubCommandUbNativeData::DoExecute() noexcept
{
    if (protocol_ == "ub_rm_ctp") {
        u_init_options_t options;
        if (ubsocket_init_options(&options) != 0) {
            std::cout << "Inner error: set ubsocket options failed" << std::endl;
            return -1;
        }

        options.allowed_protocol = Func::ProtocolFromString(protocol_);
        GlobalSetting::UBS_BACKUP_LINK_ENABLED = true;
        ::setenv("UBSOCKET_FLOW_CONTROL_ENABLE", "false", 1);
        ::setenv("UBSOCKET_UB_TRANS_MODE", "RM_CTP", 1);
        ::setenv("UBSOCKET_SIZE_CLASS_COUNT", "2", 1);
        ::setenv("UBSOCKET_TX_UNIFIED_POLL_ENABLED", "true", 0);
        ::setenv("UBS_READ_GEN_CHECK_ENABLED", "true", 0);

        if (ubsocket_init(&options) != 0) {
            std::cout << "Inner error: initialize ubsocket failed" << std::endl;
            return -1;
        }

        atexit(ubsocket_uninit);
    } else {
        GlobalSetting::UBS_NATIVE_TCP_MODE = true;
    }

    if (role_ == "client") {
        UbNativeDataClient client(*this);
        return client.Run();
    } else if (role_ == "server") {
        UbNativeDataServer server(*this);
        return server.Run();
    }

    std::cout << "Invalid role" << std::endl;
    return -1;
}

SubCommand *CreateUbNativeData(const ParamMap &params)
{
    return new (std::nothrow) SubCommandUbNativeData(SUB_CMD_UB_NATIVE_DATA, params);
}

} // namespace golden
