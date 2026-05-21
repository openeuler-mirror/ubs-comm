/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-comm is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 * http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef UBS_COMM_UMQ_ERRNO_CONVERTER_H
#define UBS_COMM_UMQ_ERRNO_CONVERTER_H

#include <cerrno>
#include <array>
#include <cstddef>
#include "umq_api.h"

namespace ock {
namespace ubs {
namespace umq {

enum class UmqOperation {
    CONNECT,
    ACCEPT,
    WRITEV,
    READV,
    CREATE,
    BIND_INFO_GET,
};

struct UmqErrnoMapping {
    int umqErrCode;
    int linuxErrno;
    const char* description;
};

struct UmqBufStatusMapping {
    umq_buf_status_t bufStatus;
    int linuxErrno;
    const char* description;
};

class UmqErrnoConverter {
public:
    UmqErrnoConverter() = delete;

    /**
     * @brief 将UMQ API返回的错误码转换为Linux errno（正值）
     * 适用于返回int类型的UMQ API（如umq_connect/umq_accept/umq_post/umq_recv等）
     * @param op 操作类型（CONNECT/ACCEPT/WRITEV/READV）
     * @param umqRet UMQ API返回值，负数表示错误
     * @param savedErrno 调用UMQ API后立即保存的errno值，用于特定场景覆盖映射结果
     *   - UMQ_FAIL(=-1)且savedErrno为EINVAL/ENODEV/ENOMEM/ENOEXEC/EIO时，优先返回savedErrno
     *   - UMQ_ERR_ENODEV且savedErrno为EINVAL/EIO时，优先返回savedErrno
     *   - 其他场景按映射表转换，映射表未命中时回退到savedErrno或EIO
     * @code
     * int ret = umq_connect(...);
     * if (ret != UMQ_SUCCESS) {
     *     int savedErrno = errno;
     *     errno = UmqErrnoConverter::Convert(UmqOperation::CONNECT, ret, savedErrno);
     * }
     * @endcode
     */
    static int Convert(UmqOperation op, int umqRet, int savedErrno = 0);

    /**
     * @brief 将UMQ缓冲区完成状态(umq_buf_status_t)转换为Linux errno（正值）
     * 适用于处理CQE中的buf->status字段
     * @param op 操作类型（CONNECT/ACCEPT/WRITEV/READV）
     * @param bufStatus CQE中的缓冲区完成状态
     * @param savedErrno 调用UMQ API后立即保存的errno值，映射表未命中时作为回退
     * @code
     * umq_buf_status_t status = buf->status;
     * if (status != UMQ_BUF_SUCCESS) {
     *     int savedErrno = errno;
     *     errno = UmqErrnoConverter::ConvertBufStatus(UmqOperation::WRITEV, status, savedErrno);
     * }
     * @endcode
     */
    static int ConvertBufStatus(UmqOperation op, umq_buf_status_t bufStatus, int savedErrno = 0);

    /**
     * @brief 将返回句柄/大小的UMQ API错误转换为Linux errno（正值）
     * 适用于返回句柄(uint64_t)或大小(uint32_t)的UMQ API，失败时返回0
     * @param op 操作类型（CREATE/BIND_INFO_GET）
     * @param savedErrno 调用UMQ API后立即保存的errno值
     *   - CREATE: savedErrno为EINVAL/EPERM时直接返回，否则返回EIO
     *   - BIND_INFO_GET: savedErrno为ENOMEM/EINVAL时直接返回，否则返回EIO
     * @code
     * uint64_t handle = umq_create(&option);
     * if (handle == UMQ_INVALID_HANDLE) {
     *     int savedErrno = errno;
     *     errno = UmqErrnoConverter::ConvertHandleResult(UmqOperation::CREATE, savedErrno);
     * }
     * @endcode
     */
    static int ConvertHandleResult(UmqOperation op, int savedErrno);

    /**
     * @brief 获取错误码的描述信息，用于日志记录
     */
    static const char* GetErrorDescription(UmqOperation op, int umqRet);

    /**
     * @brief 获取缓冲区状态的描述信息，用于日志记录
     */
    static const char* GetBufStatusDescription(UmqOperation op, umq_buf_status_t bufStatus);

private:
    static constexpr std::size_t maxErrnoMappings = 16;
    static constexpr std::size_t maxBufStatusMappings = 24;

    // CONNECT/ACCEPT共用错误码映射表
    static constexpr inline std::array<UmqErrnoMapping, 13> kCommonConnectAcceptErrnoMappings{{
        {UMQ_SUCCESS, 0, "Success"},
        // 因为UMQ_FAIL和UMQ_ERR_EPERM的值相同，均为-1，所以这里只映射UMQ_ERR_EPERM
        {UMQ_ERR_EPERM, EIO, "Unrecoverable error: device unavailable, invalid parameter, driver/hardware error"},
        {UMQ_ERR_EAGAIN, EAGAIN, "Resource temporarily unavailable"},
        {UMQ_ERR_ENOMEM, ENOMEM, "Out of memory"},
        {UMQ_ERR_EBUSY, EBUSY, "Device or resource busy"},
        {UMQ_ERR_EEXIST, EEXIST, "File exists"},
        {UMQ_ERR_EINVAL, EINVAL, "Invalid argument"},
        {UMQ_ERR_ENODEV, ENODEV, "No such device"},
        {UMQ_ERR_ENOSR, ENOSR, "Out of streams resources"},
        {UMQ_ERR_ETIMEOUT, ETIMEDOUT, "Connection timed out"},
        {UMQ_ERR_EINPROGRESS, EINPROGRESS, "Operation now in progress"},
        {UMQ_ERR_ETSEG_NON_IMPORTED, EIO, "Cannot assign requested address"},
        {UMQ_ERR_EFLOWCTL, EIO, "Flow control error"},
    }};

    // Writev/Readv共用错误码映射表
    static constexpr inline std::array<UmqErrnoMapping, 13> kCommonIoErrnoMappings{{
        {UMQ_SUCCESS, 0, "Success"},
        // 因为UMQ_FAIL和UMQ_ERR_EPERM的值相同，均为-1，所以这里只映射UMQ_ERR_EPERM
        {UMQ_ERR_EPERM, EIO, "Unrecoverable error: device unavailable, invalid parameter, driver/hardware error"},
        {UMQ_ERR_EAGAIN, EAGAIN, "Resource temporarily unavailable"},
        {UMQ_ERR_ENOMEM, ENOMEM, "Out of memory"},
        {UMQ_ERR_EBUSY, EBUSY, "Device or resource busy"},
        {UMQ_ERR_EEXIST, EEXIST, "File exists"},
        {UMQ_ERR_EINVAL, EINVAL, "Invalid argument"},
        {UMQ_ERR_ENODEV, ENODEV, "No such device"},
        {UMQ_ERR_ENOSR, ENOSR, "Out of streams resources"},
        {UMQ_ERR_ETIMEOUT, ETIMEDOUT, "Connection timed out"},
        {UMQ_ERR_EINPROGRESS, EINPROGRESS, "Operation now in progress"},
        {UMQ_ERR_ETSEG_NON_IMPORTED, EIO, "Cannot assign requested address"},
        {UMQ_ERR_EFLOWCTL, EIO, "Flow control error"},
    }};

    // Connect/Accept的Buf状态映射表，一般是回调抛出的内部驱动/硬件错误，所有错误统一映射为EIO
    static constexpr inline std::array<UmqBufStatusMapping, 17> kCommonConnectAcceptBufStatusMappings{{
        {UMQ_BUF_SUCCESS, 0, "Buffer operation success"},
        {UMQ_BUF_UNSUPPORTED_OPCODE_ERR, EIO, "Protocol not supported"},
        {UMQ_BUF_LOC_LEN_ERR, EIO, "Message too long"},
        {UMQ_BUF_LOC_OPERATION_ERR, EIO, "Local operation error"},
        {UMQ_BUF_LOC_ACCESS_ERR, EIO, "Permission denied"},
        {UMQ_BUF_REM_RESP_LEN_ERR, EIO, "Remote response length error"},
        {UMQ_BUF_REM_UNSUPPORTED_REQ_ERR, EIO, "Remote unsupported request"},
        {UMQ_BUF_REM_OPERATION_ERR, EIO, "Remote operation error"},
        {UMQ_BUF_REM_ACCESS_ABORT_ERR, EIO, "Connection reset by peer"},
        {UMQ_BUF_ACK_TIMEOUT_ERR, EIO, "Acknowledgement timeout"},
        {UMQ_BUF_RNR_RETRY_CNT_EXC_ERR, EIO, "RNR retry count exceeded"},
        {UMQ_BUF_WR_FLUSH_ERR, EIO, "Write flush error"},
        {UMQ_BUF_WR_SUSPEND_DONE, EIO, "Connection aborted"},
        {UMQ_BUF_WR_FLUSH_ERR_DONE, EIO, "Write flush error done"},
        {UMQ_BUF_WR_UNHANDLED, EIO, "Write unhandled"},
        {UMQ_BUF_LOC_DATA_POISON, EIO, "Local data poison"},
        {UMQ_BUF_REM_DATA_POISON, EIO, "Remote data poison, connection reset"},
    }};

    // Writev的Buf状态映射表，一般是回调抛出的内部驱动/硬件错误，所有错误统一映射为EIO
    static constexpr inline std::array<UmqBufStatusMapping, 24> kWritevBufStatusMappings{{
        {UMQ_BUF_SUCCESS, 0, "Buffer operation success"},
        {UMQ_BUF_UNSUPPORTED_OPCODE_ERR, EIO, "Protocol not supported"},
        {UMQ_BUF_LOC_LEN_ERR, EIO, "Message too long"},
        {UMQ_BUF_LOC_OPERATION_ERR, EIO, "Local operation error"},
        {UMQ_BUF_LOC_ACCESS_ERR, EIO, "Permission denied"},
        {UMQ_BUF_REM_RESP_LEN_ERR, EIO, "Remote response length error"},
        {UMQ_BUF_REM_UNSUPPORTED_REQ_ERR, EIO, "Remote unsupported request"},
        {UMQ_BUF_REM_OPERATION_ERR, EIO, "Broken pipe"},
        {UMQ_BUF_REM_ACCESS_ABORT_ERR, EIO, "Broken pipe, remote access abort"},
        {UMQ_BUF_ACK_TIMEOUT_ERR, EIO, "Acknowledgement timeout"},
        {UMQ_BUF_RNR_RETRY_CNT_EXC_ERR, EIO, "RNR retry count exceeded"},
        {UMQ_BUF_WR_FLUSH_ERR, EIO, "Write flush error"},
        {UMQ_BUF_WR_SUSPEND_DONE, EIO, "Connection aborted"},
        {UMQ_BUF_WR_FLUSH_ERR_DONE, EIO, "Write flush error done"},
        {UMQ_BUF_WR_UNHANDLED, EIO, "Write unhandled"},
        {UMQ_BUF_LOC_DATA_POISON, EIO, "Local data poison"},
        {UMQ_BUF_REM_DATA_POISON, EIO, "Broken pipe, remote data poison"},
        {UMQ_BUF_FLOW_CONTROL_UPDATE, 0, "Flow control update success"},
        {UMQ_MEMPOOL_UPDATE_SUCCESS, 0, "Mempool update success"},
        {UMQ_MEMPOOL_UPDATE_FAILED, EIO, "Mempool update failed"},
        {UMQ_IMPORT_TSEG_SUCCESS, 0, "Import TSEG success"},
        {UMQ_IMPORT_TSEG_FAILED, EIO, "Import TSEG failed"},
        {UMQ_FAKE_BUF_FC_UPDATE, 0, "Fake buffer flow control update success"},
        {UMQ_FAKE_BUF_FC_ERR, EIO, "Fake buffer flow control error"},
    }};

    // Readv的Buf状态映射表，一般是回调抛出的内部驱动/硬件错误，所有错误统一映射为EIO
    static constexpr inline std::array<UmqBufStatusMapping, 24> kReadvBufStatusMappings{{
        {UMQ_BUF_SUCCESS, 0, "Buffer operation success"},
        {UMQ_BUF_UNSUPPORTED_OPCODE_ERR, EIO, "Protocol not supported"},
        {UMQ_BUF_LOC_LEN_ERR, EIO, "Message too long"},
        {UMQ_BUF_LOC_OPERATION_ERR, EIO, "Local operation error"},
        {UMQ_BUF_LOC_ACCESS_ERR, EIO, "Permission denied"},
        {UMQ_BUF_REM_RESP_LEN_ERR, EIO, "Remote response length error"},
        {UMQ_BUF_REM_UNSUPPORTED_REQ_ERR, EIO, "Remote unsupported request"},
        {UMQ_BUF_REM_OPERATION_ERR, EIO, "Connection reset by peer"},
        {UMQ_BUF_REM_ACCESS_ABORT_ERR, EIO, "Connection reset by peer, remote access abort"},
        {UMQ_BUF_ACK_TIMEOUT_ERR, EIO, "Acknowledgement timeout"},
        {UMQ_BUF_RNR_RETRY_CNT_EXC_ERR, EIO, "RNR retry count exceeded"},
        {UMQ_BUF_WR_FLUSH_ERR, EIO, "Connection reset by peer, write flush error"},
        {UMQ_BUF_WR_SUSPEND_DONE, EIO, "Connection aborted"},
        {UMQ_BUF_WR_FLUSH_ERR_DONE, EIO, "Connection reset by peer, write flush error done"},
        {UMQ_BUF_WR_UNHANDLED, EIO, "Write unhandled"},
        {UMQ_BUF_LOC_DATA_POISON, EIO, "Local data poison"},
        {UMQ_BUF_REM_DATA_POISON, EIO, "Connection reset by peer, remote data poison"},
        {UMQ_BUF_FLOW_CONTROL_UPDATE, 0, "Flow control update success"},
        {UMQ_MEMPOOL_UPDATE_SUCCESS, 0, "Mempool update success"},
        {UMQ_MEMPOOL_UPDATE_FAILED, EIO, "Mempool update failed"},
        {UMQ_IMPORT_TSEG_SUCCESS, 0, "Import TSEG success"},
        {UMQ_IMPORT_TSEG_FAILED, EIO, "Import TSEG failed"},
        {UMQ_FAKE_BUF_FC_UPDATE, 0, "Fake buffer flow control update success"},
        {UMQ_FAKE_BUF_FC_ERR, EIO, "Fake buffer flow control error"},
    }};

    template <typename Mapping, std::size_t N>
    static int FindErrno(const std::array<Mapping, N>& mappings, int umqErrCode, int savedErrno = 0)
    {
        for (const auto& mapping : mappings) {
            if (mapping.umqErrCode == umqErrCode) {
                return mapping.linuxErrno;
            }
        }
        if (savedErrno > 0) {
            return savedErrno;
        }
        return EIO;
    }

    template <typename Mapping, std::size_t N>
    static const char* FindDescription(const std::array<Mapping, N>& mappings, int umqErrCode)
    {
        for (const auto& mapping : mappings) {
            if (mapping.umqErrCode == umqErrCode) {
                return mapping.description;
            }
        }
        return "Unknown error";
    }

    template <std::size_t N>
    static int FindBufStatusErrno(const std::array<UmqBufStatusMapping, N>& mappings,
                                   umq_buf_status_t bufStatus, int savedErrno = 0)
    {
        for (const auto& mapping : mappings) {
            if (mapping.bufStatus == bufStatus) {
                return mapping.linuxErrno;
            }
        }
        if (savedErrno > 0) {
            return savedErrno;
        }
        return EIO;
    }

    template <std::size_t N>
    static const char* FindBufStatusDescription(const std::array<UmqBufStatusMapping, N>& mappings,
                                                umq_buf_status_t bufStatus)
    {
        for (const auto& mapping : mappings) {
            if (mapping.bufStatus == bufStatus) {
                return mapping.description;
            }
        }
        return "Unknown buffer status";
    }

    static const char* GetErrnoMappingTableName(UmqOperation op);

    /**
     * @brief 判断是否应该用savedErrno覆盖映射结果
     * @param absUmqRet 取绝对值后的UMQ错误码（正数）
     * @param savedErrno 调用UMQ API后立即保存的errno值
     */
    static bool ShouldOverrideWithSavedErrno(int absUmqRet, int savedErrno);
};

} // namespace umq
} // namespace ubs
} // namespace ock

#endif // UBS_COMM_UMQ_ERRNO_CONVERTER_H
