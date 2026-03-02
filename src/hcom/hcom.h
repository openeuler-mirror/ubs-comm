/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 
 * ubs-hcom is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef OCK_HCOM_CPP_H_34562
#define OCK_HCOM_CPP_H_34562

#include <cstdint>
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "hcom_err.h"
#include "hcom_ref.h"
#include "hcom_def.h"
#include "hcom_log.h"
#include "hcom_utils.h"
#include "hcom_obj_statistics.h"

namespace ock {
namespace hcom {
/* flag */
#define NET_EP_SELF_POLLING NET_FLAGS_BIT(0)
#define NET_EP_EVENT_POLLING NET_FLAGS_BIT(1)

class UBSHcomNetEndpoint;
class UBSHcomNetMessage;
class UBSHcomNetRequestContext;
class UBSHcomNetResponseContext;
class UBSHcomNetMemoryRegion;
class UBSHcomNetMemoryAllocator;
class UBSHcomNetDriver;
class NetWorkerLB;
class NetSecrets;
class OOBTCPServer;
class NetServiceGlobalObject;
class OOBTCPConnection;

using UBSHcomNetEndpointPtr = NetRef<UBSHcomNetEndpoint>;
using UBSHcomNetRequestContextPtr = NetRef<UBSHcomNetRequestContext>;
using UBSHcomNetMemoryRegionPtr = NetRef<UBSHcomNetMemoryRegion>;
using UBSHcomNetMemoryAllocatorPtr = NetRef<UBSHcomNetMemoryAllocator>;
using NetOOBServer = OOBTCPServer;

using NetLogger = UBSHcomNetOutLogger;

/* ****************************************************************************************** */
enum UBSHcomNetEndPointState {
    NEP_NEW = 0,
    NEP_ESTABLISHED = 1,
    NEP_BROKEN = 2,

    NEP_BUFF
};

std::string &UBSHcomNEPStateToString(UBSHcomNetEndPointState v);

const char *UBSHcomNetErrStr(int16_t errCode);

bool UBSHcomNetCloneStringToArray(char *dest, size_t destMax, const std::string &src);

#define NN_SET_CHAR_ARRAY_FROM_STRING(CHAR_ARRAY, VALUE)                     \
    do {                                                                     \
        return UBSHcomNetCloneStringToArray(CHAR_ARRAY, sizeof(CHAR_ARRAY), VALUE); \
    } while (0)

#define NN_SET_CHAR_ARRAY_FROM_STRING_VOID(CHAR_ARRAY, VALUE)              \
    do {                                                                     \
        UBSHcomNetCloneStringToArray(CHAR_ARRAY, sizeof(CHAR_ARRAY), VALUE);        \
    } while (0)

#define NN_CHAR_ARRAY_TO_STRING(CHAR_ARRAY)                    \
    {                                                          \
        CHAR_ARRAY, strlen(CHAR_ARRAY) <= sizeof(CHAR_ARRAY) ? \
                            strlen(CHAR_ARRAY) :               \
                            sizeof(CHAR_ARRAY)                 \
    }

enum class UBSHcomNetRequestStatus {
    CALLED = 0,
    IN_HCOM,
    IN_URMA,
    POLLED,
    SUCCESS
};

std::string &UBSHcomRequestStatusToString(UBSHcomNetRequestStatus status);

void SetTraceIdInner(const std::string &traceId);

/// 传输层请求
/// 它有以下几种典型用法：
/// - 双边 bcopy, 上层应用只需要填充 `lAddress`, `size`和 `upCtxData`. 然后调用 hcom 的
/// `PostSend()` 接口将 `[lAddress, lAddress + size)` 区间拷贝到一块注册过的内存上，随
/// 后自动调整 `lAddress`, `lkey` 和 `size` (有额外头部)
/// - 单边 RDMA, 需要填充 `lAddress`, `rAddress`, `lkey`, `rkey`, `size` 和`upCtxData`. 随
/// 后调用 `PostWrite()` 时会直接使用这些参数，所以要求 `lAddress`, `rAddress` 都提前注册
/// 好了。
/// - 单边 UBC 场景与 RDMA 保持一致。
struct UBSHcomNetTransRequest {
    uintptr_t lAddress = 0;        ///< 本地读取地址
    uintptr_t rAddress = 0;        ///< 远端写入地址
    uint64_t lKey = 0;             ///< 本地 lkey, 适用于 RDMA/UB/TCP/SHM
    uint64_t rKey = 0;             ///< 远端 rkey, 适用于 RDMA/UB/TCP/SHM
    void *srcSeg = nullptr;        ///< 仅适用于 UB
    void *dstSeg = nullptr;        ///< 仅适用于 UB
    uint32_t size = 0;             ///< 写入字节数
    uint16_t upCtxSize = 0;        ///< 上层 ctx 大小。默认为 0 代表 upCtxData 无效
    char upCtxData[NN_NO64] = {};  ///< 可用于存储上层 ctx

    UBSHcomNetTransRequest() = default;

    UBSHcomNetTransRequest(void *data, uint32_t dataSize, uint16_t upContextSize)
        : lAddress(reinterpret_cast<uintptr_t>(data)),
          size(dataSize),
          upCtxSize(upContextSize)
    {
    }

    UBSHcomNetTransRequest(uintptr_t la, uintptr_t ra, uint64_t lk, uint64_t rk,
                    uint32_t s, uint16_t upCtxSi)
        : lAddress(la),
          rAddress(ra),
          lKey(lk),
          rKey(rk),
          size(s),
          upCtxSize(upCtxSi)
    {
    }

    UBSHcomNetTransRequest(uintptr_t la, uintptr_t ra, uint64_t lk, uint64_t rk,
                    uint32_t s, uint16_t upCtxSi, void *sSeg, void *dSeg)
        : lAddress(la),
          rAddress(ra),
          lKey(lk),
          rKey(rk),
          size(s),
          upCtxSize(upCtxSi)
    {
        // avoid cleancode check
        srcSeg = sSeg;
        dstSeg = dSeg;
    }
} __attribute__((packed));

struct UBSHcomNetTransSglRequest {
    UBSHcomNetTransSgeIov *iov = nullptr;  // array
    uint16_t iovCount = 0;          // max count: NET_SGE_MAX_IOV
    uint16_t upCtxSize = 0;         // upper context size
    char upCtxData[NN_NO16] = {};   // upper context data

    UBSHcomNetTransSglRequest() = default;

    UBSHcomNetTransSglRequest(UBSHcomNetTransSgeIov *iovPtr, uint16_t cnt, uint16_t upCtxSi)
        : iov(iovPtr), iovCount(cnt), upCtxSize(upCtxSi)
    {}
} __attribute__((packed));

struct UBSHcomNetTransOpInfo {
    uint32_t seqNo = 0;    // seq no
    int16_t timeout = 0;  // timeout
    int16_t errorCode = 0; // error code
    uint8_t flags = 0;     // flags in user case

    UBSHcomNetTransOpInfo() = default;

    UBSHcomNetTransOpInfo(uint32_t seqNo, int16_t timeout, int16_t errorCode, uint8_t flags)
        : seqNo(seqNo), timeout(timeout), errorCode(errorCode), flags(flags)
    {}
    UBSHcomNetTransOpInfo(uint32_t seqNo, int16_t timeout) : seqNo(seqNo), timeout(timeout) {}
} __attribute__((packed));

struct UBSHcomNetUdsIdInfo {
    uint32_t pid = 0;  // process id
    uint32_t uid = 0;  // user id
    uint32_t gid = 0;  // group id

    UBSHcomNetUdsIdInfo() = default;

    UBSHcomNetUdsIdInfo(uint32_t pid, uint32_t uid, uint32_t gid)
        : pid(pid), uid(uid), gid(gid){};
} __attribute__((packed));

union UBSHcomEpOptions {
    struct {
        bool tcpBlockingIo;
        bool cbByWorkerInBlocking;
        int32_t sendTimeout;  // send timeout in blocking mode in second
    };

    void Set(bool tcpBI, bool cb, int32_t st)
    {
        tcpBlockingIo = tcpBI;
        cbByWorkerInBlocking = cb;
        sendTimeout = st;
    }

    UBSHcomEpOptions()
    {
        tcpBlockingIo = false;
        cbByWorkerInBlocking = false;
        sendTimeout = -1;
    }
};

/**
 * @brief Cipher suite ids
 */
enum UBSHcomNetCipherSuite {
    AES_GCM_128 = 0,
    AES_GCM_256 = 1,
    AES_CCM_128 = 2,
    CHACHA20_POLY1305 = 3,
};

enum UBSHcomTlsVersion : uint32_t {
    TLS_1_2 = NN_NO771,
    TLS_1_3 = NN_NO772,
};

/**
 * @brief Endpoint for data transfer, representing a connection
 */
class UBSHcomNetEndpoint {
public:
    virtual ~UBSHcomNetEndpoint()
    {
        OBJ_GC_DECREASE(UBSHcomNetEndpoint);
    }

    /**
     * @brief Only support TCP now, and TCP is nonblocking in default, only could be set from nonblocking to blocking
     * if set as blocking, there might occur function problems in some conditions.
     */
    virtual NResult SetEpOption(UBSHcomEpOptions &epOptions) = 0;

    /**
     * @brief Get using count in sending queue
     */
    virtual uint32_t GetSendQueueCount() = 0;

    /**
     * @brief Get the id of the endpoint
     */
    inline uint64_t Id() const
    {
        return mId;
    }

    /**
     * @brief Get the worker index of the endpoint
     */
    const UBSHcomNetWorkerIndex &WorkerIndex() const;

    /**
     * @brief Check if ep is in established state
     */
    bool IsEstablished();

    /**
     * @brief Set the upper context, which could be used store user data pointer and read it when handler called
     */
    void UpCtx(uint64_t ctx);

    /**
     * @brief get the upper context
     */
    uint64_t UpCtx() const;

    /**
     * @brief Get the payload
     */
    const std::string &PeerConnectPayload() const;

    /**
     * @brief Get the local ip
     */
    uint32_t LocalIp() const;

    /**
     * @brief Get the listen port
     */
    uint16_t ListenPort() const;

    /**
     * @brief Get the driver version
     */
    uint8_t Version() const;

    /**
     * @brief Get state, don't change it which could leading to uncertain behavior
     */
    inline UBSHcomNetAtomicState<UBSHcomNetEndPointState> &State()
    {
        return mState;
    }

    /**
     * @brief Get the peer ip and port of oob tcp connection, which used to identify where peer comes from
     */
    virtual const std::string &PeerIpAndPort() = 0;

    virtual const std::string &UdsName() = 0;

    /**
     * @brief Post send a request with opcode and header to peer, peer will be trigger new request callback also with
     * opcode and header
     *
     * @param opCode       [in] operation code, 0~1023
     * @param request      [in] request information, local address and size is used only, the data is copied, you can
     * free it after called
     * @param seqNo        [in] seq number for peer to reply, must be > 0, peer can get it from context.Header().seqNo;
     * if it is 0, an auto increased number is generated, for sync client it will be matching request and response
     *
     * Behavior:
     * 1 For RDMA,
     * case a) if NET_EP_SELF_POLLING is not set, just issue the send request, not wait for sending request finished
     * case b) if NET_EP_SELF_POLLING is set, issue the send request and wait for sending arrived to peer
     *
     * @return 0 if successful
     *
     */
    virtual NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
                             uint32_t seqNo) = 0;

    virtual NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
                             const UBSHcomNetTransOpInfo &opInfo) = 0;

    /**
     * @brief Post send a request with opcode and header to peer, peer will be trigger new request callback also with
     * opcode and header
     *
     * @param opCode       [in] operation code, 0~1023
     * @param request      [in] request information, local address and size is used only, the data is copied, you can
     * free it after called
     *
     * Behavior:
     * 1 For RDMA,
     * case a) if NET_EP_SELF_POLLING is not set, just issue the send request, not wait for sending request finished
     * case b) if NET_EP_SELF_POLLING is set, issue the send request and wait for sending arrived to peer
     *
     * @return 0 if successful
     *
     */
    inline NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request)
    {
        return PostSend(opCode, request, 0);
    }

    /**
     * @brief Post send a request without opcode and header to peer, peer will be trigger new request callback also
     * without opcode and header, this could be used when you have self define header
     *
     * @param request      [in] request information, local address and size is used only, the data is copied, you can
     * free it after called
     * @param seqNo        [in] seq no for peer to reply, must be > 0, peer can get it from context.Header().seqNo,
     * for sync client it will be matching request and response
     *
     * Behavior:
     * 1 For RDMA,
     * case a) if NET_EP_SELF_POLLING is not set, just issue the send request, not wait for sending request finished
     * case b) if NET_EP_SELF_POLLING is set, issue the send request and wait for sending arrived to peer
     *
     * @return 0 if successful
     *
     */
    virtual NResult
    PostSendRaw(const UBSHcomNetTransRequest &request, uint32_t seqNo) = 0;

    /**
     * @brief Post send a request without opcode and header to peer, peer will be trigger new request callback also
     * without opcode and header, this could be used when you have self define header
     *
     * @param request      [in] request information, fill with local different MRs, send to the same remote MR by local
     * MRs sequence, you can free it after called. rKey/rAddress do not need to assign
     * @param seqNo        [in] seq no for peer to reply, must be > 0, peer can get it from context.Header().seqNo,
     * for sync client it will be matching request and response
     *
     * Behavior:
     * 1 For RDMA,
     * case a) if NET_EP_SELF_POLLING is not set, just issue the send request, not wait for sending request finished
     * case b) if NET_EP_SELF_POLLING is set, issue the send request and wait for sending arrived to peer
     *
     * @return 0 if successful
     *
     */
    virtual NResult
    PostSendRawSgl(const UBSHcomNetTransSglRequest &request, uint32_t seqNo) = 0;

    /**
     * @brief Post a single side read request to peer, no callback at peer will be triggered
     *
     * @param request      [in] request information, including 5 important variables, local/remote address/key and size
     * also an upper context for user context, which could store 16 bytes
     *
     * Behavior:
     * just issue the read request, not wait for reading request finished
     *
     * @return 0 if successful
     *
     */
    virtual NResult PostRead(const UBSHcomNetTransRequest &request) = 0;

    virtual NResult PostRead(const UBSHcomNetTransSglRequest &request) = 0;

    /**
     * @brief Post a single side write request to peer, no callback at peer will be triggered
     *
     * @param request      [in] request information, including 5 important variables, local/remote address/key and size
     * also an upper context for user context, which could store 16 bytes
     *
     * Behavior:
     * just issue the write request, not wait for writing request finished
     *
     * @return 0 if successful
     *
     */
    virtual NResult PostWrite(const UBSHcomNetTransRequest &request) = 0;

    virtual NResult PostWrite(const UBSHcomNetTransSglRequest &request) = 0;

    /**
     * @brief Set default timeout
     *
     * 1. timeout = 0: return immediately
     * 2. timeout < 0: never timeout, usually set to -1
     * 3. timeout > 0: second precision timeout.
     */
    void DefaultTimeout(int32_t timeout);

    /**
     * @brief Wait for send/read/write finish, only for NET_EP_SELF_POLLING is set
     *
     * @param timeout      [in] in second
     * 1. timeout = 0: return immediately
     * 2. timeout < 0: never timeout, usually set to -1
     * 3. timeout > 0: second precision timeout max is 2000s.
     *
     * Behavior:
     * 1 for send, return when request send to peer
     * 2 for read, return when read completion
     * 3 for write, return when write completion
     *
     * @return 0 if successful
     *
     * NN_TIMEOUT if timeout
     *
     */
    virtual NResult WaitCompletion(int32_t timeout) = 0;

    /**
     * @brief Wait for send/read/write finish, only for NET_EP_SELF_POLLING is set
     *
     * Behavior:
     * 1 for send, return when request send to peer
     * 2 for read, return when read completion
     * 3 for write, return when write completion
     *
     * Default timeout will be used
     *
     * @return 0 if successful
     *
     * NN_TIMEOUT if timeout
     *
     */
    inline NResult WaitCompletion()
    {
        return WaitCompletion(mDefaultTimeout);
    }

    /**
     * @brief Get the response for send request reply
     *
     * @param timeout      [in] in second
     * 1. timeout = 0: return immediately
     * 2. timeout < 0: never timeout, usually set to -1
     * 3. timeout > 0: second precision timeout max is 2000s.
     * @param ctx          [out] ctx for response message
     *
     * @return 0 if successful
     */
    virtual NResult Receive(int32_t timeout, UBSHcomNetResponseContext &ctx) = 0;

    /**
     * @brief Get the response for send request reply
     * Default timeout will be used
     *
     * @param ctx          [out] ctx for response message
     *
     * @return 0 if successful
     */
    inline NResult Receive(UBSHcomNetResponseContext &ctx)
    {
        return Receive(mDefaultTimeout, ctx);
    }

    /**
     * @brief Get the response for send request reply, without header and opCode etc
     *
     * @param timeout      [in] in second
     * 1. timeout = 0: return immediately
     * 2. timeout < 0: never timeout, usually set to -1
     * 3. timeout > 0: second precision timeout max is 2000s.
     * @param ctx          [out] ctx for response message,
     *
     * @return 0 if successful
     */
    virtual NResult ReceiveRaw(int32_t timeout, UBSHcomNetResponseContext &ctx) = 0;

    /**
     * @brief Get the response for send request reply, without header and opCode etc
     * Default timeout will be used
     *
     * @param ctx          [out] ctx for response message
     *
     * @return 0 if successful
     */
    inline NResult ReceiveRaw(UBSHcomNetResponseContext &ctx)
    {
        return ReceiveRaw(mDefaultTimeout, ctx);
    }

    /**
     * @brief Get the response for send request reply, without header and opCode etc
     * Default timeout will be used
     *
     * @param ctx          [out] ctx for response message
     *
     * @return 0 if successful
     */
    inline NResult ReceiveRawSgl(UBSHcomNetResponseContext &ctx)
    {
        return ReceiveRaw(mDefaultTimeout, ctx);
    }

    /**
     * @brief Estimated Encrypt length for input raw len
     *
     * @param rawLen       [in] raw length before encrypt
     *
     * @return the length after encrypt
     */
    virtual uint64_t EstimatedEncryptLen(uint64_t rawLen)
    {
        return 0;
    }

    /**
     * @brief Encrypt data
     *
     * @param rawData      [in] raw data before encrypt
     * @param rawLen       [in] raw data length before encrypt
     * @param cipher       [out] cipher data after encrypt
     * @param cipherLen    [out] cipher data length after encrypt
     *
     * @return 0 if success
     */
    virtual NResult Encrypt(const void *rawData, uint64_t rawLen, void *cipher,
                            uint64_t &cipherLen)
    {
        return 0;
    }

    /**
     * @brief Estimate Decrypt length
     *
     * @param cipherLen    [in] cipher len before decrypt
     *
     * @return the raw length after decrypt
     */
    virtual uint64_t EstimatedDecryptLen(uint64_t cipherLen)
    {
        return 0;
    }

    /**
     * @brief Decrypt data
     *
     * @param cipher       [in] cipher data after encrypt
     * @param cipherLen    [in] cipher data length after encrypt
     * @param rawData      [out] raw data before encrypt
     * @param rawLen       [out] raw data length before encrypt
     *
     * @return 0 if success
     */
    virtual NResult Decrypt(const void *cipher, uint64_t cipherLen,
                            void *rawData, uint64_t &rawLen)
    {
        return 0;
    }

    /**
     * @brief Send shm fds, only shm protocol support
     *
     * @param fds          [in] fds to send
     * @param len          [in] fds count to send
     *
     * @return 0 if success
     */
    virtual NResult SendFds(int fds[], uint32_t len)
    {
        return NN_EXCHANGE_FD_NOT_SUPPORT;
    }

    /**
     * @brief Receive shm fds, only shm protocol support
     *
     * @param fds          [out] fds to be received
     * @param len          [in] fds count to be received
     * @param timeoutSec   [in] timeout in second for receive. -1 is never timeout
     *
     * @return 0 if success
     */
    virtual NResult ReceiveFds(int fds[], uint32_t len, int32_t timeoutSec)
    {
        return NN_EXCHANGE_FD_NOT_SUPPORT;
    }

    /**
     * @brief Get remote uds ids include pid uid gid, only support in oob server and when oob type is uds
     *
     * @param idInfo       [out] remote uds ids
     */
    virtual NResult GetRemoteUdsIdInfo(UBSHcomNetUdsIdInfo &idInfo)
    {
        return NN_UDS_ID_INFO_NOT_SUPPORT;
    }

    /**
     * @brief Get ip and port of peer
     */
    virtual bool GetPeerIpPort(std::string &ip, uint16_t &port) = 0;

    /**
     * @brief Close endpoint, then will async call broken function
     */
    virtual void Close() {}

    inline uint8_t GetDevIndex() const
    {
        return mDevIndex;
    }

    inline uint8_t GetPeerDevIndex() const
    {
        return mPeerDevIndex;
    }

    inline uint8_t GetBandWidth() const
    {
        return mBandWidth;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

protected:
    explicit UBSHcomNetEndpoint(uint64_t id, const UBSHcomNetWorkerIndex &workerWholeIndex)
        : mId(id)
    {
        OBJ_GC_INCREASE(UBSHcomNetEndpoint);
        mWorkerIndex = workerWholeIndex;
    }

    inline uint32_t NextSeq()
    {
        return __sync_fetch_and_add(&mSeqIndex, 1);
    }

    /**
     * To later, change this to private and using friend to access this
     */
    inline std::atomic_bool &EPBrokenProcessed()
    {
        return mEPBrokenProcessed;
    }

    bool IsNeedSendHb() const;

    virtual NResult PostSendSglInline(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo)
    {
        return PostSend(opCode, request, opInfo);
    }

protected:
    uint64_t mUpCtx = 0;
    uint32_t mSeqIndex = 1;
    uint32_t mSegSize = 0;
    /// mAllowedSize 通常表示为除 UBSHcomNetTransHeader 外可允许发送消息的大小。但是有
    /// 时候服务层可能会有 ExtHeader 需要发送，需注意。通常它的检查是在
    /// POST_SEND_VALIDATION 中，当涉及到 ExtHeader 时可直接减去 extHeaderSize.
    /// \see NetAsyncEndpoint::PostSend
    /// \see NetSyncEndpoint::PostSend
    /// \see NetUBAsyncEndpoint::PostSend
    /// \see NetUBSyncEndpoint::PostSend
    uint32_t mAllowedSize = 0;
    int32_t mDefaultTimeout = -1;

    UBSHcomNetWorkerIndex mWorkerIndex{};
    UBSHcomNetAtomicState<UBSHcomNetEndPointState> mState{NEP_NEW};

    bool mIsNeedSendHb = false;
    std::atomic_bool mEPBrokenProcessed{false};

    uint64_t mId = 0;
    UBSHcomNetUdsIdInfo mRemoteUdsIdInfo{};
    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class NetHeartbeat;

    // 服务层拆包专用，上层用户在调用时应当保证 extHeaderType != RAW
    virtual NResult PostSend(uint16_t opCode, const UBSHcomNetTransRequest &request,
        const UBSHcomNetTransOpInfo &opInfo, const UBSHcomExtHeaderType extHeaderType, const void *extHeader,
        uint32_t extHeaderSize)
    {
        NN_LOG_WARN("PostSend with header unimplemented yet!!!");
        return NN_ERROR;
    }

    friend class NetChannel;
    friend class HcomChannelImp;
    friend class Publisher;
    friend class SubscriberContext;

private:
    /**
     * @brief Set the connect info
     */
    void StoreConnInfo(uint32_t localIp, uint16_t listenPort, uint8_t version,
                       const std::string &payload);

    /**
     * @brief Set the payload
     */
    void Payload(const std::string &payload);

    /**
     * @brief Set remote uds id info
     */
    void RemoteUdsIdInfo(uint32_t pid, uint32_t uid, uint32_t gid);

    virtual NResult PostSendRawNoCpy(const UBSHcomNetTransRequest &request, uint32_t seqNo)
    {
        return NN_OK;
    }

    uint32_t mLocalIp = INVALID_IP;
    uint16_t mListenPort = 0;
    uint8_t mVersion = 0;
    std::string mPayload;
    uint8_t mDevIndex = 0;
    uint8_t mPeerDevIndex = 0;
    uint8_t mBandWidth = 0;

    friend class NetDriverRDMAWithOob;
    friend class NetDriverSockWithOOB;
    friend class NetDriverShmWithOOB;
    friend class Publisher;
#ifdef UB_BUILD_ENABLED
    friend class NetDriverUBWithOob;
#endif
};

inline void UBSHcomNetEndpoint::UpCtx(uint64_t ctx)
{
    mUpCtx = ctx;
}

inline uint64_t UBSHcomNetEndpoint::UpCtx() const
{
    return mUpCtx;
}

inline bool UBSHcomNetEndpoint::IsNeedSendHb() const
{
    return mIsNeedSendHb;
}

/* ****************************************************************************************** */
class UBSHcomNetMessage {
public:
    inline uint32_t DataLen() const
    {
        return mDataLen;
    }

    inline void *Data() const
    {
        return mBuf;
    }

    uint32_t GetBufLen() const
    {
        return mBufLen;
    }

protected:
    UBSHcomNetMessage()
    {
        OBJ_GC_INCREASE(UBSHcomNetMessage);
    }

    ~UBSHcomNetMessage()
    {
        if (mBuf != nullptr) {
            free(mBuf);
            mBuf = nullptr;
        }

        OBJ_GC_DECREASE(UBSHcomNetMessage);
    }

    inline bool AllocateIfNeed(uint32_t newSize)
    {
        if (NN_UNLIKELY(newSize == NN_NO0)) {
            NN_LOG_ERROR("Invalid msg size " << newSize << ", alloc failed");
            return false;
        }
        if (newSize > mBufLen) {
            if (mBuf != nullptr) {
                free(mBuf);
            }

            if ((mBuf = malloc(newSize)) != nullptr) {
                mBufLen = newSize;
                return true;
            }
            mBuf = nullptr;
            mBufLen = NN_NO0;
            return false;
        }

        return true;
    }

    inline void SetBuf(void *buf, uint32_t len)
    {
        mBuf = buf;
        mBufLen = len;
    }

    UBSHcomNetMessage(const UBSHcomNetMessage &) = delete;
    UBSHcomNetMessage(UBSHcomNetMessage &&) = delete;
    UBSHcomNetMessage &operator=(const UBSHcomNetMessage &) = delete;
    UBSHcomNetMessage &operator=(UBSHcomNetMessage &&) = delete;

private:
    uint32_t mBufLen = 0;
    uint32_t mDataLen = 0;
    void *mBuf = nullptr;

    friend class NetAsyncEndpoint;
    friend class NetSyncEndpoint;
    friend class NetAsyncEndpointSock;
    friend class NetSyncEndpointSock;
    friend class NetSyncEndpointShm;
    friend class NetDriverSockWithOOB;
    friend class NetDriverRDMAWithOob;
    friend class NetDriverShmWithOOB;
    friend class NetAsyncEndpointShm;
    friend class NetServiceDefaultImp;

#ifdef UB_BUILD_ENABLED
    friend class NetUBAsyncEndpoint;
    friend class NetUBSyncEndpoint;
    friend class NetDriverUBWithOob;
#endif
};

/* ****************************************************************************************** */
/**
 * @brief UBSHcomNetRequestContext
 */
class UBSHcomNetRequestContext {
public:
    enum NN_OpType : uint8_t {
        NN_SENT = 0,
        NN_SENT_RAW = 1,
        NN_SENT_RAW_SGL = 2,
        NN_RECEIVED = 3,
        NN_RECEIVED_RAW = 4,
        NN_WRITTEN = 5,
        NN_READ = 6,
        NN_SGL_WRITTEN = 7,
        NN_SGL_READ = 8,
        NN_RNDV = 9,
        NN_SENT_SGL_INLINE = 10,

        NN_INVALID_OP_TYPE = 255,
    };

    /**
     * @brief Get the endpoint of context
     */
    const UBSHcomNetEndpointPtr &EndPoint() const;

    /**
     * @brief Get result of all operation
     */
    NResult Result() const;

    /**
     * @brief Get header of two side operation
     */
    const UBSHcomNetTransHeader &Header() const;

    /**
     * @brief Get the message received
     */
    UBSHcomNetMessage *Message() const;

    /**
     * @brief Get the operation type, send/receive/read/write
     */
    NN_OpType OpType() const;

    /**
     * @brief Get the original request
     */
    const UBSHcomNetTransRequest &OriginalRequest() const;

    /**
     * @brief Get the original sgl request
     */
    const UBSHcomNetTransSglRequest &OriginalSgeRequest() const;

    // the passed context cannot be copy directly need to use SafeClone()
    // if needing to transfer to thread to process in async
    static bool
    SafeClone(const UBSHcomNetRequestContext &old, const UBSHcomNetRequestContextPtr &newOne)
    {
        if (NN_UNLIKELY(newOne.Get() == nullptr)) {
            return false;
        }

        newOne->mEp = old.mEp;
        newOne->mHeader = old.mHeader;
        newOne->mOpType = old.mOpType;
        return true;
    }

    UBSHcomNetRequestContext() : mMessage(nullptr)
    {
        OBJ_GC_INCREASE(UBSHcomNetRequestContext);
    }

    ~UBSHcomNetRequestContext()
    {
        OBJ_GC_DECREASE(UBSHcomNetRequestContext);
    }

    UBSHcomNetRequestContext(const UBSHcomNetRequestContext &) = delete;
    UBSHcomNetRequestContext(UBSHcomNetRequestContext &&) = delete;
    UBSHcomNetRequestContext &operator=(const UBSHcomNetRequestContext &) = delete;
    UBSHcomNetRequestContext &operator=(UBSHcomNetRequestContext &&) = delete;

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    UBSHcomNetEndpointPtr mEp = nullptr;
    NResult mResult = NN_OK;
    UBSHcomNetTransHeader mHeader{};
    NN_OpType mOpType = NN_RECEIVED;
    UBSHcomNetMessage *mMessage = nullptr;
    UBSHcomNetTransRequest mOriginalReq{};  // copy information, not original address

    UBSHcomNetTransSgeIov iov[NET_SGE_MAX_IOV];
    UBSHcomNetTransSglRequest
            mOriginalSglReq{};  // copy information, not original address

    UBSHcomExtHeaderType extHeaderType = UBSHcomExtHeaderType::RAW;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class NetAsyncEndpoint;
    friend class NetSyncEndpoint;
    friend class NetAsyncEndpointSock;
    friend class NetSyncEndpointSock;
    friend class NetSyncEndpointShm;
    friend class NetDriverSockWithOOB;
    friend class NetDriverRDMAWithOob;
    friend class NetDriverShmWithOOB;
    friend class NetServiceGlobalObject;
    friend class NetServiceDefaultImp;
    friend class HcomServiceImp;

#ifdef UB_BUILD_ENABLED
    friend class NetUBAsyncEndpoint;
    friend class NetUBSyncEndpoint;
    friend class NetDriverUBWithOob;
#endif
};

inline const UBSHcomNetEndpointPtr &UBSHcomNetRequestContext::EndPoint() const
{
    return mEp;
}

inline NResult UBSHcomNetRequestContext::Result() const
{
    return mResult;
}

inline const UBSHcomNetTransHeader &UBSHcomNetRequestContext::Header() const
{
    return mHeader;
}

inline UBSHcomNetMessage *UBSHcomNetRequestContext::Message() const
{
    return mMessage;
}

inline UBSHcomNetRequestContext::NN_OpType UBSHcomNetRequestContext::OpType() const
{
    return mOpType;
}

inline const UBSHcomNetTransRequest &UBSHcomNetRequestContext::OriginalRequest() const
{
    return mOriginalReq;
}

inline const UBSHcomNetTransSglRequest &UBSHcomNetRequestContext::OriginalSgeRequest() const
{
    return mOriginalSglReq;
}

/**
 * @brief Response context for sync call
 */
class UBSHcomNetResponseContext {
public:
    /**
     * @brief Get header of response
     */
    const UBSHcomNetTransHeader &Header() const;

    UBSHcomNetMessage *Message() const;

    UBSHcomNetResponseContext() : mMessage(nullptr)
    {
        OBJ_GC_INCREASE(UBSHcomNetResponseContext);
    }

    ~UBSHcomNetResponseContext()
    {
        OBJ_GC_DECREASE(UBSHcomNetResponseContext);
    }

    UBSHcomNetResponseContext(const UBSHcomNetRequestContext &) = delete;
    UBSHcomNetResponseContext(UBSHcomNetRequestContext &&) = delete;
    UBSHcomNetResponseContext &operator=(const UBSHcomNetRequestContext &) = delete;
    UBSHcomNetResponseContext &operator=(UBSHcomNetRequestContext &&) = delete;

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    UBSHcomNetTransHeader mHeader{};
    UBSHcomNetMessage *mMessage = nullptr;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class NetAsyncEndpoint;
    friend class NetSyncEndpoint;
    friend class NetAsyncEndpointSock;
    friend class NetSyncEndpointSock;
    friend class NetSyncEndpointShm;
    friend class NetDriverSockWithOOB;
    friend class NetDriverRDMAWithOob;

#ifdef UB_BUILD_ENABLED
    friend class NetUBAsyncEndpoint;
    friend class NetUBSyncEndpoint;
    friend class NetDriverUBWithOob;
#endif
};

inline const UBSHcomNetTransHeader &UBSHcomNetResponseContext::Header() const
{
    return mHeader;
}

inline UBSHcomNetMessage *UBSHcomNetResponseContext::Message() const
{
    return mMessage;
}

/* ****************************************************************************************** */
/**
 * @brief Memory region for one side operation
 */
class UBSHcomNetMemoryRegion {
public:
    /**
     * @brief Initialize memory region, lkey can be got after
     *
     * Behavior
     * 1) RDMA, physical memory will be allocated and registered to hardware, will be pinned
     * 2) TCP/UDS, physical memory will be allocated
     *
     * @return 0 successful
     */
    virtual NResult Initialize() = 0;

    /**
     * @brief Get local key
     */
    inline uint64_t GetLKey() const
    {
        return mLKey;
    }

    /**
     * @brief Get address
     */
    inline uintptr_t GetAddress() const
    {
        return mBuf;
    }

    /**
     * @brief Get size of memory size
     */
    inline uint64_t Size() const
    {
        return mSize;
    }

    virtual void *GetMemorySeg() = 0;

    virtual void GetVa(uint64_t &va, uint64_t &vaLen, uint32_t &tokenId) = 0;

    virtual uint8_t *GetEidRaw() = 0;
    
    DEFINE_RDMA_REF_COUNT_FUNCTIONS

protected:
    UBSHcomNetMemoryRegion(const std::string &name, bool extMem, uintptr_t buf,
                    uint64_t size)
        : mName(name), mExternalMemory(extMem), mSize(size), mBuf(buf)
    {}

    /**
     * @brief UnInitialize
     */
    virtual void UnInitialize() = 0;

    virtual ~UBSHcomNetMemoryRegion() = default;

protected:
    std::string mName;
    bool mExternalMemory = false;
    uint64_t mSize = 0;

    uintptr_t mBuf = 0;
    bool mGetBufWithMapping = false;
    uint64_t mLKey = 0;
    uintptr_t mPgRegion = 0;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class NetDriverRDMA;
    friend class NetDriverSockWithOOB;
    friend class NetDriverShmWithOOB;
#ifdef UB_BUILD_ENABLED
    friend class NetDriverUB;
    friend class UBJetty;
#endif
    friend class HcomServiceImp;
};

/**
 * @brief Type of allocator
 */
enum UBSHcomNetMemoryAllocatorType {
    DYNAMIC_SIZE =
            0, /* allocate dynamic memory size, there is alignment with X KB */
    DYNAMIC_SIZE_WITH_CACHE =
            1, /* allocator with dynamic memory size, with pre-allocate cache for performance */
};

/**
 * @brief Covert UBSHcomNetMemoryAllocatorType to string
 *
 * @param v                [in] value to type to be converted
 *
 * @return string coverted
 */
std::string &UBSHcomNetMemoryAllocatorTypeToString(UBSHcomNetMemoryAllocatorType v);

/**
 * @brief Allocator cache tier policy
 */
enum UBSHcomNetMemoryAllocatorCacheTierPolicy : int16_t {
    TIER_TIMES = 0, /* tier by times of min-block-size */
    TIER_POWER = 1, /* tier by power of min-block-size */
};

/**
 * @brief Allocator options
 */
struct UBSHcomNetMemoryAllocatorOptions {
    uintptr_t address = 0;                     /* base address of large range of memory for allocator */
    uint64_t size = 0;                         /* size of large memory chuck */
    uint32_t minBlockSize = 0;                 /* min size of block can be allocated from allocator */
    uint32_t bucketCount = NN_NO8192;          /* default size of hash bucket */
    bool alignedAddress = false;               /* force to align the memory block allocated */
    uint16_t cacheTierCount = NN_NO8;          /* for DYNAMIC_SIZE_WITH_CACHE only */
    uint16_t cacheBlockCountPerTier = NN_NO16; /* for DYNAMIC_SIZE_WITH_CACHE only */
    UBSHcomNetMemoryAllocatorCacheTierPolicy cacheTierPolicy = TIER_TIMES; /* tier policy */

    std::string ToString() const;
};

/**
 * @brief Allocator to alloc memory area from a large mount of memory.
 *
 * For example, we have RDMA memory region, which already registered to NIC,
 * and we need to reuse memory on this region, so we need to alloc sub part
 * of memory from the large memory region, use it and return it.
 */
class UBSHcomNetMemoryAllocator {
public:
    /**
     * @brief Create a memory allocator
     *
     * @param t            [in] type of allocator
     * @param options      [in] options
     * @param allocator    [out] allocator created
     */
    static NResult Create(UBSHcomNetMemoryAllocatorType t,
                          const UBSHcomNetMemoryAllocatorOptions &options,
                          UBSHcomNetMemoryAllocatorPtr &out);

public:
    virtual ~UBSHcomNetMemoryAllocator() = default;

    /**
     * @brief Get the memory region key
     *
     * @return key
     */
    uint64_t MrKey() const;

    /**
     * @brief Set the memory region key
     */
    void MrKey(uint64_t mrKey);

    void *GetTargetSeg() const;

    void SetTargetSeg(void *targetSeg);

    /**
     * @brief Get the memory offset based on base address
     *
     * @param address      [in] memory address
     *
     * @return offset comparing to base address
     */
    virtual uintptr_t MemOffset(uintptr_t address) const = 0;

    /**
     * @brief Get free memory size
     *
     * @return Free memory size
     */
    virtual uint64_t FreeSize() const = 0;

    /**
     * @brief Allocate memory area
     *
     * @param size         [in] size of memory of demand
     * @param outAddress   [out] allocated memory address
     *
     * @return 0 if successful
     */
    virtual NResult Allocate(uint64_t size, uintptr_t &outAddress) = 0;

    /**
     * @brief Free the address allocated by #Allocate function
     *
     * @param address      [in] address to be freed
     *
     * @param 0 if successful
     */
    virtual NResult Free(uintptr_t address) = 0;

    /**
     * @brief function should be called before managed memory freeing
     *
     * Remove memory protection if enabled(cmake -DBUILD_WITH_ALLOCATOR_PROTECTION=ON),
     * should be called before freeing the memory passed in, otherwise sigsegv will raise by free(),
     * It's suggested to be called even if you are not using memory protection currently,
     * in case you may miss this once you turn memory protection on in the future.
     */
    virtual void Destroy(){};

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

private:
    uint64_t mMrKey = 0;
    void *mTargetSeg = nullptr;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
};

inline uint64_t UBSHcomNetMemoryAllocator::MrKey() const
{
    return mMrKey;
}

inline void UBSHcomNetMemoryAllocator::MrKey(uint64_t mrKey)
{
    mMrKey = mrKey;
}

inline void *UBSHcomNetMemoryAllocator::GetTargetSeg() const
{
    return mTargetSeg;
}

inline void UBSHcomNetMemoryAllocator::SetTargetSeg(void *targetSeg)
{
    mTargetSeg = targetSeg;
}

/**
 * @brief Oob listening information for multiple listen port
 */
struct UBSHcomNetOobListenerOptions {
    char ip[NN_NO40]{};   /* listening ip */
    uint16_t port = 9980; /* listening port */
    uint16_t targetWorkerCount =
            UINT16_MAX; /* the count of target workers, if >= 1, the
                                       accepted socket will be attached to sub set to workers, 0 means all */

    /**
     * @brief Set ip/port/targetWorkerCount
     *
     * @param pIp          [in] ip to set
     * @param pp           [in] port to set
     * @param twc          [in] target worker count to set
     */
    bool Set(const std::string &pIp, uint16_t pp, uint16_t twc);

    /**
     * @brief Set ip/port/targetWorkerCount
     *
     * @param eid          [in] public jetty eid to set
     * @param id           [in] public jetty id to set
     * @param twc          [in] target worker count to set
     */
    bool SetEid(const std::string &eid, uint16_t id, uint16_t twc);

    /**
     * @brief Set ip/port, targetWorkerCount will be set to uint16_max
     *
     * @param pIp          [in] ip to set
     * @param pp           [in] port to set
     */
    bool Set(const std::string &pIp, uint16_t pp);

    /**
     * @brief Set port/targetWorkerCount
     *
     * @param pp           [in] port to set
     * @param twc          [in] target worker count to set
     */
    bool Set(uint16_t pp, uint16_t twc);

    /**
     * @brief Set the listen ip
     *
     * @param value        [in] the ip to set
     *
     * @return 0 if successful, otherwise it could be the length of value is too large
     */
    NResult Ip(const std::string &value);

    /**
     * @brief Get ip of listening
     */
    std::string Ip() const;
} __attribute__((packed));

/**
 * @brief Oob listening information for multiple listen file
 */
struct UBSHcomNetOobUDSListenerOptions {
    char name[NN_NO96] {}; /* UDS name for listen or file path */
    uint16_t perm = 0600;  /* if 0 means not use file, otherwise use file and this perm as file perm, max is 0600 */
    uint16_t targetWorkerCount = UINT16_MAX; /* the count of target workers, if >= 1, the
                                       accepted socket will be attached to sub set to workers, 0 means all */
    bool isCheck = true;                     /* whether to verify the permission on the UDS file */

    /**
     * @brief Set name/targetWorkerCount
     *
     * @param name         [in] name or file path to set
     * @param twc          [in] target worker count to set
     */
    bool Set(const std::string &pName, uint16_t twc);

    /**
     * @brief Set name for uds oob
     *
     * @param value        [in] the name or file path to set, less than 32
     *
     * @return 0 if successful, otherwise it could be the length of value is too large
     */
    bool Name(const std::string &value);

    /**
     * @brief Get name or file path of listening
     */
    std::string Name() const;
} __attribute__((packed));

/* ****************************************************************************************** */
using UBSHcomNetDriverNewEndPointHandler =
    std::function<int(const std::string &ipPort, const UBSHcomNetEndpointPtr &, const std::string &payload)>;
using UBSHcomNetDriverEndpointBrokenHandler = std::function<void(const UBSHcomNetEndpointPtr &)>;

// the passed context cannot be copy directly need to use SafeClone()
// if needing to transfer to thread to process in async
using UBSHcomNetDriverReceivedHandler = std::function<int(const UBSHcomNetRequestContext &)>;
using UBSHcomNetDriverSentHandler = std::function<int(const UBSHcomNetRequestContext &)>;
using UBSHcomNetDriverOneSideDoneHandler = std::function<int(const UBSHcomNetRequestContext &)>;
using UBSHcomNetDriverIdleHandler = std::function<void(const UBSHcomNetWorkerIndex &)>;

/**
 * @brief During establish TLS connection, we can verify peer cert. There are three types of behaviors:
 * a) don't verify peer certification
 * b) verify peer certification by what hcom provided
 * c) verify peer certification using caller's
 */
enum UBSHcomPeerCertVerifyType : uint8_t {
    VERIFY_BY_NONE = 0,        /* don't verify peer certification */
    VERIFY_BY_DEFAULT = 1,     /* verify peer certification by what hcom provided, crl check and cert check */
    VERIFY_BY_CUSTOM_FUNC = 2, /* verify peer certification using caller's */
};

/**
 * @brief Callback function to erase key pass after used it, for huawei's security policy
 * that "don't store plaintext in memory"
 *
 * @param void*                [in] the address where store key passwd
 * @param int                  [in] the length key passwd
 */
using UBSHcomTLSEraseKeypass = std::function<void(void *, int)>;

/**
 * @brief Callback function to get certification path
 *
 * @param name                 [in] a name for logging
 * @param path                 [out] cert file path
 */
using UBSHcomTLSCertificationCallback = std::function<bool(const std::string &name, std::string &path)>;

/**
 * @brief Callback function to get TLS private key and related things, when establishing a connection
 *
 * @param name                 [in] a name for logging
 * @param path                 [out] path of cert file
 * @param password             [out] key passwd of private key
 * @param length               [out] length of key passwd
 * @param erase                [out] callback function to erase key passwd in memory, which is called just after key
 * passwd is used
 */
using UBSHcomTLSPrivateKeyCallback = std::function<bool(const std::string &name, std::string &path, void *&password,
    int &length, UBSHcomTLSEraseKeypass &erase)>;

/**
 * @brief Customize callback function of verify cert, which is used in UBSHcomTLSCaCallback
 */
using UBSHcomTLSCertVerifyCallback = std::function<int(void *, const char *)>;

/**
 * @brief Callback function of certification check
 *
 * @param name                 [in] a name for logging
 * @param capath               [out] path of ca files, could be multiple files
 * @param crlPath              [out] path of crl file
 * @param verifyPeerCert       [out] cert verification type, none | default_by_hcom | customized, if customized, cb need
 * to be specified
 * @param cb                   [out] callback function of customized function
 */
using UBSHcomTLSCaCallback = std::function<bool(const std::string &name, std::string &capath, std::string &crlPath,
    UBSHcomPeerCertVerifyType &verifyPeerCert, UBSHcomTLSCertVerifyCallback &cb)>;

/**
 * @brief UBSHcomNetDriver secure mode
 */
enum UBSHcomNetDriverSecType : uint8_t {
    NET_SEC_DISABLED = 0,
    NET_SEC_VALID_ONE_WAY = 1,
    NET_SEC_VALID_TWO_WAY = 2,
};

/**
 * @brief Sec callback function, when oob connect build, this function will be called to generate auth info.
 * if this function not set secure type is C_NET_SEC_NO_VALID and oob will not send secure info
 *
 * @param ctx              [in] ctx from connect param ctx, and will send in auth process
 * @param flag             [out] flag to send in auth process
 * @param type             [out] secure type, value should set in oob client, and should in [C_NET_SEC_ONE_WAY,
 * C_NET_SEC_TWO_WAY]
 * @param output           [out] secure info created
 * @param outLen           [out] secure info length
 * @param needAutoFree     [out] secure info need to auto free in hcom or not
 */
using UBSHcomNetDriverEndpointSecInfoProvider = std::function<int(uint64_t ctx, int64_t &flag,
    UBSHcomNetDriverSecType &type, char *&output, uint32_t &outLen, bool &needAutoFree)>;

/**
 * @brief ValidateSecInfo callback function, when oob connect build, this function will be called to validate auth info
 * if this function not set oob will not validate secure info
 *
 * @param ctx              [in] ctx received in auth process
 * @param flag             [in] flag received in auth process
 * @param input            [in] secure info received
 * @param inputLen         [in] secure info length
 */
using UBSHcomNetDriverEndpointSecInfoValidator =
    std::function<int(uint64_t ctx, int64_t flag, const char *input, uint32_t inputLen)>;

/**
 * @brief Callback function of PSK check, set for client
 *
 * @param ssl               [in] SSL connection pointer
 * @param md                [in] digest algorithm
 * @param id                [out] the identity that the client gives to server uses to find the psk
 * @param idlen             [out] the id length
 * @param sess              [out] SSL session
 *
 * @return int              1 on success or 0 on failure
 */
using UBSHcomPskUseSessionCb =
    std::function<int(void *ssl, const void *md, const unsigned char **id, size_t *idlen, void **sess)>;

/**
 * @brief Callback function of PSK check, set for server
 *
 * @param ssl               [in] SSL connection pointer
 * @param identity          [in] Client's identity (provided by the client)
 * @param identityLen      [in] Length of the client's identity
 * @param sess              [out] SSL session
 *
 * @return int              1 on success or 0 on failure
 */
using UBSHcomPskFindSessionCb =
    std::function<int(void *ssl, const unsigned char *identity, size_t identityLen, void **sess)>;

std::string &UBSHcomNetDriverSecTypeToString(UBSHcomNetDriverSecType v);

/**
 * @brief UBSHcomNetDriver working mode
 */
enum NetDriverOobType : uint8_t {
    NET_OOB_TCP = 0,
    NET_OOB_UDS = 1,
    NET_OOB_UB = 2,
};

std::string &UBSHcomNetDriverOobTypeToString(NetDriverOobType v);

/**
 * @brief UBSHcomNetDriver working mode
 */
enum UBSHcomNetDriverWorkingMode : uint8_t {
    NET_BUSY_POLLING = 0,
    NET_EVENT_POLLING = 1,
};

/**
 * @brief UBSHcomNetDriver load balance policy
 */
enum UBSHcomNetDriverLBPolicy : uint8_t {
    NET_ROUND_ROBIN = 0,
    NET_HASH_IP_PORT = 1,
};

std::string &UBSHcomNetDriverLBPolicyToString(UBSHcomNetDriverLBPolicy v);

/// UB-C 专用: UB-C 具有多路径能力，发送时使用多条路径可以增大带宽，对于带宽要求
/// 不高、时延敏感型业务又提供单路径直连模式。
enum class UBSHcomUbcMode : int8_t {
    LowLatency = 0,     ///< 低时延模式，使用单路径发送
    HighBandwidth = 1,  ///< 高带宽模式，使用多条路径发送
};

struct UBSHcomWorkerGroupInfo {
    int8_t threadPriority = 0;  // [-20, 19], 19 is the lowest, -20 is the highest
    uint16_t threadCount = 1;   // total number of threads in the worker group
    uint16_t groupId = 0;   // group id of the worker group
    std::pair<uint32_t, uint32_t> cpuIdsRange;   // worker groups cpu ids range
};

/**
 * @brief UBSHcomNetDriver options
 */
struct UBSHcomNetDriverOptions {
    union {
        char netDeviceIpMask[NN_NO256]{};  // IP 掩码。非 UBC 多路径场景通过此掩码可查找得到实际设备的 IP
        uint8_t netDeviceEid[NN_NO16];     // UB EID (128b). 多路径聚合设备为非 IP 设备，需用户显式指定
    } __attribute__((packed));
    char netDeviceIpGroup[NN_NO1024]{};           // ip group for devices
    bool enableTls = true;                       // enable ssl
    UBSHcomNetDriverSecType secType = NET_SEC_DISABLED;  // security type
    UBSHcomTlsVersion tlsVersion = TLS_1_3;  // tls version, default TLS1.3 (772)
    UBSHcomNetCipherSuite cipherSuite =
            AES_GCM_128;  // if tls enabled can set cipher suite, client and server should same
                          /* worker setting */
    bool dontStartWorkers = false;  // start worker or not
    UBSHcomNetDriverWorkingMode mode =
            NET_BUSY_POLLING;  // worker polling mode, could busy polling or event polling
    char workerGroups[NN_NO64]{};  // worker groups, for example 1,3,3
    char workerGroupsCpuSet
            [NN_NO128]{};  // worker groups cpu set, for example 1-1,2-5,na
    char workerGroupsThreadPriority[NN_NO64] {};  // worker groups thread priority, for example -10,na,9
    // worker thread priority [-20,19], 19 is the lowest, -20 is the highest, 0 (default) means do not set priority
    int workerThreadPriority = 0;
    /* connection attribute */
    NetDriverOobType oobType =
            NET_OOB_TCP;  // oob type, tcp or UDS, UDS cannot accept remote connection
    UBSHcomNetDriverLBPolicy lbPolicy =
            NET_ROUND_ROBIN;  // select worker load balance policy, default round-robin
    uint16_t magic = NN_NO256;  // magic number for c/s connect validation
    uint8_t version = 0;        // program version used by connect validation
                                /* heart beat attribute */
    uint16_t heartBeatIdleTime = NN_NO60;   // heart beat idle time, in seconds
    uint16_t heartBeatProbeTimes = NN_NO7;  // heart beat probe times
    uint16_t heartBeatProbeInterval =
            NN_NO2;  // heart beat probe interval, in seconds
                     /* options for only tcp protocol */
    // timeout during io (s), it should be [-1, 1024], -1 means do not set, 0 means never timeout during io
    int16_t tcpUserTimeout = -1;
    bool tcpEnableNoDelay = true;  // tcp TCP_NODELAY option, true in default
    bool tcpSendZCopy =
            false;  // tcp whether copy request to inner memory, false in default
    /* The buffer sizes will be adjusted automatically when these two variables are 0, and the performance would be
     * better */
    uint16_t tcpSendBufSize =
            0;  // tcp connection send buffer size in kernel, by KB
    uint16_t tcpReceiveBufSize =
            0;  // tcp connection send receive buf size in kernel, by KB
                /* options for rdma protocol only */
    uint32_t mrSendReceiveSegCount =
            NN_NO8192;  // memory region segment count for two side operation
    uint32_t mrSendReceiveSegSize =
            NN_NO1024;  // data size of memory region segment
    /* transmit of 256b data performs better when dmSegSize is 290 */
    uint32_t dmSegSize = NN_NO290;   // data size of device memory segment
    uint32_t dmSegCount = NN_NO400;  // segment count of device memory segment
    uint16_t completionQueueDepth = NN_NO2048;  // completion queue size of rdma
    uint16_t maxPostSendCountPerQP = NN_NO64;  // max number request could issue
    uint16_t prePostReceiveSizePerQP = NN_NO64;  // pre post receive of qp
    uint16_t pollingBatchSize = NN_NO4;  // polling batch size for worker
    uint32_t eventPollingTimeout =
            NN_NO500;  // event polling timeout in ms, max value is 2000000ms
    uint32_t qpSendQueueSize =
            NN_NO256;  // max send working request of qp for rdma
    uint32_t qpReceiveQueueSize =
            NN_NO256;  // max receive working request of qp for rdma
    uint32_t qpBatchRePostSize = NN_NO1; // qp batch return wr size
    uint16_t oobConnHandleThreadCount =
            NN_NO2;  // server accept connection thread num
    uint32_t oobConnHandleQueueCap =
            NN_NO4096;  // server accept connection queue capability
    uint32_t maxConnectionNum = NN_NO250;  // max connection number
    bool enableMultiRail = false;          // enable multi rail
    uint8_t slave = 1;                     // slave 1 or 2

    char oobPortRange[NN_NO16]{};  // port range when enable port auto selection

    UBSHcomUbcMode ubcMode = UBSHcomUbcMode::LowLatency;

    /* verify the common options of each driver */
    NResult ValidateCommonOptions();

    std::string NetDeviceIpMask() const;

    std::string NetDeviceIpGroup() const;

    std::string WorkGroups() const;

    std::string WorkerGroupCpus() const;

    std::string WorkerGroupThreadPriority() const;

    /// 设置设备 IP 掩码以辅助查找得到真实通信设备 IP，与 `SetNetDeviceEid()` 冲突，
    /// 不可同时使用。当前仅支持 IPv4, 格式如下 `192.168.0.1/24`.
    bool SetNetDeviceIpMask(const std::string &mask);

    /// 设置 UB 设备 EID 以辅助查找得到真实通信设备, 与 `SetNetDeviceIpMask()` 冲突，不可
    /// 同时使用。EID 格式类似 IPv6, 为如下格式 `0000:0000:0000:0000:0000:xxxx:0x0x:0x0x`.
    /// 要求在去除冒号后为 16 字节，不可省略每个数字前的前导 0. 通常用户只需要复制
    /// `urma_admin show` 的输出即可。
    bool SetNetDeviceEid(const std::string &eid);

    /**
     * @brief Set the ip mask for net devices
     *
     * @param mask Each element in the mask vector represent an ipmask. e.g. mask = {192.168.0.1/24. 192.168.1.1/24}
     * @return true set success
     * @return false set failed
     */
    bool SetNetDeviceIpMask(const std::vector<std::string> &mask);

    /**
     * @brief Set the ip group for net devices, example: 192.168.0.1;192.168.0.2
     */
    bool SetNetDeviceIpGroup(const std::string &ipGroup);

    /**
     * @brief Set the ip group for net devices
     *
     * @param ipGroup Each element in the ipGroup represent an ip. e.g. ipGroup = {192.168.0.1;192.168.0.2}
     * @return true set success
     * @return false set failed
     */
    bool SetNetDeviceIpGroup(const std::vector<std::string> &ipGroup);

    /**
     * @brief Set worker groups, example: 1,3,4
     * meaning 3 groups for workers:
     * group0 has 1 workers
     * group1 has 3 workers
     * group2 has 4 workers
     */
    bool SetWorkerGroups(const std::string &groups);

    /**
     * @brief Set worker groups, example: 10-10,11-13,na
     * meaning 3 groups for workers:
     * group0 bind to cpu 10
     * group1 bind to cpu 11, 12, 13
     * group2 not bind to cpu
     */
    bool SetWorkerGroupsCpuSet(const std::string &value);

    /**
     * @brief Set worker groups thread priority, example: 10,na,15
     * meaning 3 groups for workers:
     * group0 thread priority 10
     * group1 not set thread priority
     * group2 thread priority 15
     */
    bool SetWorkerGroupThreadPriority(const std::string &value);

    void SetUbcMode(UBSHcomUbcMode m)
    {
        ubcMode = m;
    }
    /**
     * @brief Set the Worker Groups Info by UBSHcomWorkerGroupInfo vector
     *
     * @param workerGroups vector of UBSHcomWorkerGroupInfo, each element represent a worker group config
     * @return true set success
     * @return false set fail
     */
    bool SetWorkerGroupsInfo(const std::vector<UBSHcomWorkerGroupInfo> &workerGroupInfos);

    std::string ToString() const;

    std::string ToStringForSock() const;
} __attribute__((packed));

/**
 * @brief The protocol of driver
 */
enum UBSHcomNetDriverProtocol {
    RDMA = 0,
    TCP = 1,
    UDS = 2,
    SHM = 3,
    UBC = 7,

    UNKNOWN = 255,
};

/**
 * @brief Protocol to string
 */
std::string &UBSHcomNetDriverProtocolToString(UBSHcomNetDriverProtocol v);

/**
 * @brief UBSHcomNetDriver
 */
class UBSHcomNetDriver {
public:
    /**
     * @brief Get a driver instance by name
     *
     * @param t            [in] protocol of this driver
     * @param name         [in] name of driver to be created
     * @param startOobSvr  [in] start oob server or not
     *
     * @return Driver instance is OK, otherwise return nullptr
     */
    static UBSHcomNetDriver *Instance(UBSHcomNetDriverProtocol t, const std::string &name, bool startOobSvr);

    /**
     * @brief Destroy driver instance by name
     *
     * @param name         [in] name of driver to be created
     *
     * @return Destroy driver instance is OK, otherwise return error
     */
    static NResult DestroyInstance(const std::string &name);

    /**
     * @brief Check if local host support certain protocol
     *
     * @param t            [in] protocol
     * @param t            [out] device info
     *
     * @return true is support
     */
    static bool LocalSupport(UBSHcomNetDriverProtocol t, UBSHcomNetDriverDeviceInfo &deviceInfo);

    static bool MultiRailGetDevCount(UBSHcomNetDriverProtocol t, std::string ipMask, uint16_t &enableDevCount,
        std::string ipGroup);

public:
    virtual ~UBSHcomNetDriver()
    {
        OBJ_GC_DECREASE(UBSHcomNetDriver);
    }

    /**
     * @brief Initialize the net driver
     *
     * @param option       [in] option for initialize
     *
     * @return 0 if successful
     */
    virtual NResult Initialize(const UBSHcomNetDriverOptions &option) = 0;

    /**
     * @brief UnInitialize the net driver
     */
    virtual void UnInitialize() = 0;

    /**
     * @brief Start the net driver
     *
     * @return 0 if successful
     */
    virtual NResult Start() = 0;

    /**
     * @brief Stop the net driver
     */
    virtual void Stop() = 0;

    /**
     * @brief Register a memory region, the memory will be allocated internally
     *
     * @param size         [in]  size of the memory region
     * @param mr           [out] memory region registered
     *
     * @return 0 successful
     */
    virtual NResult
    CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr) = 0;

    /**
     * @brief Register a memory region, the memory need to be passed in
     *
     * @param address      [in]  the memory point need to be registered
     * @param size         [in]  size of the memory region
     * @param mr           [out] memory region registered
     *
     * @return 0 successful
     */
    virtual NResult CreateMemoryRegion(uintptr_t address, uint64_t size,
                                       UBSHcomNetMemoryRegionPtr &mr) = 0;

    virtual NResult CreateMemoryRegion(uint64_t size, UBSHcomNetMemoryRegionPtr &mr,
                                       unsigned long memid) = 0;

    virtual NResult ImportUrmaSeg(uintptr_t address, uint64_t size, uint64_t key, void **tSeg, uint8_t *eid,
        uint32_t eidLen)
    {
        NN_LOG_ERROR("ImportUrmaSeg not supported in other protocol, only UBC");
        return NN_ERROR;
    }
    /**
     * @brief Unregister the memory region
     *
     * @param mr           [in] memory region registered
     *
     * @return 0 successful
     */
    virtual void DestroyMemoryRegion(UBSHcomNetMemoryRegionPtr &mr) = 0;

    /**
     * @brief Connect to server with driver's oob ip or uds name
     *
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param flags        [in]  flags
     * @param serverGrpNo  [in]  indicates which client worker group to connect
     * @param clientGrpNo  [in]  indicates which server worker group to connect to
     *
     * @return 0 successful
     */
    virtual NResult Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep,
                            uint32_t flags, uint8_t serverGrpNo,
                            uint8_t clientGrpNo) = 0;

    /**
     * @brief Connect to server with driver's oob ip or uds name
     *
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param serverGrpNo  [in]  indicates which client worker group to connect
     * @param clientGrpNo  [in]  indicates which server worker group to connect to
     *
     * @return 0 successful
     */
    virtual NResult Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep,
                            uint8_t serverGrpNo, uint8_t clientGrpNo)
    {
        return Connect(payload, ep, 0, serverGrpNo, clientGrpNo);
    }

    /**
     * @brief Connect to server with driver's oob ip or uds name
     *
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param flags        [in]  flags
     *
     * @return 0 successful
     */
    virtual NResult
    Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep, uint32_t flags)
    {
        return Connect(payload, ep, flags, 0, 0);
    }

    /**
     * @brief Connect to server with driver's oob ip or uds name
     *
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     *
     * @return 0 successful
     */
    virtual NResult Connect(const std::string &payload, UBSHcomNetEndpointPtr &ep)
    {
        return Connect(payload, ep, 0, 0, 0);
    }

    /**
     * @brief Connect to server
     *
     * @param oobIpOrName  [in]  oob ip or name to connect, set ip for tcp and name for uds
     * @param oobPort      [in]  only need to set when tcp oob
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param flags        [in]  flags
     * @param serverGrpNo  [in]  indicates which client worker group to connect
     * @param clientGrpNo  [in]  indicates which server worker group to connect to
     *
     * @return 0 successful
     */
    NResult Connect(const std::string &oobIpOrName, uint16_t oobPort,
                    const std::string &payload, UBSHcomNetEndpointPtr &ep,
                    uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo)
    {
        return Connect(oobIpOrName, oobPort, payload, ep, flags, serverGrpNo,
                       clientGrpNo, 0);
    };

    /**
     * @brief Connect to server
     *
     * @param serverUrl    [in]  oob url, e.g. tcp://127.0.0.1:9981 or uds://udsName
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param flags        [in]  flags
     * @param serverGrpNo  [in]  indicates which client worker group to connect
     * @param clientGrpNo  [in]  indicates which server worker group to connect to
     * @param ctx          [in]  ctx in upstream
     *
     * @return 0 successful
     */
    virtual NResult Connect(const std::string &serverUrl, const std::string &payload,
        UBSHcomNetEndpointPtr &ep, uint32_t flags, uint8_t serverGrpNo, uint8_t clientGrpNo, uint64_t ctx) = 0;

    /**
     * @brief Connect to server
     *
     * @param oobIpOrName  [in]  oob ip or name to connect, set ip for tcp and name for uds
     * @param oobPort      [in]  only need to set when tcp oob
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param flags        [in]  flags
     * @param serverGrpNo  [in]  indicates which client worker group to connect
     * @param clientGrpNo  [in]  indicates which server worker group to connect to
     * @param ctx          [in]  ctx in upstream
     *
     * @return 0 successful
     */
    virtual NResult Connect(const std::string &oobIpOrName, uint16_t oobPort,
                            const std::string &payload, UBSHcomNetEndpointPtr &ep,
                            uint32_t flags, uint8_t serverGrpNo,
                            uint8_t clientGrpNo, uint64_t ctx) = 0;

    /**
     * @brief Connect to server
     *
     * @param oobIpOrName  [in]  oob ip or name to connect, set ip for tcp and name for uds
     * @param oobPort      [in]  only need to set when tcp oob
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param serverGrpNo  [in]  indicates which client worker group to connect
     * @param clientGrpNo  [in]  indicates which server worker group to connect to
     *
     * @return 0 successful
     */
    inline NResult Connect(const std::string &oobIpOrName, uint16_t oobPort,
                           const std::string &payload, UBSHcomNetEndpointPtr &ep,
                           uint8_t serverGrpNo, uint8_t clientGrpNo)
    {
        return Connect(oobIpOrName, oobPort, payload, ep, 0, serverGrpNo,
                       clientGrpNo);
    }

    /**
     * @brief Connect to server
     *
     * @param oobIpOrName  [in]  oob ip or name to connect, set ip for tcp and name for uds
     * @param oobPort      [in]  only need to set when tcp oob
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     * @param flags        [in]  flags
     *
     * @return 0 successful
     */
    virtual NResult Connect(const std::string &oobIpOrName, uint16_t oobPort,
                            const std::string &payload, UBSHcomNetEndpointPtr &ep,
                            uint32_t flags)
    {
        return Connect(oobIpOrName, oobPort, payload, ep, flags, 0, 0);
    }

    /**
     * @brief Connect to server
     *
     * @param oobIpOrName  [in]  oob ip or name to connect, set ip for tcp and name for uds
     * @param oobPort      [in]  only need to set when tcp oob
     * @param payload      [in]  payload transferred to peer, could be got EP Connected callback at server
     * @param ep           [out] connected end point
     *
     * @return 0 successful
     */
    virtual NResult Connect(const std::string &oobIpOrName, uint16_t oobPort,
                            const std::string &payload, UBSHcomNetEndpointPtr &ep)
    {
        return Connect(oobIpOrName, oobPort, payload, ep, 0, 0, 0);
    }

    virtual NResult MultiRailNewConnection(OOBTCPConnection &conn) = 0;

    /**
     * @brief Destroy the endpoint
     *
     * @param ep           [in] the end point to destroy
     */
    virtual void DestroyEndpoint(UBSHcomNetEndpointPtr &ep) = 0;

    /**
     * @brief Set out of bound ip and port
     *
     * @param ip           [in]  ip address
     * @param port         [out] port
     */
    void OobIpAndPort(const std::string &ip, uint16_t port);

    /**
     * @brief Set out of bound eid and jetty id used in public jetty
     *
     * @param eid           [in] public jetty eid
     * @param id            [in] public jetty id
     */
    void OobEidAndJettyId(const std::string &eid, uint16_t id);

    /**
     * @brief Get out of bound ip and port
     */
    bool GetOobIpAndPort(std::vector<std::pair<std::string, uint16_t>> &result);

    /**
     * @brief Add multiple oob listeners, if there is only one listener just use OobIpAndPort
     *
     * @param option       [in] listen options
     */
    void AddOobOptions(const UBSHcomNetOobListenerOptions &option);

    /**
     * @brief Set oob listener of uds type
     *
     * @param name         [in] name of uds listener
     *
     */
    void OobUdsName(const std::string &name);

    /**
     * @brief Add multiple oob uds listeners, if there is only one listener just use OobUdsName
     *
     * @param option       [in] option of uds listener option
     *
     */
    void AddOobUdsOptions(const UBSHcomNetOobUDSListenerOptions &option);

    /**
     * @brief Register callback for new end point connected from client, only need to register at server side
     *
     * @param handler      [in] handler function
     */
    void RegisterNewEPHandler(const UBSHcomNetDriverNewEndPointHandler &handler);

    /**
     * @brief Register callback for end point broken
     *
     * @param handler      [in] handler function
     */
    void RegisterEPBrokenHandler(const UBSHcomNetDriverEndpointBrokenHandler &handler);

    /**
     * @brief Register callback for new request from peer
     *
     * @param handler      [in] handler function
     */
    void RegisterNewReqHandler(const UBSHcomNetDriverReceivedHandler &handler);

    /**
     * @brief Register callback for request posted to peer (send/read/write etc)
     *
     * @param handler      [in] handler function
     */
    void RegisterReqPostedHandler(const UBSHcomNetDriverSentHandler &handler);

    /**
     * @brief Register callback for one side operation done
     *
     * @param handler      [in] handler function
     */
    void RegisterOneSideDoneHandler(const UBSHcomNetDriverOneSideDoneHandler &handler);

    /**
     * @brief Register callback for idle
     *
     * @param handler      [in] handler function
     */
    void RegisterIdleHandler(const UBSHcomNetDriverIdleHandler &handler);

    /**
     * @brief Register callback for idle
     *
     * @param handler      [in] handler function
     */
    void RegisterTLSCaCallback(const UBSHcomTLSCaCallback &cb);

    /**
     * @brief Register callback for idle
     *
     * @param handler      [in] handler function
     */
    void RegisterTLSCertificationCallback(const UBSHcomTLSCertificationCallback &cb);

    /**
     * @brief Register callback for idle
     *
     * @param handler      [in] handler function
     */
    void RegisterTLSPrivateKeyCallback(const UBSHcomTLSPrivateKeyCallback &cb);

    /**
     * @brief Register callback for create secure info
     *
     * @param provider      [in] provider function
     */
    void RegisterEndpointSecInfoProvider(
            const UBSHcomNetDriverEndpointSecInfoProvider &provider);

    /**
     * @brief Register callback for validate secure info from peer
     *
     * @param validator      [in] validator function
     */
    void RegisterEndpointSecInfoValidator(
            const UBSHcomNetDriverEndpointSecInfoValidator &validator);

    /**
     * @brief Register psk callback for client
     *
     * @param cb             [in] psk use session callback
     */
    void RegisterPskUseSessionCb(const UBSHcomPskUseSessionCb &cb);

    /**
     * @brief Register psk callback for server
     *
     * @param cb             [in] psk find session callback
     */
    void RegisterPskFindSessionCb(const UBSHcomPskFindSessionCb &cb);

    /**
     * @brief Get the name of driver
     */
    const std::string &Name() const;

    uint8_t GetId() const;

    /**
     * @brief Get the protocol of driver
     */
    UBSHcomNetDriverProtocol Protocol() const;

    /**
     * @brief Get the result indicates whether driver is stopped
     */
    bool IsStarted() const;

    /**
     * @brief Get the result indicates whether driver is inited
     */
    bool IsInited() const;

    static void DumpObjectStatistics();

    void SetPeerDevId(uint8_t index);

    uint8_t GetPeerDevId() const;

    inline void SetDeviceId(uint8_t index)
    {
        mDevIndex = index;
    }

    inline uint8_t GetDeviceId() const
    {
        return mDevIndex;
    }

    uint8_t GetBandWidth() const;

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

protected:
    UBSHcomNetDriver(const std::string &name, bool startOobSvr,
              UBSHcomNetDriverProtocol protocol)
        : mName(name), mStartOobSvr(startOobSvr), mProtocol(protocol)
    {
        OBJ_GC_INCREASE(UBSHcomNetDriver);
    }

protected:
    NResult CreateListeners(bool enableMultiRail = false);
    NResult CreateUdsListeners();
    NResult CreateServerLB();
    NResult StartListeners();
    NResult StopListeners(bool clear = true);

    NResult CreateClientLB();
    void DestroyClientLB();

    NResult ValidateAndParseOobPortRange(const char *oobPortRange);
    // tcp://127.0.0.1:9981 or uds://name
    NResult ParseUrl(const std::string &url, NetDriverOobType &type, std::string &ip, uint16_t &port);

    static NResult ValidateKunpeng();

    NResult ValidateHandlesCheck();

    NResult ValidateOptionsOobType();

protected:
    std::mutex mInitMutex;

    bool mStarted = false;
    UBSHcomNetDriverOptions mOptions;

    std::string mOobIp;
    uint16_t mOobPort = 0;
    std::string mUdsName;
    uint8_t mIndex = 0;
    uint8_t mPeerDevIndex = 0;
    uint16_t mDevIndex = 0;
    uint8_t mBandWidth = 0;
    std::string mEid;
    std::pair<uint16_t, uint16_t> mPortRange{0, 0};

    // hot used variables for start
    std::string mName;
    bool mStartOobSvr = true;
    UBSHcomNetDriverProtocol mProtocol = UBSHcomNetDriverProtocol::RDMA;
    bool mEnableTls = true;
    uint32_t mMajorVersion = NN_NO1;
    uint32_t mMinorVersion = 0;
    std::atomic_bool mInited{false};

    UBSHcomNetDriverReceivedHandler mReceivedRequestHandler = nullptr;
    UBSHcomNetDriverSentHandler mRequestPostedHandler = nullptr;
    UBSHcomNetDriverOneSideDoneHandler mOneSideDoneHandler = nullptr;

    UBSHcomNetDriverIdleHandler mIdleHandler = nullptr;

    UBSHcomNetDriverNewEndPointHandler mNewEndPointHandler = nullptr;
    UBSHcomNetDriverEndpointBrokenHandler mEndPointBrokenHandler = nullptr;

    std::mutex mEndPointsMutex;
    std::unordered_map<uint64_t, UBSHcomNetEndpointPtr> mEndPoints;

    NetWorkerLB *mClientLb = nullptr;
    NetWorkerLB *mServerLb = nullptr;

    std::vector<NetOOBServer *> mOobServers;
    std::vector<std::pair<uint16_t, uint16_t>> mWorkerGroups;

    UBSHcomTLSPrivateKeyCallback mTlsPrivateKeyCB = nullptr;
    UBSHcomTLSCertificationCallback mTlsCertCB = nullptr;
    UBSHcomTLSCaCallback mTlsCaCallback = nullptr;

    UBSHcomNetDriverEndpointSecInfoProvider mSecInfoProvider = nullptr;
    UBSHcomNetDriverEndpointSecInfoValidator mSecInfoValidator = nullptr;

    UBSHcomPskFindSessionCb mPskFindSessionCb = nullptr;
    UBSHcomPskUseSessionCb mPskUseSessionCb = nullptr;

    std::vector<UBSHcomNetOobListenerOptions> mOobListenOptions;
    std::unordered_map<std::string, UBSHcomNetOobUDSListenerOptions>
            mOobUdsListenOptions;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

private:
    static uint32_t gMaxListenPort;
    static uint8_t gDriverIndex;
    static std::mutex gDriverMapMutex;
    static std::map<std::string, UBSHcomNetDriver *> gDriverMap;
    static int32_t gOSMaxFdCount; // number of file descriptors that can be opened by each user process
    friend class NetHeartbeat;
};

inline const std::string &UBSHcomNetDriver::Name() const
{
    return mName;
}

inline uint8_t UBSHcomNetDriver::GetId() const
{
    return mIndex;
}

inline UBSHcomNetDriverProtocol UBSHcomNetDriver::Protocol() const
{
    return mProtocol;
}

inline bool UBSHcomNetDriver::IsStarted() const
{
    return mStarted;
}

inline bool UBSHcomNetDriver::IsInited() const
{
    return mInited;
}

inline void UBSHcomNetDriver::SetPeerDevId(uint8_t index)
{
    mPeerDevIndex = index;
}

inline uint8_t UBSHcomNetDriver::GetPeerDevId() const
{
    return mPeerDevIndex;
}

inline uint8_t UBSHcomNetDriver::GetBandWidth() const
{
    return mBandWidth;
}
}  // namespace hcom
}  // namespace ock

#endif  // OCK_HCOM_CPP_H_34562
