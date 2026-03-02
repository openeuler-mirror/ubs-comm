#include "ring_buffer.h"
#include <sys/uio.h>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <cstdio>
#include "rpc_adpt_vlog.h"
#include "sys/epoll.h"
#ifdef UBS_SHM_BUILD_ENABLED

namespace Brpc {
    // Element 状态
    constexpr uint8_t ELEMENT_EMPTY = 0;            // 空闲
    constexpr uint8_t ELEMENT_DATA = 1;             // 有数据（消息的中间部分）
    constexpr uint8_t ELEMENT_EOF = 2;              // 有数据且是消息的最后一个fragment
    constexpr uint8_t COPY_ALIGNED_DATA_BYTES = 64; // 硬件拷贝指令，拷贝长度
    // 自旋等待的最大尝试次数
    constexpr uint32_t MAX_SPIN_ITERATIONS = 100;
    
    // 本地元素缓冲区（避免每次函数调用都分配栈空间）
    thread_local Element mLocalElem;
    
    // 原子操作辅助函数
    inline uint32_t AtomicLoadAcquire32(volatile uint32_t *addr)
    {
        return __atomic_load_n(addr, __ATOMIC_ACQUIRE);
    }
    
    inline void AtomicStoreRelease32(volatile uint32_t *addr, uint32_t val)
    {
        __atomic_store_n(addr, val, __ATOMIC_RELEASE);
    }
    
    // YZH_OK
    // 环形缓冲区辅助函数
    // size: Ring中element的数量
    inline uint32_t RingDistance(uint32_t head, uint32_t tail, uint32_t size)
    {
        return (head - tail) & (size - 1);
    }

    // 计算空闲 element 数量
    inline uint32_t RingFreeElements(uint32_t head, uint32_t tail, uint32_t size)
    {
        return (tail + size - head - 1) % size;
    }
    
    // 字节转换为element数量（向上取整）
    inline uint32_t BytesToElements(uint32_t bytes)
    {
        return (bytes + RING_DATA_SIZE - 1) / RING_DATA_SIZE;
    }
    
    // 获取 Element 指针
    inline Element* GetElement(void *ringBuf, uint32_t index, uint32_t size)
    {
        uint32_t physicalIdx = index % size;
        return reinterpret_cast<Element*>(
            static_cast<uint8_t*>(ringBuf) + physicalIdx * RING_ELEMENT_SIZE);
    }

    static inline int Copy64Byte(int8_t *dst, int8_t *src)
    {
    #ifdef LS64
        asm volatile (
            "mov x12, %0\n"
            "mov x13, %1\n"
            "ldr x4, [x12]\n"
            "ldr x5, [x12, #8]\n"
            "ldr x6, [x12, #16]\n"
            "ldr x7, [x12, #24]\n"
            "ldr x8, [x12, #32]\n"
            "ldr x9, [x12, #40]\n"
            "ldr x10, [x12, #48]\n"
            "ldr x11, [x12, #56]\n"
            "ST64B x4, [x13]\n"
            :
            : "r" (src), "r" (dst)
            : "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13"
        );
        return 0;
    #else
        memcpy(dst, src, COPY_ALIGNED_DATA_BYTES); // 此处不使用安全函数，src与dst地址保证合法，提高性能
        return 0;
    #endif
    }
/* RingBuffer 构造函数

   功能：
   1. 初始化所有成员变量
   2. 设置共享内存地址（mTxTail 指向前4字节，mTxAddr/mRxAddr 64字节对齐）
   3. 调用 InitElementPointers() 预计算 element 指针
*/
RingBuffer::RingBuffer(const RingBufferOpt &opts, const AddrInfo &localShm, const AddrInfo &remoteShm)
    : mProdHead(0), mProdTail(0), mCons(0), mConsCnt(0), mUpdateLim(opts.updateThreshold), mEpLastCap(0), mEpEofPos(0),
      mTxElements(nullptr), mRxElements(nullptr), mTxElemCount(0), mRxElemCount(0), mTxSizeMask(0), mRxSizeMask(0)
{
    // Send 相关：读取 localShm 的 tail，写入 localShm 的 buffer
    mTxTail.addr = localShm.addr;  // 对端consumer会更新这里
    mTxTail.len = sizeof(uint32_t);
    
    uintptr_t txBaseRaw = reinterpret_cast<uintptr_t>(localShm.addr);
    uintptr_t txAlignedAddr = (txBaseRaw + sizeof(uint32_t) + 63) & ~63ULL;
    mTxAddr.addr = reinterpret_cast<void *>(txAlignedAddr);
    size_t txUsedBytes = static_cast<size_t>(txAlignedAddr - txBaseRaw);
    mTxAddr.len = localShm.len > txUsedBytes ? localShm.len - txUsedBytes : 0;
    
    // Recv 相关：读取 remoteShm 的 buffer，更新 remoteShm 的 tail
    mRxTail.addr = remoteShm.addr;  // 本地consumer更新这里，告诉对端producer
    mRxTail.len = sizeof(uint32_t);
    
    uintptr_t rxBaseRaw = reinterpret_cast<uintptr_t>(remoteShm.addr);
    uintptr_t rxAlignedAddr = (rxBaseRaw + sizeof(uint32_t) + 63) & ~63ULL;
    mRxAddr.addr = reinterpret_cast<void *>(rxAlignedAddr);
    size_t rxUsedBytes = static_cast<size_t>(rxAlignedAddr - rxBaseRaw);
    mRxAddr.len = remoteShm.len > rxUsedBytes ? remoteShm.len - rxUsedBytes : 0;
    
    InitElementPointers();
}

void RingBuffer::InitElementPointers()
{
    // 初始化发送端 element 指针数组
    if (mTxAddr.addr != nullptr && mTxAddr.len > 0) {
        mTxElemCount = static_cast<uint32_t>(mTxAddr.len) / RING_ELEMENT_SIZE;
        mTxSizeMask = mTxElemCount > 0 ? mTxElemCount - 1 : 0;  // 假设 size 是 2 的幂
        
        mTxElements = new void *[mTxElemCount];
        uint8_t *base = static_cast<uint8_t*>(mTxAddr.addr);
        
        for (uint32_t i = 0; i < mTxElemCount; i++) {
            mTxElements[i] = base + i * RING_ELEMENT_SIZE;
        }
    }
    
    // 初始化接收端 element 指针数组
    if (mRxAddr.addr != nullptr && mRxAddr.len > 0) {
        mRxElemCount = static_cast<uint32_t>(mRxAddr.len) / RING_ELEMENT_SIZE;
        mRxSizeMask = mRxElemCount > 0 ? mRxElemCount - 1 : 0;
        
        mRxElements = new void *[mRxElemCount];
        uint8_t *base = static_cast<uint8_t*>(mRxAddr.addr);
        
        for (uint32_t i = 0; i < mRxElemCount; i++) {
            mRxElements[i] = base + i * RING_ELEMENT_SIZE;
        }
    }
}

/* 初始化共享内存
*
* 注意：
* 1. 只在首次创建 Ring Buffer 时调用一次
* 2. 将所有 Element 的 hasData 设置为 EMPTY
* 3. 如果是重新连接已存在的 Ring Buffer，不应调用此函数
*/
void RingBuffer::InitSharedMemory()
{
    if (mTxElements == nullptr || mTxElemCount == 0) {
        return;
    }
    
    // 初始化所有 element 为 EMPTY 状态
    for (uint32_t i = 0; i < mTxElemCount; i++) {
        Element *elem = static_cast<Element*>(mTxElements[i]);
        elem->hasData = ELEMENT_EMPTY;
        elem->dataLen = 0;
        elem->readOffset = 0;
        elem->reserved = 0;
    }
    
    // 确保初始化完成
    std::atomic_thread_fence(std::memory_order_release);
}

/* MPSC Ring Buffer 发送实现 (基于 Element 包头)
*
* 关键改进：
* 1. 支持任意大小的数据包，自动拆分成多个 element
* 2. 例如 200 字节 → 60+60+60+20 分散到 4 个 element
* 3. 每个 element 有独立的包头（hasData, dataLen, readOffset）
* 4. 生产者通过 mTxTail 判断空间（由消费者更新）
* 5. 生产者使用 CAS 竞争和顺序提交（保证 MPSC）
*/
size_t RingBuffer::Send(const void *buf, uint32_t bufLen)
{
    if (buf == nullptr || bufLen == 0) {
        return -1;
    }

    // 计算需要的 element 数量（例如 200字节 需要 4 个 element）
    uint32_t elementsNeeded = BytesToElements(bufLen);
    
    // ===== 阶段 1: CAS 竞争预留多个 element =====
    uint32_t prodHead = 0;
    uint32_t prodNext = 0;
    uint32_t consTail = 0;
    
    do {
        prodHead = mProdHead.load(std::memory_order_relaxed);
        prodNext = prodHead + elementsNeeded;
        
        // 读取远程消费者 tail（消费者会更新此值）
        consTail = AtomicLoadAcquire32(
            reinterpret_cast<volatile uint32_t*>(mTxTail.addr));
        
        // 计算空闲 element 数量
        uint32_t freeElements = RingFreeElements(prodHead, consTail, mTxElemCount);
        if (elementsNeeded > freeElements) {
            return -1; // 空间不足
        }
        
        // CAS 竞争
    } while (!mProdHead.compare_exchange_weak(
        prodHead, prodNext,
        std::memory_order_acquire,
        std::memory_order_relaxed));
    
    // ===== 阶段 2: 将数据拆分写入多个 element =====
    const uint8_t *srcBuf = static_cast<const uint8_t*>(buf);
    uint32_t remaining = bufLen;
    uint32_t srcOffset = 0;
    uint32_t toWrite = 0;
    for (uint32_t i = 0; i < elementsNeeded; i++) {
        // 使用预先计算的指针，无需每次计算
        Element *elem = static_cast<Element*>(GetTxElement(prodHead + i));
        
        // 计算当前 element 要写入的数据量
        if (remaining < RING_DATA_SIZE) {
            toWrite = remaining;
            mLocalElem.hasData = ELEMENT_EOF;
        } else {
            toWrite = RING_DATA_SIZE;
            mLocalElem.hasData = ELEMENT_DATA;
        }
        // 写入数据和包头
        mLocalElem.dataLen = static_cast<uint8_t>(toWrite);
        mLocalElem.readOffset = 0;
        // 先在本地组装好元素，再一次性拷贝到共享内存
        memcpy(mLocalElem.data, srcBuf + srcOffset, toWrite);
        // 需要改为硬件指令
        Copy64Byte((int8_t *)elem, (int8_t *)(&mLocalElem));
        
        srcOffset += toWrite;
        remaining -= toWrite;
    }
    
    // ===== 阶段 3: 顺序提交（等待前序生产者）=====
    uint32_t spinCount = 0;
    while (mProdTail.load(std::memory_order_acquire) != prodHead) {
        if (++spinCount > MAX_SPIN_ITERATIONS) {
            std::atomic_thread_fence(std::memory_order_acquire);
            spinCount = 0;
        }
    }
    
    // 内存屏障：确保数据写入完成
    std::atomic_thread_fence(std::memory_order_release);
    // 更新 tail，表示所有 element 提交完成
    mProdTail.store(prodNext, std::memory_order_release);
    
    return static_cast<size_t>(bufLen);
}

/* MPSC Ring Buffer 接收实现 (遍历 Element 包头)
*
* 关键改进：
* 1. 不读取远程 prodTail，而是遍历 element 检查 hasData
* 2. hasData=1: 有数据，可以读取
* 3. hasData=2: 部分读取，继续读取剩余部分
* 4. hasData=0: 空闲，停止遍历
* 5. 读取完成后更新 hasData 和 readOffset
* 6. 延迟更新 mTxTail 通知生产者释放空间
*/
size_t RingBuffer::Recv(void *buf, uint32_t bufLen)
{
    if (buf == nullptr || bufLen == 0) {
        return -1;
    }

    uint8_t *destBuf = static_cast<uint8_t*>(buf);
    uint32_t totalRead = 0;
    
    // 从 mCons 开始遍历 element
    while (totalRead < bufLen) {
        // 使用预先计算的指针
        Element *elem = static_cast<Element*>(GetRxElement(mCons));
        
        // 读取 hasData 状态（单字节读写天然原子）
        uint8_t state = elem->hasData;
        
        if (state == ELEMENT_EMPTY) {
            // 遇到空 element，停止读取
            break;
        }
        
        // 内存屏障：确保能看到生产者写入的数据
        std::atomic_thread_fence(std::memory_order_acquire);
        
        // 计算可读取的字节数
        uint32_t dataLen = elem->dataLen;
        uint32_t readOffset = elem->readOffset;
        uint32_t remaining = dataLen - readOffset;
        uint32_t toRead = std::min(remaining, bufLen - totalRead);
        
        // 读取数据
        memcpy(destBuf + totalRead, elem->data + readOffset, toRead);
        totalRead += toRead;
        readOffset += toRead;
        
        // 更新 element 状态
        if (readOffset < dataLen) {
            // 部分读取，readOffset 更新但 hasData 保持不变
            elem->readOffset = readOffset;
            break;
        }
        
        // 完全读取，设置为 EMPTY
        elem->readOffset = 0;
        elem->hasData = ELEMENT_EMPTY;
        
        // mCons 前进（element 索引）
        mCons++;
        // mConsCnt 累计（element 计数，用于判断是否更新 tail）
        mConsCnt++;
        // 延迟更新远程 tail，通知对端生产者空间已释放
        if (mConsCnt >= mUpdateLim && mRxTail.addr != nullptr) {
            AtomicStoreRelease32(reinterpret_cast<volatile uint32_t*>(mRxTail.addr), mCons);
            mConsCnt = 0;
        }
        // 继续检查下一个 element 是否有数据
    }
    
    return static_cast<size_t>(totalRead);
}

/* MPSC Ring Buffer WriteV (iovec 版本) */
size_t RingBuffer::WriteV(const struct iovec *iov, int iovCnt)
{
    if (iov == nullptr || iovCnt <= 0) {
        return -1;
    }

    uint32_t totalLen = 0;
    for (int i = 0; i < iovCnt; i++) {
        if (iov[i].iov_base != nullptr && iov[i].iov_len > 0) {
            totalLen += iov[i].iov_len;
        }
    }
    
    if (totalLen == 0) {
        return 0;
    }

    uint32_t elementsNeeded = BytesToElements(totalLen);
    // ===== 阶段 1: CAS 竞争预留空间 =====
    uint32_t prodHead = 0;
    uint32_t prodNext = 0;
    uint32_t consTail = 0;
    do {
        prodHead = mProdHead.load(std::memory_order_relaxed);
        prodNext = prodHead + elementsNeeded;
        
        consTail = AtomicLoadAcquire32(reinterpret_cast<volatile uint32_t*>(mTxTail.addr));
        
        uint32_t freeElements = RingFreeElements(prodHead, consTail, mTxElemCount);
        if (elementsNeeded > freeElements) {
            return -1; // 空间不足
        }
    } while (!mProdHead.compare_exchange_weak(
        prodHead, prodNext,
        std::memory_order_acquire,
        std::memory_order_relaxed));
    
    // ===== 阶段 2: 将 iovec 数据拆分写入多个 element =====
    uint32_t remaining = totalLen;
    uint32_t currentElemIdx = 0;  // 当前正在写入的 element 索引（相对于 prodHead）
    uint32_t bytesInCurrentElem = 0;  // 当前 element 已写入的字节数
    
    for (int iovIdx = 0; iovIdx < iovCnt; iovIdx++) {
        if (iov[iovIdx].iov_base == nullptr || iov[iovIdx].iov_len == 0) {
            continue;
        }

        const uint8_t *src = static_cast<const uint8_t*>(iov[iovIdx].iov_base);
        uint32_t srcLen = iov[iovIdx].iov_len;
        uint32_t srcOffset = 0;
        
        while (srcOffset < srcLen) {
            // 当前 element 剩余空间
            uint32_t spaceInElem = RING_DATA_SIZE - bytesInCurrentElem;
            uint32_t toCopy = std::min(srcLen - srcOffset, spaceInElem);
            
            // 拷贝到本地缓冲区
            memcpy(mLocalElem.data + bytesInCurrentElem, src + srcOffset, toCopy);
            bytesInCurrentElem += toCopy;
            srcOffset += toCopy;
            remaining -= toCopy;
            if (bytesInCurrentElem < RING_DATA_SIZE && remaining != 0) {
                continue;
            }
            // 如果当前 element 写满或者是最后的数据
            // 设置包头
            mLocalElem.dataLen = static_cast<uint8_t>(bytesInCurrentElem);
            mLocalElem.readOffset = 0;
            
            // 判断是否是最后一个 element
            if (remaining == 0) {
                mLocalElem.hasData = ELEMENT_EOF;
            } else {
                mLocalElem.hasData = ELEMENT_DATA;
            }
            
            // 一次性拷贝到共享内存
            Element *elem = static_cast<Element*>(GetTxElement(prodHead + currentElemIdx));
            Copy64Byte((int8_t *)elem, (int8_t *)(&mLocalElem));
            
            // 移动到下一个 element
            currentElemIdx++;
            bytesInCurrentElem = 0;
            memset(&mLocalElem, 0, RING_ELEMENT_SIZE);
        }
    }
    
    // ===== 阶段 3: 顺序提交（等待前序生产者）=====
    uint32_t spinCount = 0;
    while (mProdTail.load(std::memory_order_acquire) != prodHead) {
        if (++spinCount > MAX_SPIN_ITERATIONS) {
            std::atomic_thread_fence(std::memory_order_acquire);
            spinCount = 0;
        }
    }
    
    // 内存屏障：确保数据写入完成
    std::atomic_thread_fence(std::memory_order_release);
    
    // 更新 tail，表示所有 element 提交完成
    mProdTail.store(prodNext, std::memory_order_release);

    return static_cast<size_t>(totalLen);
}

/* MPSC Ring Buffer ReadV (iovec 版本) */
size_t RingBuffer::ReadV(const struct iovec *iov, int iovCnt)
{
    if (iov == nullptr || iovCnt <= 0 || mRxElements == nullptr) {
        return -1;
    }

    size_t totalRead = 0;
    
    // 遍历所有 iovec
    for (int iovIdx = 0; iovIdx < iovCnt; iovIdx++) {
        if (iov[iovIdx].iov_base == nullptr || iov[iovIdx].iov_len == 0) {
            continue;
        }
        
        uint8_t *destBuf = static_cast<uint8_t*>(iov[iovIdx].iov_base);
        uint32_t destLen = iov[iovIdx].iov_len;
        uint32_t destOffset = 0;
        
        // 填充当前 iovec
        while (destOffset < destLen) {
            Element *elem = static_cast<Element*>(GetRxElement(mCons));
            
            // 读取 hasData 状态（单字节读写天然原子）
            uint8_t state = elem->hasData;
            
            if (state == ELEMENT_EMPTY) {
                // 遇到空 element，停止读取
                return totalRead;
            }
            
            // 计算可读字节数
            uint32_t dataLen = elem->dataLen;
            uint32_t readOffset = elem->readOffset;
            uint32_t remaining = dataLen - readOffset;
            uint32_t toRead = std::min(remaining, destLen - destOffset);
            
            // 读取数据
            memcpy(destBuf + destOffset, elem->data + readOffset, toRead);
            destOffset += toRead;
            totalRead += toRead;
            readOffset += toRead;
            
            // 更新 element 状态
            if (readOffset < dataLen) {
                // 部分读取，readOffset 更新但 hasData 保持不变
                elem->readOffset = readOffset;
                // mCons 不前进，当前 iovec 已满，继续处理下一个 iovec
                break;  // 跳出当前 iovec 的 while 循环，继续下一个 iovec
            }
            // 完全读取，设置为 EMPTY
            elem->readOffset = 0;
            elem->hasData = ELEMENT_EMPTY;
            
            // mCons 前进（element 索引）
            mCons++;
            // mConsCnt 累计（element 计数，用于判断是否更新 tail）
            mConsCnt++;
            
            // 延迟更新远程 tail，通知对端生产者空间已释放
            if (mConsCnt >= mUpdateLim && mRxTail.addr != nullptr) {
                AtomicStoreRelease32(reinterpret_cast<volatile uint32_t*>(mRxTail.addr), mCons);
                mConsCnt = 0;
            }
            // 继续检查下一个 element 是否有数据
        }
    }
    return totalRead;
}

/* MPSC Ring Buffer GetWriteEvent
*
* 参数 event: 事件标志
*   - EPOLLET: 边缘触发模式（只在空间增加时触发）
*   - 0 或不带 EPOLLET: 水平触发模式（只要有空间就触发）
*
* 返回值:
*   - >0: 可写的 element 数量
*   - 0: 无可写空间或事件未触发
*   - -1: 错误
*/
int RingBuffer::GetWriteEvent(uint32_t event)
{
    // 读取本地 head（当前预留位置）
    uint32_t head = mProdTail.load(std::memory_order_relaxed);
    
    // 读取远程消费者 tail
    uint32_t tail = AtomicLoadAcquire32(
        reinterpret_cast<volatile uint32_t*>(mTxTail.addr));
    
    // 计算当前可写 element 数量
    uint32_t freeElements = RingFreeElements(head, tail, mTxElemCount);
    if ((event & EPOLLET) && (mEpLastCap >= freeElements)) {
        // ===== 边缘触发模式 (ET) =====
        // 空间不变或减小，未触发
        mEpLastCap = freeElements;
        return -1; // 事件未触发
    }
    // ===== 边缘触发模式 (ET) =====
    // 只在空间增加时触发
    // ===== 水平触发模式 (LT) =====
    // 只要有空间就触发，返回可写 element 数量
    mEpLastCap = freeElements;  // 更新记录的容量
    return 0;
}

/* MPSC Ring Buffer GetReadEvent
*
* 参数 event: 事件标志
*   - EPOLLET: 边缘触发模式（只在新数据到达时触发）
*   - 0 或不带 EPOLLET: 水平触发模式（只要有数据就触发）
*
* 返回值:
*   - >0: 可读的 element 数量
*   - 0: 无可读数据或事件未触发
*   - -1: 错误
*
*   边缘触发时，使用 (mCons + readableCount) 作为"数据边界位置"来检测新数据。
*   这样即使消费者读取了部分数据（mCons 增加），只要有新数据写入
*   （导致新的边界位置 > mEpEofPos），就会触发事件。
*/
int RingBuffer::GetReadEvent(uint32_t event)
{
    // 从 mCons 开始向后遍历，统计有多少个 element 有数据
    uint32_t currentPos = mCons;
    if (event & EPOLLET) {
        currentPos = mEpEofPos;
    }
    bool eof = false;
    // 遍历查找连续的有数据的 element
    // 注意：遇到 EMPTY 就停止（保证连续性）
    for (uint32_t i = 0; i < mRxElemCount; i++) {
        Element *elem = static_cast<Element*>(GetRxElement(currentPos));
        uint8_t state = elem->hasData;  // 单字节读写天然原子
        
        // 内存屏障：确保能看到生产者的数据
        std::atomic_thread_fence(std::memory_order_acquire);
        if (state == ELEMENT_EMPTY) {
            break;
        }
        currentPos++;
        if (state == ELEMENT_EOF) {
            eof = true;
            break;
        }
    }
    if (!eof) {
        return -1;
    }
    if (event & EPOLLET) {
        mEpEofPos = currentPos;
    }
    return 0;
}
}
#endif