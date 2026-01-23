#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <cstdint>
#include <atomic>
#include <sys/types.h>  // for size_t
#include <sys/uio.h>    // for struct iovec
#ifdef UBS_SHM_BUILD_ENABLED

namespace Brpc {

// epoll 风格的事件标志
constexpr uint32_t RING_EPOLLIN  = 0x001;  // 可读事件
constexpr uint32_t RING_EPOLLOUT = 0x004;  // 可写事件
constexpr uint32_t RING_EPOLLET  = 0x80000000;  // 边缘触发模式

struct AddrInfo {
    AddrInfo() = default;
    AddrInfo(void *ad, size_t length) : addr(ad), len(length) {}

    void *addr = nullptr;
    size_t len = 0;
};
struct RingBufferOpt {
    uint32_t updateThreshold = 8;  // 累计多少个 element 后更新远程 tail
};

// Ring元素大小常量
constexpr uint32_t RING_ELEMENT_SIZE = 64;      // 4 字节包头 + 60 字节数据
constexpr uint32_t RING_DATA_SIZE = 60;         // 实际数据区大小
constexpr uint32_t RING_HEADER_SIZE = 4;        // 包头大小
    
// Element 结构
struct Element {
    uint8_t data[RING_DATA_SIZE];   // 数据内容
    uint8_t hasData;                // 状态标志（单字节读写天然原子）
    uint8_t dataLen;                // 实际数据长度 (1-60)
    uint8_t readOffset;             // 已读取字节数 (0-60)
    uint8_t reserved;               // 保留
} __attribute__((aligned(64)));     // cache line 对齐

class RingBuffer {
public:
    RingBuffer(const RingBufferOpt &opts, const AddrInfo &localShm, const AddrInfo &remoteShm);

    ~RingBuffer()
    {
        if (mTxElements != nullptr) {
            delete[] mTxElements;
            mTxElements = nullptr;
        }
        if (mRxElements != nullptr) {
            delete[] mRxElements;
            mRxElements = nullptr;
        }
    }
    
    // 初始化共享内存（只在首次创建时调用，重新连接不应调用）
    void InitSharedMemory();
    
    // 内部辅助函数：初始化 element 指针数组
    void InitElementPointers();
    
    // 快速获取 element 指针（内联）
    inline void* GetTxElement(uint32_t index) const
    {
        return mTxElements[index % mTxElemCount];
    }
    
    inline void* GetRxElement(uint32_t index) const
    {
        return mRxElements[index % mRxElemCount];
    }
    
    size_t Send(const void *buf, uint32_t bufLen);
    size_t Recv(void *buf, uint32_t bufLen);
    size_t WriteV(const struct iovec *iov, int iovCnt);
    size_t ReadV(const struct iovec *iov, int iovCnt);

    int GetWriteEvent(uint32_t event);
    int GetReadEvent(uint32_t event);

private:
    // shm used at send
    AddrInfo mTxTail;  // 读取：对端consumer更新的tail（生产者读取此值判断可写空间）
    AddrInfo mTxAddr;  // 写入：本地producer写入数据的buffer

    // shm used at recv
    AddrInfo mRxTail;  // 写入：更新到对端producer的tail（告诉对端我消费到哪了）
    AddrInfo mRxAddr;  // 读取：本地consumer从这里读取数据

    // local variable used at send
    std::atomic<uint32_t> mProdHead;
    std::atomic<uint32_t> mProdTail;

    // local variable used at recv
    uint32_t mCons;
    uint32_t mConsCnt;
    uint32_t mUpdateLim; // consCnt >= updateLim, write TxTail

    // local variable used at epoll_wait
    uint32_t mEpLastCap; // GetWritableCap
    uint32_t mEpEofPos; // GetReadableCap
    
    // 预先计算的 element 指针数组（优化性能）
    void **mTxElements;     // 发送端 element 指针数组
    void **mRxElements;     // 接收端 element 指针数组
    uint32_t mTxElemCount;  // 发送端 element 数量
    uint32_t mRxElemCount;  // 接收端 element 数量
    uint32_t mTxSizeMask;   // Tx 端 size - 1，用于位运算取模
    uint32_t mRxSizeMask;   // Rx 端 size - 1，用于位运算取模
};

}
#endif // UBS_SHM_BUILD_ENABLED
#endif // RING_BUFFER_H