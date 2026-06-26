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

#include <array>
#include <cerrno>
#include <cstddef>
#include "umq_api.h"

namespace ock {
namespace ubs {
namespace umq {

/**
 * UMQ操作类型，决定映射路径和映射表。
 *
 * 映射路径分四类：
 * 1. Convert(统一表映射+override): CONNECT/ACCEPT/WRITEV/READV — 统一查kCommonErrnoMappings，
 *    ShouldOverrideWithSavedErrno生效时返回savedErrno覆盖表结果；表未命中回退savedErrno(>0时)，再回退EIO。
 *    op对errno映射无区分作用，仅影响BufStatus路径的映射表选择。
 *
 * 2. ConvertBufStatus(方向区分表映射): op决定BufStatus映射表选择——
 *    - CONNECT/ACCEPT: 查kCommonConnectAcceptBufStatusMappings
 *    - WRITEV: 查kWritevBufStatusMappings
 *    - READV: 查kReadvBufStatusMappings
 *
 * 3. Convert(GET_STATE): umq_state_get — 返回值是umq_state_t枚举(0-3)，不是UMQ_ERR_*负值，
 *    不查映射表，不走ShouldOverride，QUEUE_STATE_ERR和QUEUE_STATE_MAX返回EIO，其余返回0。
 *
 * 4. ConvertHandleResult(有限透传): API失败时返回0/nullptr，无法从返回值推导UMQ错误码，
 *    不查映射表，仅依赖savedErrno做有限透传：
 *    - CREATE(umq_create): 仅透传EINVAL/EPERM，其余EIO
 *    - BIND_INFO_GET(umq_bind_info_get/umq_dev_info_list_get): 仅透传ENOMEM/EINVAL，其余EIO
 */
enum class UmqOperation
{
    CONNECT,
    ACCEPT,
    WRITEV,
    READV,
    CREATE,
    BIND_INFO_GET,
    GET_STATE,
};

struct UmqErrnoMapping {
    int umqErrCode;
    int linuxErrno;
    const char *description;
};

struct UmqBufStatusMapping {
    umq_buf_status_t bufStatus;
    int linuxErrno;
    const char *description;
};

class UmqErrnoConverter {
public:
    UmqErrnoConverter() = delete;

    /**
     * @brief 将UMQ API返回的错误码转换为Linux errno（正值）
     * 适用于返回int类型的UMQ API（如umq_connect/umq_accept/umq_post/umq_recv等）
     * @param op 操作类型。对errno映射路径无区分作用——CONNECT/ACCEPT/WRITEV/READV统一查kCommonErrnoMappings；
     *   仅GET_STATE有特殊处理（不查表，非成功一律返回EIO）。op仍影响BufStatus路径的映射表选择。
     * @param umqRet UMQ API返回值，负数表示错误
     * @param savedErrno 调用UMQ API后立即保存的errno值，用于特定场景覆盖映射结果
     *   - ShouldOverrideWithSavedErrno生效时用savedErrno覆盖表结果；表未命中回退savedErrno(>0时)，再回退EIO
     *   - UMQ_FAIL(=-1)且savedErrno∈{EINVAL,ENODEV,ENOMEM,ENOEXEC,EIO}时，优先返回savedErrno
     *   - UMQ_ERR_ENODEV且savedErrno∈{EINVAL,EIO}时，优先返回savedErrno
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
     * 适用于返回句柄(uint64_t)或指针(umq_dev_info_t*)的UMQ API，失败时返回0或nullptr。
     * 与Convert不同：这些API失败时无法从返回值推导UMQ错误码（返回的是0/nullptr而非UMQ_ERR_*负值），
     * 因此不查映射表，仅依赖savedErrno做有限透传。
     * @param op 操作类型
     *   - CREATE: 用于umq_create — 返回0(UMQ_INVALID_HANDLE)表示失败；
     *     savedErrno为EINVAL/EPERM时直接返回，否则返回EIO
     *   - BIND_INFO_GET: 用于umq_bind_info_get/umq_dev_info_list_get — 返回nullptr表示失败；
     *     savedErrno为ENOMEM/EINVAL时直接返回，否则返回EIO
     * @param savedErrno 调用UMQ API后立即保存的errno值
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
     * @brief 获取errno映射的描述信息，用于日志记录
     * @param op 操作类型。对errno描述无区分作用——统一查kCommonErrnoMappings；
     *   仅GET_STATE有特殊描述路径。op不影响描述结果。
     * @param umqRet UMQ API返回值或umq_state_t枚举值
     */
    static const char *GetErrorDescription(UmqOperation op, int umqRet);

    /**
     * @brief 获取BufStatus映射的描述信息，用于日志记录
     * @param op 操作类型，决定BufStatus描述表选择——
     *   CONNECT/ACCEPT查kCommonConnectAcceptBufStatusMappings，WRITEV查kWritevBufStatusMappings，
     *   READV查kReadvBufStatusMappings。不同方向的描述语义不同（如WRITEV说"Broken pipe"，
     *   READV说"Connection reset by peer"）。
     * @param bufStatus CQE中的缓冲区完成状态
     */
    static const char *GetBufStatusDescription(UmqOperation op, umq_buf_status_t bufStatus);

private:
    static constexpr std::size_t maxErrnoMappings = 16;
    static constexpr std::size_t maxBufStatusMappings = 24;

    // 统一errno映射表（原Connect/Accept与Writev/Readv映射表完全一致，合并为单表）
    static constexpr inline std::array<UmqErrnoMapping, 15> kCommonErrnoMappings{{
        {UMQ_SUCCESS, 0, "Success"},
        {UMQ_ERR_EPERM, EIO, "Unrecoverable error: device unavailable, invalid parameter, driver/hardware error"},
        {UMQ_ERR_EAGAIN, EAGAIN, "Resource temporarily unavailable"},
        {UMQ_ERR_ENOMEM, ENOMEM, "Out of memory"},
        {UMQ_ERR_EBUSY, EBUSY, "Device or resource busy"},
        {UMQ_ERR_EEXIST, EEXIST, "File exists"},
        {UMQ_ERR_EINVAL, EINVAL, "Invalid argument"},
        {UMQ_ERR_ENODEV, ENODEV, "No such device"},
        {UMQ_ERR_ENOSR, ENOSR, "Out of streams resources"},
        {UMQ_ERR_EMLINK, EMLINK, "Too many links"},
        {UMQ_ERR_ENOBUFS, ENOBUFS, "No buffer space available"},
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
    static int FindErrno(const std::array<Mapping, N> &mappings, int umqErrCode, int savedErrno = 0)
    {
        for (const auto &mapping : mappings) {
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
    static const char *FindDescription(const std::array<Mapping, N> &mappings, int umqErrCode)
    {
        for (const auto &mapping : mappings) {
            if (mapping.umqErrCode == umqErrCode) {
                return mapping.description;
            }
        }
        return "Unknown error";
    }

    template <std::size_t N>
    static int FindBufStatusErrno(const std::array<UmqBufStatusMapping, N> &mappings, umq_buf_status_t bufStatus,
                                  int savedErrno = 0)
    {
        for (const auto &mapping : mappings) {
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
    static const char *FindBufStatusDescription(const std::array<UmqBufStatusMapping, N> &mappings,
                                                umq_buf_status_t bufStatus)
    {
        for (const auto &mapping : mappings) {
            if (mapping.bufStatus == bufStatus) {
                return mapping.description;
            }
        }
        return "Unknown buffer status";
    }

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
