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
#ifndef OCK_HCOM_OOB_1233432457233_H
#define OCK_HCOM_OOB_1233432457233_H

#include <arpa/inet.h>
#include <cstdint>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "hcom.h"
#include "hcom_def.h"
#include "net_common.h"
#include "net_execution_service.h"
#include "net_load_balance.h"
#include "net_monotonic.h"
#include "net_util.h"
#include "rdma_verbs_wrapper_qp.h"
#include "securec.h"

namespace ock {
namespace hcom {
constexpr uint64_t MAX_CB_TIME_US = NN_NO1000000; // 1s
constexpr uint32_t DEFAULT_CONN_THREAD_NUM = NN_NO2;
constexpr uint32_t DEFAULT_CONN_THREAD_QUEUE_CAP = NN_NO4096;
union ConnectHeader {
    struct {
        uint64_t magic : 16;
        uint64_t version : 8;
        uint64_t groupIndex : 8;
        uint64_t protocol : 8;
        uint64_t bandWidth : 8;
        uint64_t devIndex : 8;
        uint64_t majorVersion : 8;
        uint64_t minorVersion : 8;
        uint64_t tlsVersion : 16;
        uint64_t reserve : 40;
    };
    uint64_t wholeHeader[2] = {0};
};

inline void SetConnHeader(ConnectHeader &h, uint32_t magic, uint32_t version, uint32_t groupIndex, uint32_t protocol,
    uint32_t majorVersion, uint32_t minorVersion, uint32_t tlsVersion)
{
    h.magic = magic;
    h.version = version;
    h.groupIndex = groupIndex;
    h.protocol = protocol;
    h.majorVersion = majorVersion;
    h.minorVersion = minorVersion;
    h.tlsVersion = tlsVersion;
}


inline void SetDriverConnHeader(ConnectHeader &h, uint8_t bandWidth, uint8_t devIndex)
{
    h.bandWidth = bandWidth;
    h.devIndex = devIndex;
}

enum class ConnectState : int8_t {
    DISCONNECTED,
    CONNECTED,
};

/*
 * @brief oob connection response
 *
 * =0 means no error
 * >1 means no error and use this protocol for further processing
 * <0 means error
 */
enum ConnectResp : int16_t {
    OK_PROTOCOL_TCP = 2, /* tell client using tcp socket to connect real worker */
    OK_PROTOCOL_UDS = 1, /* tell client using uds to connect real worker */
    OK = 0,
    MAGIC_MISMATCH = -1,
    VERSION_MISMATCH = -2,
    WORKER_GRPNO_MISMATCH = -3,
    WORKER_NOT_STARTED = -4,
    PROTOCOL_MISMATCH = -5,
    SERVER_INTERNAL_ERROR = -6,
    CONN_ACCEPT_NEW_TASK_FAIL = -7,
    CONN_ACCEPT_QUEUE_FULL = -8,
    SEC_VALID_FAILED = -9,
    TLS_VERSION_MISMATCH = -10,
};

struct ConnRespWithUId {
    ConnectResp connResp = OK;
    uint64_t epId = 0;

    ConnRespWithUId() = default;

    ConnRespWithUId(ConnectResp resp, uint64_t uid) : connResp(resp), epId(uid) {}

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << "connResp = " << std::to_string(connResp) << ", epId = " << epId;
        return oss.str();
    }
} __attribute__((packed));

struct ConnSecHeader {
    int64_t flag = 0;
    uint64_t ctx = 0;
    uint32_t secInfoLen = 0;
    uint8_t type = 0;

    ConnSecHeader() = default;
    ConnSecHeader(int64_t flag, uint64_t ctx, uint32_t len, uint8_t type)
        : flag(flag), ctx(ctx), secInfoLen(len), type(type){};
};

struct OOBServerIndex {
    uint8_t driverIdx = 0;
    uint16_t oobSvrIdx = 0;

    OOBServerIndex() = default;

    OOBServerIndex(uint8_t driverIndex, uint16_t oobIndex) : driverIdx(driverIndex), oobSvrIdx(oobIndex) {}

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << std::to_string(driverIdx) << "-" << oobSvrIdx;
        return oss.str();
    }
};

class OOBTCPConnection;

class OOBTCPServer {
public:
    using NewConnectionHandler = std::function<int(OOBTCPConnection &)>;

    OOBTCPServer(const std::string &ip, uint16_t port) : OOBTCPServer(NET_OOB_TCP, ip, port) {}

    OOBTCPServer(NetDriverOobType t, const std::string &ipOrName, uint16_t portOrPerm) : mOobType(t)
    {
        if (mOobType == NET_OOB_TCP) {
            mListenIP = ipOrName;
            mListenPort = portOrPerm;
        } else if (mOobType == NET_OOB_UDS) {
            mUdsName = ipOrName;
            mUdsPerm = portOrPerm;
        }
    }

    OOBTCPServer(NetDriverOobType t, const std::string &ipOrName, uint16_t portOrPerm, bool isCheck) : mOobType(t)
    {
        if (mOobType == NET_OOB_TCP) {
            mListenIP = ipOrName;
            mListenPort = portOrPerm;
        } else if (mOobType == NET_OOB_UDS) {
            mUdsName = ipOrName;
            mUdsPerm = portOrPerm;
            mCheckUdsPerm = isCheck;
        }
    }

    virtual ~OOBTCPServer()
    {
        (void)Stop();

        if (mEs != nullptr) {
            mEs->Stop();
        }

        if (mWorkerLb != nullptr) {
            mWorkerLb->DecreaseRef();
            mWorkerLb = nullptr;
        }
    }

    inline void SetNewConnCB(const NewConnectionHandler &handler)
    {
        mNewConnectionHandler = handler;
    }

    inline void SetWorkerLb(NetWorkerLB *lb)
    {
        if (lb != nullptr) {
            mWorkerLb = lb;
            mWorkerLb->IncreaseRef();
        }
    }

    inline void SetMultiRail(bool flags)
    {
        enableMultiRail = flags;
    }

    inline void SetNewConnCbThreadNum(uint16_t threadNum)
    {
        mNewConnCbThreadNum = threadNum;
    }

    inline void SetNewConnCbQueueCap(uint32_t queueCap)
    {
        mNewConnCbQueueCap = queueCap;
    }

    NResult EnableAutoPortSelection(uint16_t minPort, uint16_t maxPort);
    NResult GetListenPort(uint16_t &port);
    NResult GetListenIp(std::string &ip);
    NResult GetUdsName(std::string &udsName);

    NResult Start();
    NResult Stop();

    inline NetDriverOobType OobType() const
    {
        return mOobType;
    }

    inline void Index(const OOBServerIndex &value)
    {
        mIndex = value;
    }

    inline NResult CompareEpNum(const std::string &ip)
    {
        auto iter = mIpEpNumberMap.find(ip);
        if (iter == mIpEpNumberMap.end()) {
            return NN_OK;
        }

        if (iter->second >= mMaxConnectionNum) {
            return NN_ERROR;
        }

        return NN_OK;
    }

    inline void AddEpNum(const std::string &ip)
    {
        std::lock_guard<std::mutex> guard(mEpNumMutex);
        auto iter = mIpEpNumberMap.find(ip);
        if (iter == mIpEpNumberMap.end()) {
            mIpEpNumberMap[ip] = 1;
        } else {
            mIpEpNumberMap[ip] = mIpEpNumberMap[ip] + 1;
        }
    }

    inline void DelEpNum(const std::string &ip)
    {
        std::lock_guard<std::mutex> guard(mEpNumMutex);
        auto iter = mIpEpNumberMap.find(ip);
        if (iter != mIpEpNumberMap.end()) {
            mIpEpNumberMap[ip] = mIpEpNumberMap[ip] - 1;

            if (iter->second == 0) {
                mIpEpNumberMap.erase(ip);
            }
        }
    }

    inline void SetMaxConntionNum(uint32_t maxConnectionNum)
    {
        mMaxConnectionNum = maxConnectionNum;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

protected:
    virtual void RunInThread();

    NResult StartForUds();

    virtual void DealConnectInThread(int fd, struct sockaddr_in addressIn);

protected:
    NetDriverOobType mOobType = NET_OOB_TCP;        /* listen type TCP or UDS */
    std::string mListenIP;                          /* listen ip for tcp listener */
    uint16_t mListenPort = OOB_DEFAULT_LISTEN_PORT; /* listen port for tcp listener */
    bool mIsAutoPortSelectionEnabled = false;       /* whether auto port selection is enabled or not, for tcp only */
    uint16_t mMinListenPort = 0;                    /* min port number when enable auto port selection */
    uint16_t mMaxListenPort = 0;                    /* max port number when enable auto port selection */
    std::string mUdsName;                           /* listen name of UDS listener */
    uint16_t mUdsPerm = 0;                          /* perm of uds file, if 0 means don't use file */
    bool mCheckUdsPerm = true;                      /* whether to verify the permission on the UDS file */

    OOBServerIndex mIndex {};
    std::thread mAcceptThread;
    bool mStarted = false;
    std::atomic<bool> mThreadStarted { false };
    volatile bool mNeedStop = false;
    int mListenFD = -1;

    NewConnectionHandler mNewConnectionHandler = nullptr;
    NetWorkerLB *mWorkerLb = nullptr;
    NetExecutorServicePtr mEs;
    uint16_t mNewConnCbThreadNum = DEFAULT_CONN_THREAD_NUM;
    uint32_t mNewConnCbQueueCap = DEFAULT_CONN_THREAD_QUEUE_CAP;
    uint32_t mMaxConnectionNum = NN_NO250;
    std::mutex mEpNumMutex;
    std::map<std::string, uint16_t> mIpEpNumberMap;
    bool enableMultiRail = false;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

private:
    NResult AssignUdsAddress(sockaddr_un &address, socklen_t &addressLen);
    NResult CreateAndStartSocket();
    NResult CreateAndConfigSocket(int &socketFD);
    NResult BindAndListenCommon(int socketFD);
    NResult BindAndListenAuto(int &socketFD);
};
using NetOOBServerPtr = NetRef<OOBTCPServer>;

class OOBTCPClient {
public:
    OOBTCPClient(const std::string &ip, uint32_t port) : OOBTCPClient(NET_OOB_TCP, ip, port) {}

    OOBTCPClient(NetDriverOobType t, const std::string &ipOrName, uint32_t port) : mOobType(t)
    {
        if (mOobType == NET_OOB_TCP) {
            mServerIP = ipOrName;
            mServerPort = port;
        } else if (mOobType == NET_OOB_UDS) {
            mServerUdsName = ipOrName;
        }
    }

    virtual ~OOBTCPClient() = default;

    virtual inline NResult Connect(OOBTCPConnection *&conn)
    {
        if (mOobType == NET_OOB_TCP) {
            return Connect(mServerIP, mServerPort, conn);
        } else if (mOobType == NET_OOB_UDS) {
            return Connect(mServerUdsName, conn);
        }

        return NN_ERROR;
    }

    inline const std::string& GetServerIp() const
    {
        return mServerIP;
    }

    inline uint32_t GetServerPort() const
    {
        return mServerPort;
    }

    inline const std::string& GetServerUdsName() const
    {
        return mServerUdsName;
    }

    inline NetDriverOobType GetOobType() const
    {
        return mOobType;
    }

    /*
     * @brief for tcp
     */
    virtual NResult Connect(const std::string &ip, uint32_t port, OOBTCPConnection *&conn);
    static NResult ConnectWithFd(const std::string &ip, uint32_t port, int &fd);

    /*
     * @brief for uds
     */
    virtual NResult Connect(const std::string &udsName, OOBTCPConnection *&);
    static NResult ConnectWithFd(const std::string &filename, int &fd);

    DEFINE_RDMA_REF_COUNT_FUNCTIONS
protected:
    NetDriverOobType mOobType = NET_OOB_TCP;
    std::string mServerIP;
    uint32_t mServerPort = OOB_DEFAULT_LISTEN_PORT;
    std::string mServerUdsName;

    DEFINE_RDMA_REF_COUNT_VARIABLE;
private:
    static void ConfigureSocketTimeouts(int &tmpFD, long &maxConnRetryTimes, long &maxConnRetryInterval);
};
using OOBTCPClientPtr = NetRef<OOBTCPClient>;

class OOBTCPConnection {
public:
    explicit OOBTCPConnection(int fd) : mFD(fd) {}
    virtual ~OOBTCPConnection();

    virtual NResult Send(void *buf, uint32_t size) const;
    virtual NResult Receive(void *buf, uint32_t size) const;

    NResult SendMsg(msghdr msg, uint32_t size) const;
    NResult ReceiveMsg(msghdr msg, uint32_t size) const;

    inline void SetIpAndPort(const std::string &ip, uint32_t port)
    {
        mIpAndPort = ip + ":" + std::to_string(port);
    }

    inline const std::string &GetIpAndPort() const
    {
        return mIpAndPort;
    }

    inline void ListenPort(uint32_t port)
    {
        mListenPort = port;
    }

    inline uint32_t ListenPort() const
    {
        return mListenPort;
    }

    inline void SetUdsName(std::string udsName)
    {
        mUdsName = udsName;
    }

    inline const std::string &GetUdsName() const
    {
        return mUdsName;
    }

    inline void LoadBalancer(const NetWorkerLBPtr &lb)
    {
        mLb = lb;
    }

    inline const NetWorkerLBPtr &LoadBalancer() const
    {
        return mLb;
    }

    inline bool IsUDS() const
    {
        return mIsUds;
    }

    /*
     * @brief transfer this oob tcp connection to real connection
     */
    inline int TransferFd()
    {
        auto tmp = mFD;
        mFD = -1;
        return tmp;
    }

    inline int GetFd() const
    {
        return mFD;
    }

    DEFINE_RDMA_REF_COUNT_FUNCTIONS

protected:
    int mFD = -1;
    uint32_t mListenPort = 0;
    std::string mIpAndPort;
    NetWorkerLBPtr mLb = nullptr;
    bool mIsUds = false;
    std::string mUdsName;

    DEFINE_RDMA_REF_COUNT_VARIABLE;

    friend class OOBTCPClient;
    friend class OOBTCPServer;
    friend class OOBSSLServer;
    friend class OOBSSLClient;
    friend class ConnectCbTask;
    friend class TlsConnectCbTask;
};

class ConnectCbTask : public NetRunnable {
public:
    using NewConnectionHandler = std::function<int(OOBTCPConnection &)>;

    ConnectCbTask(const NewConnectionHandler &cb, int fd, const NetWorkerLBPtr &workerLb)
        : mNewConnectionHandler(cb), mFd(fd), mWorkerLb(workerLb)
    {}

    void SetIpPort(const std::string &clientIp, uint32_t clientPort, uint32_t serverPort)
    {
        mClientIP = clientIp;
        mClientPort = clientPort;
        mListenPort = serverPort;
    }

    void SetUdsName(const std::string &udsName)
    {
        mUdsName = udsName;
    }

    ~ConnectCbTask() override
    {
        NetFunc::NN_SafeCloseFd(mFd);
    }

    void Run() override
    {
        ConnectResp resp = ConnectResp::OK;
        if (::send(mFd, &resp, sizeof(ConnectResp), 0) <= 0) {
            char buf[NET_STR_ERROR_BUF_SIZE] = {0};
            NN_LOG_ERROR("Failed to send connect status to peer on oob @ " << mClientIP << ":" << mClientPort <<
                ", as " << NetFunc::NN_GetStrError(errno, buf, NET_STR_ERROR_BUF_SIZE));
            return;
        }

        // ConnectCbTask holds and is responsible for closing fd.
        // At the end of the execution, OOBTCPConnection returns fd to ConnectCbTask.
        OOBTCPConnection conn(mFd);
        conn.SetIpAndPort(mClientIP, mClientPort);
        conn.ListenPort(mListenPort);
        conn.LoadBalancer(mWorkerLb);
        conn.SetUdsName(mUdsName);

        if (NN_UNLIKELY(mNewConnectionHandler == nullptr)) {
            NN_LOG_ERROR("Failed to handshake and exchange address as new connection handler is null");
            return;
        }

        auto startConnCb = NetMonotonic::TimeUs();
        auto result = mNewConnectionHandler(conn);
        if (result != 0) {
            mFd = conn.TransferFd();
            NN_LOG_ERROR("Failed to handshake and exchange address with client " << conn.GetIpAndPort() << ",result:" <<
                result << " continue to accept future connection");
            return;
        }
        auto endConnCb = NetMonotonic::TimeUs();
        auto cbTime = endConnCb - startConnCb;
        if (NN_UNLIKELY(cbTime > MAX_CB_TIME_US)) {
            NN_LOG_WARN("Call new Connection Cb time is too long: " << cbTime << " us.");
        }
        /* the socket could be transfer to real connection when type is socket */
        mFd = conn.TransferFd();
    }

protected:
    NewConnectionHandler mNewConnectionHandler = nullptr; /* new connection handler */
    int mFd = -1;                                         /* new oob connection file descriptor */
    std::string mClientIP;                                /* ip of connector */
    uint32_t mClientPort = 0;                             /* port of connector */
    uint32_t mListenPort = 0;                             /* listener port */
    std::string mUdsName;
    NetWorkerLBPtr mWorkerLb = nullptr;                   /* load balancer of worker */
};

}
}

#endif // OCK_HCOM_OOB_1233432457233_H
