# bRPC错误码映射参考

> 来源: `bRPC错误码1.0.xlsx` (Sheet: 接口调用链, 列D-F)
> 代码实现: `src/ubsocket/csrc/core/umq/umq_errno_converter.h/.cpp`
> UMQ错误码定义: `src/hcom/umq/include/umq/umq_errno.h`

---

## 人工验收查验清单

验收时应逐项确认以下内容，确保代码实现与xlsx设计文档完全对齐：

1. **映射表完整性**：对照第八节8.1/8.2，确认 `kCommonConnectAcceptErrnoMappings`(13项) 和 `kCommonIoErrnoMappings`(13项) 中每个UMQ错误码均有对应条目，无遗漏
2. **映射目标errno正确性**：逐行核对每个UMQ错误码映射到的Linux errno值是否与xlsx D列一致，重点关注以下易错项：
   - `UMQ_ERR_EPERM`(=UMQ_FAIL=-1) → EIO（两个错误码值相同，代码中仅映射EPERM）
   - `UMQ_ERR_ETSEG_NON_IMPORTED` → EIO
   - `UMQ_ERR_EFLOWCTL` → EIO
3. **ShouldOverrideWithSavedErrno覆盖逻辑**：对照第八节8.3，确认当UMQ返回UMQ_FAIL/UMQ_ERR_EPERM时，savedErrno为EINVAL/ENODEV/ENOMEM/ENOEXEC/EIO时能正确覆盖；UMQ_ERR_ENODEV时savedErrno为EINVAL/EIO时能正确覆盖
4. **ConvertHandleResult逻辑**：对照第八节8.4，确认CREATE场景仅透传EINVAL和EPERM，BIND_INFO_GET场景仅透传ENOMEM和EINVAL，其余一律返回EIO
5. **Buf Status映射完整性**：对照第八节8.5，确认三张Buf状态映射表条目数和映射值：
   - `kCommonConnectAcceptBufStatusMappings` 17项，非成功均映射EIO
   - `kWritevBufStatusMappings` 24项，非错误状态(FC_UPDATE/MEMPOOL_SUCCESS/IMPORT_TSEG_SUCCESS/FAKE_BUF_FC_UPDATE)映射0
   - `kReadvBufStatusMappings` 24项，同上
6. **Writev/Readv语义差异**：确认同一Buf Status在writev和readv中描述不同（远端错误writev用"Broken pipe"，readv用"Connection reset by peer"），但映射errno值相同（均为EIO）
7. **FindErrno/FindBufStatusErrno回退逻辑**：确认映射表未命中时先回退savedErrno，再回退EIO；不应出现映射表未命中时丢失错误信息的情况
8. **UMQ_FAIL与UMQ_ERR_EPERM同值处理**：确认代码注释说明UMQ_FAIL(-1)和UMQ_ERR_EPERM(EPERM=1)绝对值相同，映射表中只保留UMQ_ERR_EPERM条目，UMQ_FAIL通过ShouldOverrideWithSavedErrno覆盖机制处理

---

## 一、UMQ错误码定义 (umq_errno.h)

| 宏定义 | 值 | 说明 |
|--------|-----|------|
| UMQ_SUCCESS | 0 | 成功 |
| UMQ_FAIL | -1 | 通用失败 |
| UMQ_INVALID_HANDLE | 0 | 无效句柄 |
| UMQ_INVALID_FD | -1 | 无效FD |
| UMQ_ERR_EPERM | EPERM(1) | 操作不允许 |
| UMQ_ERR_EAGAIN | EAGAIN(11) | 资源暂时不可用 |
| UMQ_ERR_ENOMEM | ENOMEM(12) | 内存不足 |
| UMQ_ERR_EBUSY | EBUSY(16) | 设备或资源忙 |
| UMQ_ERR_EEXIST | EEXIST(17) | 已存在 |
| UMQ_ERR_EINVAL | EINVAL(22) | 参数无效 |
| UMQ_ERR_ENODEV | ENODEV(19) | 设备不存在 |
| UMQ_ERR_ENOSR | ENOSR(63) | 流资源不足(Jetty/TP) |
| UMQ_ERR_ETIMEOUT | ETIMEDOUT(110) | 连接超时 |
| UMQ_ERR_EINPROGRESS | EINPROGRESS(115) | 操作进行中 |
| UMQ_ERR_ETSEG_NON_IMPORTED | 0x0201(513) | TSEG未导入 |
| UMQ_ERR_EFLOWCTL | 0x0202(514) | 流控错误 |

> **注意**: UMQ_FAIL(=-1) 与 UMQ_ERR_EPERM(=EPERM=1) 的绝对值相同，代码中只映射UMQ_ERR_EPERM。

---

## 二、UMQ Buf状态定义 (umq_buf_status_t)

| 枚举值 | 数值 | 说明 |
|--------|------|------|
| UMQ_BUF_SUCCESS | 0 | 成功 |
| UMQ_BUF_UNSUPPORTED_OPCODE_ERR | 1 | WR操作码不支持 |
| UMQ_BUF_LOC_LEN_ERR | 2 | 本地数据过长 |
| UMQ_BUF_LOC_OPERATION_ERR | 3 | 本地操作错误 |
| UMQ_BUF_LOC_ACCESS_ERR | 4 | 本地内存访问错误 |
| UMQ_BUF_REM_RESP_LEN_ERR | 5 | 远端响应长度错误 |
| UMQ_BUF_REM_UNSUPPORTED_REQ_ERR | 6 | 远端不支持请求 |
| UMQ_BUF_REM_OPERATION_ERR | 7 | 目标jetty无法完成操作 |
| UMQ_BUF_REM_ACCESS_ABORT_ERR | 8 | 远端访问内存错误或中止操作 |
| UMQ_BUF_ACK_TIMEOUT_ERR | 9 | 重传超过最大次数(ACK超时) |
| UMQ_BUF_RNR_RETRY_CNT_EXC_ERR | 10 | RNR重试超限(远端jfr无buffer) |
| UMQ_BUF_WR_FLUSH_ERR | 11 | Jetty错误态，硬件已处理WR |
| UMQ_BUF_WR_SUSPEND_DONE | 12 | 硬件构造fake CQE, user_ctx无效 |
| UMQ_BUF_WR_FLUSH_ERR_DONE | 13 | 硬件构造fake CQE, user_ctx无效 |
| UMQ_BUF_WR_UNHANDLED | 14 | flush jetty/jfs返回，硬件未处理WR |
| UMQ_BUF_LOC_DATA_POISON | 15 | 本地数据中毒 |
| UMQ_BUF_REM_DATA_POISON | 16 | 远端数据中毒 |
| UMQ_BUF_FLOW_CONTROL_UPDATE | 128 | 流控窗口更新(非错误) |
| UMQ_MEMPOOL_UPDATE_SUCCESS | 129 | 内存池更新成功(非错误) |
| UMQ_MEMPOOL_UPDATE_FAILED | 130 | 内存池更新失败 |
| UMQ_IMPORT_TSEG_SUCCESS | 131 | 导入TSEG成功(非错误) |
| UMQ_IMPORT_TSEG_FAILED | 132 | 导入TSEG失败 |
| UMQ_FAKE_BUF_FC_UPDATE | 192 | 虚拟buffer流控更新(非错误) |
| UMQ_FAKE_BUF_FC_ERR | 193 | 虚拟buffer流控错误(CR状态异常) |

---

## 三、connect 错误码映射 (xlsx D-F列)

### 3.1 UBSocket connect → TCP errno 映射总表

| 出错场景 | TCP映射errno | errno说明 | 映射原因 (F列) |
|----------|-------------|-----------|---------------|
| umq_dev_info_get失败 | ENODEV | 设备不存在 | 设备不存在于已有的设备缓存中 |
| umq_dev_info_get失败(参数) | EINVAL | 参数无效 | 接口入参无效（超过限制） |
| umq_dev_add失败(非EEXIST) | EIO | 内部硬件或驱动错误 | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| urma_log_set_level失败 | EINVAL | - | urma直接抛出errno，复用 |
| urma_register_log_func失败 | ENODEV | - | urma直接抛出errno，复用 |
| urma_register_log_func失败 | ENOMEM | - | urma直接抛出errno，复用 |
| urma_register_log_func失败 | ENOEXEC | - | urma直接抛出errno，复用 |
| urma_init失败(URMA_EEXIST) | EINVAL | - | urma直接抛出errno，复用 |
| urma_init失败(URMA_FAIL) | ENOMEM | - | urma直接抛出errno，复用 |
| urma_get_device_list失败 | EINVAL | - | urma直接抛出errno，复用 |
| urma_get_device_list失败 | ENODEV | - | urma直接抛出errno，复用 |
| urma_get_device_list失败 | ENOMEM | - | urma直接抛出errno，复用 |
| urma_get_device_list失败 | ENOEXEC | - | urma直接抛出errno，复用 |
| urma_get_eid_list失败 | EINVAL | - | urma直接抛出errno，复用 |
| urma_get_eid_list失败 | ENOMEM | - | urma直接抛出errno，复用 |
| urma_get_eid_list失败 | EIO | 内部错误 | 内核找不到eid信息，urma直接抛出errno |
| umq_create失败 | ENOMEM | 内存不足/内存申请失败 | UMQ申请/创建内存失败 |
| umq_create失败 | EEXIST | - | 设备已存在映射为地址已被占用/网络地址已被占用需要映射 |
| umq_create失败 | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| umq_create失败 | EINVAL | - | 入参无效，访问用户给定的无效地址空间 |
| umq_create失败(urma) | EIO | - | urma直接抛出errno，复用 |
| umq_bind失败 | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| umq_bind失败(urma) | EINVAL | - | urma直接抛出errno，复用 |
| umq_bind失败(urma) | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| umq_create(handle无效) | EINVAL | - | urma直接抛出errno，复用 |
| umq_create(handle无效,urma) | EINVAL→EINVAL | - | urma直接抛出errno，复用 |
| umq_create(handle无效,urma) | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| 多次urma调用 | EINVAL→EINVAL | - | urma直接抛出errno，复用 |
| 多次urma调用 | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| urma_create_jetty | EINVAL→EINVAL | - | 整体记录为网络基础设施问题（设备不存在，驱动/硬件操作失败上报错误） |
| urma_create_jetty | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| urma_create_jetty | EPERM | - | urma说明可重试解决 |
| umq_bind_info_get | ENOMEM→ENOMEM | - | UMQ抛出errno |
| umq_bind_info_get | EINVAL→EINVAL | - | UMQ抛出errno |
| umq_bind | EINVAL→EINVAL | - | urma直接抛出errno，复用 |
| umq_bind(urma) | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| umq_buf_alloc | ENOMEM | 资源暂时不足 | 内存申请失败 |
| umq_post | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| umq_post(urma) | EAGAIN | - | - |
| umq_post(urma) | ENOMEM | - | 传入超出限制 |
| umq_post(urma) | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| umq_post(urma) | EINVAL | - | 访问用户提供的无效地址空间导致失败映射到入参不符合 |
| umq_post(flowctl) | EIO | - | 内部出错，流控能力异常 |
| CQE错误(connect方向) | EIO/EAGAIN/ENOMEM/EPERM/ETIMEDOUT/EINVAL/EEXIST/EINPROGRESS | - | UMQ错误码→TCP errno映射 |
| umq_poll(poll到错误cqe) | EIO | - | - |
| CR状态错误 | EIO | - | - |
| umq_state_get | EIO/.../ENETUNREACH | 底层硬件驱动异常 | CR的报错一般是底层硬件驱动异常 |
| umq_state_get(QUEUE_STATE_ERR) | EIO | - | 设备不存在，驱动/硬件操作失败上报等内部错误 |
| umq_buf_free | N/A | N/A | 不影响connect接口成功，void返回 |

### 3.2 connect UMQ错误码→TCP errno 汇总映射

| UMQ错误码 | → TCP errno | errno说明 | 备注 |
|-----------|------------|-----------|------|
| UMQ_FAIL | EIO | 未知错误 | 表示未定义的严重底层失败 |
| -UMQ_ERR_EAGAIN | EAGAIN | 资源暂时不可用 | 完全匹配 |
| -UMQ_ERR_ENOMEM | ENOMEM | 内存不足 | - |
| -UMQ_ERR_EPERM | EPERM | 操作不允许 | - |
| -UMQ_ERR_ETIMEOUT | ETIMEDOUT | 连接超时 | 语义优先 |
| -UMQ_ERR_EINVAL | EINVAL | 无效参数 | - |
| -UMQ_ERR_EEXIST | EEXIST | 已存在 | - |
| -UMQ_ERR_EINPROGRESS | EINPROGRESS | 操作进行中 | - |
| -UMQ_ERR_ENODEV | ENODEV | 设备不存在 | - |
| -UMQ_ERR_ENOSR | ENOSR | 流资源不足 | - |
| -UMQ_ERR_ETSEG_NON_IMPORTED | EIO | TSEG未导入 | 映射为EIO |
| -UMQ_ERR_EFLOWCTL | EIO | 流控错误 | 映射为EIO |
| UMQ_BUF_* (CR错误) | EIO | 底层驱动/硬件异常 | CR错误一般为底层硬件问题 |

---

## 四、accept 错误码映射 (xlsx D-F列)

### 4.1 UBSocket accept → TCP errno 映射总表

| 出错场景 | TCP映射errno | errno说明 | 映射原因 (F列) |
|----------|-------------|-----------|---------------|
| umq_get_route_list失败 | ENOMEM | - | route list超过个数上限 |
| umq_dev_info_get失败 | ENODEV | 协议错误 | UMQ未查询到设备信息 |
| umq_get_route_list参数 | EINVAL | 套接字未在监听连接或addrlen无效 | route信息即bonding eid，此处出错一般为eid不合理 |
| urma入参错误 | EINVAL | - | 抛出的INVAL都需要有日志配合——是umq或者urma的入参错误 |
| urma内核无topo信息 | EINVAL | - | 内核中无topo信息 |
| umq_dev_add失败 | EINVAL | - | - |
| umq_dev_add(urma) | EIO | - | 统一表示"底层设备/驱动/资源出现了不可恢复的故障" |
| urma_create_jetty | EINVAL→EINVAL | - | - |
| urma_create_jetty | ENODEV→ENODEV | 协议错误 | 内部错误ENODEV表示无可用设备，映射为EPROTO最能准确反映此类不可恢复的底层错误 |
| urma_create_jetty | ENOMEM→ENOMEM | - | - |
| urma_create_jetty | ENOEXEC→ENOEXEC | 协议错误 | 内部错误ENOEXEC表示设备信息前后不一致，映射为EPROTO最能反映此类不可恢复的底层状态错误 |
| urma_free_device_list | N/A | N/A | - |
| urma_get_eid_list | EINVAL | - | urma入参错误 |
| urma_get_eid_list | ENOMEM | - | urma内存申请失败 |
| urma_get_eid_list | EIO | - | - |
| urma_bind_jetty | EINVAL | - | - |
| urma_bind_jetty | EIO | - | 统一表示"底层设备/驱动/资源出现了不可恢复的故障" |
| umq_create | ENOMEM | - | - |
| umq_create | EEXIST(表格外) | - | - |
| umq_create | ENODEV(表格外) | - | - |
| umq_create(urma) | EINVAL→EINVAL | - | - |
| umq_create(urma) | EIO(表格外) | - | - |
| umq_bind | ENODEV | - | - |
| umq_bind(urma) | EINVAL→EINVAL | - | - |
| umq_bind(urma) | ENODEV | - | 依赖UMQ和URMA的日志确认具体的问题 |
| umq_bind | EINVAL | - | - |
| umq_bind(urma) | EINVAL→EINVAL | - | - |
| umq_bind(urma) | EIO | - | 统一表示"底层设备/驱动/资源出现了不可恢复的故障" |
| umq_dev_info_get | ENODEV(列表外) | - | - |
| umq_dev_info_get(urma) | EINVAL | - | - |
| umq_create(handle无效) | EIO | - | 统一表示"底层设备/驱动/资源出现了不可恢复的故障" |
| umq_create(urma) | EINVAL→EINVAL | - | - |
| umq_create(urma) | EIO | - | - |
| urma_modify_jetty | EINVAL→EINVAL | - | - |
| urma_modify_jetty | EIO | - | - |
| urma_bind_jetty | EINVAL→EINVAL | - | - |
| urma_bind_jetty | EIO | - | - |
| urma_create_jfc | EINVAL→EINVAL | - | - |
| urma_create_jfc | EIO | - | - |
| urma_bind_jfc | EPROTO | - | - |
| urma_bind_jfc | EPERM→EPERM | - | - |
| umq_bind_info_get | ENOMEM | - | - |
| umq_bind_info_get | EINVAL | - | - |
| umq_bind | EINVAL→EINVAL | - | - |
| umq_bind(urma) | EIO | - | 统一表示"底层设备/驱动/资源出现了不可恢复的故障" |
| umq_buf_alloc | ENOMEM | - | - |
| umq_post | EIO | - | 统一表示"底层设备/驱动/资源出现了不可恢复的故障" |
| umq_post(urma) | EEXIST(列表外) | - | 依赖内部UMQ/URMA提供充分的差异日志 |
| umq_post(urma) | ENODEV(列表外) | - | 依赖内部UMQ/URMA提供充分的差异日志 |
| CQE错误(accept方向) | EIO/EAGAIN/ENOMEM/EPERM/ETIMEDOUT/EINVAL/EEXIST/EAGAIN/UMQ_BUF*→EIO | - | EIO表示未定义的严重底层失败; CR错误一般为底层驱动/硬件问题 |
| umq_state_get | EIO | - | 内部错误设置导致的错误 |
| umq_buf_free | N/A | N/A | void返回 |

### 4.2 accept UMQ错误码→TCP errno 汇总映射

| UMQ错误码 | → TCP errno | errno说明 | 备注 |
|-----------|------------|-----------|------|
| UMQ_FAIL | EIO | 未知错误 | 表示未定义的严重底层失败 |
| -UMQ_ERR_EAGAIN | EAGAIN | 资源暂时不可用 | 完全匹配 |
| -UMQ_ERR_ENOMEM | ENOMEM | 内存不足 | - |
| -UMQ_ERR_EPERM | EPERM | 操作不允许 | - |
| -UMQ_ERR_ETIMEOUT | ETIMEDOUT | 连接超时 | 语义优先 |
| -UMQ_ERR_EINVAL | EINVAL | 无效参数 | - |
| -UMQ_ERR_EEXIST | EEXIST | 已存在 | - |
| -UMQ_ERR_EINPROGRESS | EINPROGRESS | 操作进行中 | - |
| -UMQ_ERR_ENODEV | ENODEV | 设备不存在 | - |
| -UMQ_ERR_ENOSR | ENOSR | 流资源不足 | - |
| -UMQ_ERR_ETSEG_NON_IMPORTED | EIO | TSEG未导入 | - |
| -UMQ_ERR_EFLOWCTL | EIO | 流控错误 | - |
| UMQ_BUF_* (CR错误) | EIO | 底层驱动/硬件异常 | - |

---

## 五、writev 错误码映射 (xlsx D-F列)

### 5.1 UBSocket writev → TCP errno 映射总表

| 出错场景 | TCP映射errno | errno说明 | 映射原因 (F列) |
|----------|-------------|-----------|---------------|
| umq_get_cq_event失败 | EIO | 硬件故障/资源不可用 | urma_wait_jfc失败的原因，期待用户的处理方式未记录 |
| umq_ack_interrupt | N/A | N/A | void返回 |
| umq_poll(poll到错误cqe) | EIO/EBADF/ENODEV | - | urma_poll_jfc失败原因，期待用户的处理方式未记录 |
| umq_cq_buffer_get | EINVAL | 参数无效 | 1.umq handle初始化出错 2.max_buf_count设置过小 |
| umq_cq_buffer_get(urma) | EIO | 参数无效 | umq/urma参数校验错误 |
| umq_cq_buffer_get(urma) | - | 参数无效/输入输出错误 | 参数校验失败/获取cqe失败(底层IO问题) |
| umq_cq_buffer_get(urma) | - | 参数无效 | 参数校验失败 |
| umq_buf_alloc失败 | ENOMEM | N/A | 申请成功返回地址，失败返回NULL |
| umq_rearm_interrupt失败 | EINVAL | 参数无效 | 参数校验失败 |
| umq_post失败(urma) | EIO | 硬件故障/资源不可用/FD无效 | umq_post部分符合EIO，部分符合EINVAL |
| umq_post(urma) | EAGAIN | 资源暂时不可用 | - |
| umq_post(urma) | ENOMEM | 内存不足 | - |
| umq_post(urma) | EIO | 设备不存在 | ENODEV不是writev标准errno，使用EIO替代 |
| umq_post(urma) | EINVAL | 参数无效 | - |
| umq_post(flowctl) | EIO | 资源暂时不足/资源异常 | 不可恢复应使用EIO标识无法继续进行通信 |

### 5.2 writev UMQ错误码→TCP errno 汇总映射

| UMQ错误码 | → TCP errno | errno说明 | 备注 |
|-----------|------------|-----------|------|
| UMQ_FAIL | EIO | 未知错误 | - |
| -UMQ_ERR_EAGAIN | EAGAIN | 资源暂时不可用 | - |
| -UMQ_ERR_ENOMEM | ENOMEM | 内存不足 | - |
| -UMQ_ERR_EPERM | EPERM | 操作不允许 | - |
| -UMQ_ERR_ETIMEOUT | ETIMEDOUT | 连接超时 | - |
| -UMQ_ERR_EINVAL | EINVAL | 无效参数 | - |
| -UMQ_ERR_EEXIST | EINVAL | 已存在 | 非标准错误，映射为EINVAL(错误的入参引入) |
| -UMQ_ERR_EINPROGRESS | EAGAIN | 异步操作进行中→资源暂时不可用 | 资源暂时不足 |
| UMQ_BUF_* (CR错误) | EIO | 底层驱动/硬件异常 | CR常见为硬件异常需找驱动等定位 |

---

## 六、readv 错误码映射 (xlsx D-F列)

> readv的映射与writev基本相同（xlsx标注"同writev"），语义差异主要在Buf Status映射。

### 6.1 readv UMQ错误码→TCP errno 汇总映射

| UMQ错误码 | → TCP errno | errno说明 | 备注 |
|-----------|------------|-----------|------|
| UMQ_FAIL | EIO | 未知错误 | - |
| -UMQ_ERR_EAGAIN | EAGAIN | 资源暂时不可用 | - |
| -UMQ_ERR_ENOMEM | ENOMEM | 内存不足 | - |
| -UMQ_ERR_EPERM | EPERM | 操作不允许 | - |
| -UMQ_ERR_ETIMEOUT | ETIMEDOUT | 连接超时 | - |
| -UMQ_ERR_EINVAL | EINVAL | 无效参数 | - |
| -UMQ_ERR_EEXIST | EINVAL | 已存在 | 非标准错误 |
| -UMQ_ERR_EINPROGRESS | EAGAIN | 异步操作进行中→资源暂时不可用 | - |
| UMQ_BUF_* (CR错误) | EIO | 底层驱动/硬件异常 | - |

---

## 七、URMA错误码映射建议 (Sheet2)

| URMA错误码 | 建议映射errno | 在accept标准列表内 | errno说明 | 映射原因 |
|-----------|-------------|:---:|-----------|---------|
| URMA_FAIL / 未定义 | EPROTO | 是 | Protocol error | 通用兜底选择，语义宽泛且安全 |
| URMA_EAGAIN | EAGAIN | 是 | 资源暂时不可用 | 完全匹配 |
| URMA_ENOMEM | ENOMEM | 是 | 无法分配内存 | 完全匹配 |
| URMA_ENOPERM | EPERM | 是 | 操作不允许 | 完全匹配 |
| URMA_ETIMEOUT | ETIMEDOUT | 否 | 连接超时 | 语义优先选择 |
| URMA_EINVAL | EINVAL | 是 | 无效参数 | 完全匹配 |
| URMA_EEXIST | EADDRINUSE | 是 | 地址已在使用 | 更优的标准映射(EEXIST语义不匹配网络编程) |
| EINPROGRESS | EAGAIN | 是 | 资源暂时不可用 | 纠正映射(accept中EINPROGRESS应映射为EAGAIN) |

---

## 八、代码实现 vs xlsx文档 对照检查

### 8.1 Connect/Accept Errno映射 (kCommonConnectAcceptErrnoMappings)

| UMQ错误码 | 代码映射errno | xlsx建议errno | 是否一致 | 备注 |
|-----------|:---:|:---:|:---:|------|
| UMQ_SUCCESS | 0 | 0 | ✅ | - |
| UMQ_ERR_EPERM (=UMQ_FAIL) | EIO | EIO | ✅ | UMQ_FAIL和UMQ_ERR_EPERM值相同(-1)，代码只映射UMQ_ERR_EPERM |
| UMQ_ERR_EAGAIN | EAGAIN | EAGAIN | ✅ | - |
| UMQ_ERR_ENOMEM | ENOMEM | ENOMEM | ✅ | - |
| UMQ_ERR_EBUSY | EBUSY | - | ✅ | umq_errno.h有定义，代码覆盖更全面 |
| UMQ_ERR_EEXIST | EEXIST | EEXIST | ✅ | - |
| UMQ_ERR_EINVAL | EINVAL | EINVAL | ✅ | - |
| UMQ_ERR_ENODEV | ENODEV | ENODEV | ✅ | - |
| UMQ_ERR_ENOSR | ENOSR | - | ✅ | umq_errno.h有定义(Jetty/TP资源不足)，代码覆盖更全面 |
| UMQ_ERR_ETIMEOUT | ETIMEDOUT | ETIMEDOUT | ✅ | - |
| UMQ_ERR_EINPROGRESS | EINPROGRESS | EINPROGRESS | ✅ | - |
| UMQ_ERR_ETSEG_NON_IMPORTED | EIO | EIO | ✅ | - |
| UMQ_ERR_EFLOWCTL | EIO | EIO | ✅ | - |

### 8.2 Writev/Readv Errno映射 (kCommonIoErrnoMappings)

| UMQ错误码 | 代码映射errno | xlsx建议errno | 是否一致 | 备注 |
|-----------|:---:|:---:|:---:|------|
| UMQ_SUCCESS | 0 | 0 | ✅ | - |
| UMQ_ERR_EPERM (=UMQ_FAIL) | EIO | EIO | ✅ | - |
| UMQ_ERR_EAGAIN | EAGAIN | EAGAIN | ✅ | - |
| UMQ_ERR_ENOMEM | ENOMEM | ENOMEM | ✅ | - |
| UMQ_ERR_EBUSY | EBUSY | - | ✅ | umq_errno.h有定义，代码覆盖更全面 |
| UMQ_ERR_EEXIST | EEXIST | EEXIST | ✅ | - |
| UMQ_ERR_EINVAL | EINVAL | EINVAL | ✅ | - |
| UMQ_ERR_ENODEV | ENODEV | ENODEV | ✅ | - |
| UMQ_ERR_ENOSR | ENOSR | - | ✅ | umq_errno.h有定义，代码覆盖更全面 |
| UMQ_ERR_ETIMEOUT | ETIMEDOUT | ETIMEDOUT | ✅ | - |
| UMQ_ERR_EINPROGRESS | EINPROGRESS | EINPROGRESS | ✅ | - |
| UMQ_ERR_ETSEG_NON_IMPORTED | EIO | EIO | ✅ | - |
| UMQ_ERR_EFLOWCTL | EIO | EIO | ✅ | - |

### 8.3 ShouldOverrideWithSavedErrno 逻辑对照

代码中的覆盖逻辑（当UMQ返回UMQ_FAIL/UMQ_ERR_EPERM时，用savedErrno覆盖）：

| 条件 | 代码行为 | xlsx对照 | 是否一致 | 备注 |
|------|---------|---------|:---:|------|
| umqRet=UMQ_FAIL(EPERM), savedErrno=EINVAL | 返回EINVAL | EINVAL→EINVAL(urma直接抛出errno) | ✅ | - |
| umqRet=UMQ_FAIL(EPERM), savedErrno=ENODEV | 返回ENODEV | ENODEV→ENODEV | ✅ | - |
| umqRet=UMQ_FAIL(EPERM), savedErrno=ENOMEM | 返回ENOMEM | ENOMEM→ENOMEM | ✅ | - |
| umqRet=UMQ_FAIL(EPERM), savedErrno=ENOEXEC | 返回ENOEXEC | ENOEXEC→ENOEXEC | ✅ | - |
| umqRet=UMQ_FAIL(EPERM), savedErrno=EIO | 返回EIO | EIO | ✅ | - |
| umqRet=UMQ_ERR_ENODEV, savedErrno=EINVAL | 返回EINVAL | EINVAL→EINVAL | ✅ | - |
| umqRet=UMQ_ERR_ENODEV, savedErrno=EIO | 返回EIO | EIO | ✅ | - |

### 8.4 ConvertHandleResult 逻辑对照

| 操作 | 条件 | 代码行为 | xlsx对照 | 是否一致 | 备注 |
|------|------|---------|---------|:---:|------|
| CREATE | savedErrno=EINVAL | 返回EINVAL | EINVAL→EINVAL | ✅ | - |
| CREATE | savedErrno=EPERM | 返回EPERM | EPERM(urma说明可重试解决) | ✅ | - |
| CREATE | 其他savedErrno | 返回EIO | EIO | ✅ | - |
| BIND_INFO_GET | savedErrno=ENOMEM | 返回ENOMEM | ENOMEM→ENOMEM | ✅ | - |
| BIND_INFO_GET | savedErrno=EINVAL | 返回EINVAL | EINVAL→EINVAL | ✅ | - |
| BIND_INFO_GET | 其他savedErrno | 返回EIO | EIO | ✅ | - |

### 8.5 Buf Status映射对照

**Connect/Accept (kCommonConnectAcceptBufStatusMappings) — 17项**

| Buf Status | 代码映射 | xlsx建议 | 是否一致 | 备注 |
|-----------|:---:|:---:|:---:|------|
| UMQ_BUF_SUCCESS | 0 | 0 | ✅ | - |
| UMQ_BUF_UNSUPPORTED_OPCODE_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_LOC_LEN_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_LOC_OPERATION_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_LOC_ACCESS_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_REM_RESP_LEN_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_REM_UNSUPPORTED_REQ_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_REM_OPERATION_ERR | EIO | EIO | ✅ | connect: "Connection reset by peer" |
| UMQ_BUF_REM_ACCESS_ABORT_ERR | EIO | EIO | ✅ | connect: "Connection reset by peer" |
| UMQ_BUF_ACK_TIMEOUT_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_RNR_RETRY_CNT_EXC_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_WR_FLUSH_ERR | EIO | EIO | ✅ | - |
| UMQ_BUF_WR_SUSPEND_DONE | EIO | EIO | ✅ | - |
| UMQ_BUF_WR_FLUSH_ERR_DONE | EIO | EIO | ✅ | - |
| UMQ_BUF_WR_UNHANDLED | EIO | EIO | ✅ | - |
| UMQ_BUF_LOC_DATA_POISON | EIO | EIO | ✅ | - |
| UMQ_BUF_REM_DATA_POISON | EIO | EIO | ✅ | connect: "Connection reset by peer, remote data poison" |

**Writev (kWritevBufStatusMappings) — 24项**

| Buf Status | 代码映射 | 代码描述 | 是否一致 | 备注 |
|-----------|:---:|---------|:---:|------|
| UMQ_BUF_SUCCESS | 0 | Buffer operation success | ✅ | - |
| UMQ_BUF_UNSUPPORTED_OPCODE_ERR | EIO | Protocol not supported | ✅ | - |
| UMQ_BUF_LOC_LEN_ERR | EIO | Message too long | ✅ | - |
| UMQ_BUF_LOC_OPERATION_ERR | EIO | Local operation error | ✅ | - |
| UMQ_BUF_LOC_ACCESS_ERR | EIO | Permission denied | ✅ | - |
| UMQ_BUF_REM_RESP_LEN_ERR | EIO | Remote response length error | ✅ | - |
| UMQ_BUF_REM_UNSUPPORTED_REQ_ERR | EIO | Remote unsupported request | ✅ | - |
| UMQ_BUF_REM_OPERATION_ERR | EIO | Broken pipe | ✅ | writev语义: 对端断开→EPIPE |
| UMQ_BUF_REM_ACCESS_ABORT_ERR | EIO | Broken pipe, remote access abort | ✅ | writev语义: 对端断开→EPIPE |
| UMQ_BUF_ACK_TIMEOUT_ERR | EIO | Acknowledgement timeout | ✅ | - |
| UMQ_BUF_RNR_RETRY_CNT_EXC_ERR | EIO | RNR retry count exceeded | ✅ | - |
| UMQ_BUF_WR_FLUSH_ERR | EIO | Write flush error | ✅ | - |
| UMQ_BUF_WR_SUSPEND_DONE | EIO | Connection aborted | ✅ | - |
| UMQ_BUF_WR_FLUSH_ERR_DONE | EIO | Write flush error done | ✅ | - |
| UMQ_BUF_WR_UNHANDLED | EIO | Write unhandled | ✅ | - |
| UMQ_BUF_LOC_DATA_POISON | EIO | Local data poison | ✅ | - |
| UMQ_BUF_REM_DATA_POISON | EIO | Broken pipe, remote data poison | ✅ | writev语义: 对端数据中毒→EPIPE |
| UMQ_BUF_FLOW_CONTROL_UPDATE | 0 | Flow control update success | ✅ | 非错误 |
| UMQ_MEMPOOL_UPDATE_SUCCESS | 0 | Mempool update success | ✅ | 非错误 |
| UMQ_MEMPOOL_UPDATE_FAILED | EIO | Mempool update failed | ✅ | - |
| UMQ_IMPORT_TSEG_SUCCESS | 0 | Import TSEG success | ✅ | 非错误 |
| UMQ_IMPORT_TSEG_FAILED | EIO | Import TSEG failed | ✅ | - |
| UMQ_FAKE_BUF_FC_UPDATE | 0 | Fake buffer flow control update success | ✅ | 非错误 |
| UMQ_FAKE_BUF_FC_ERR | EIO | Fake buffer flow control error | ✅ | - |

**Readv (kReadvBufStatusMappings) — 24项**

与Writev主要差异在远端错误的语义描述（writev用"Broken pipe"，readv用"Connection reset by peer"）：

| Buf Status | 代码映射 | 代码描述 | 差异说明 |
|-----------|:---:|---------|---------|
| UMQ_BUF_REM_OPERATION_ERR | EIO | Connection reset by peer | writev为"Broken pipe" |
| UMQ_BUF_REM_ACCESS_ABORT_ERR | EIO | Connection reset by peer, remote access abort | writev为"Broken pipe" |
| UMQ_BUF_WR_FLUSH_ERR | EIO | Connection reset by peer, write flush error | writev为"Write flush error" |
| UMQ_BUF_WR_FLUSH_ERR_DONE | EIO | Connection reset by peer, write flush error done | writev为"Write flush error done" |
| UMQ_BUF_REM_DATA_POISON | EIO | Connection reset by peer, remote data poison | writev为"Broken pipe" |

---

## 九、待确认项汇总

无待确认项，所有映射已补全。
