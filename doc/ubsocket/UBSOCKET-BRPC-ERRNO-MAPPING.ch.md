# URMA错误码映射参考

> 代码实现: `src/ubsocket/csrc/core/urma/urma_errno_converter.h` (header-only，`ALWAYS_INLINE` 实现)
> URMA错误码定义: `src/ubsocket/csrc/under_api/urma/urma_opcode.h` (`urma_status_t` / `urma_cr_status_t`)
> UT覆盖: `src/ubsocket/unit_test/core/urma/urma_errno_converter_test.cpp`

***

## 人工验收查验清单

验收时应逐项确认以下内容，确保代码实现与映射设计对齐：

1. **Convert 直接映射完整性**：对照第二节2.2，确认 `Convert` 的直接映射分支覆盖 `URMA_SUCCESS`(→0)、`URMA_EAGAIN`(→EAGAIN)、`URMA_ENOMEM`(→ENOMEM)、`URMA_EINVAL`(→EINVAL) 四项，无遗漏
2. **Convert 按op兜底正确性**：逐项核对 op 兜底映射：`URMA_OP_CONNECT`/`URMA_OP_ACCEPT`→ECONNREFUSED、`URMA_OP_WRITEV`/`URMA_OP_READV`→EIO、`URMA_OP_CREATE_CTX`/`URMA_OP_CREATE_JETTY`→ENODEV、`URMA_OP_IMPORT_JETTY`/`URMA_OP_BIND`→ECONNREFUSED、default→EIO
3. **未直接映射的 urma_status_t 宏走op兜底**：`URMA_ENOPERM`/`URMA_ETIMEOUT`/`URMA_EEXIST`/`URMA_EINPROGRESS`/`URMA_FAIL`(0x1000) 不在直接映射分支中，落入 op 兜底；`URMA_FAIL`(0x1000) 为自定义值，不与任何 Linux errno 同值
4. **ConvertCrStatus 映射逐项核对**：对照第三节3.2，确认 RNR重试超限→EAGAIN、ACK超时→ETIMEDOUT、内存访问错误→EACCES、长度错误→EMSGSIZE、操作错误→EIO、flush错误→ENETRESET、数据中毒→EIO
5. **ConvertCrStatus default兜底**：确认未显式列出的状态（`URMA_CR_UNSUPPORTED_OPCODE_ERR`、`URMA_CR_REM_UNSUPPORTED_REQ_ERR`、`URMA_CR_WR_SUSPEND_DONE`、`URMA_CR_WR_UNHANDLED`）一律返回 EIO
6. **ConvertCrStatus 的op参数不参与逻辑**：确认函数签名中的 `op` 参数当前不影响映射结果，CR状态到errno的映射与调用流程无关
7. **两条路径均为纯查表**：`Convert`/`ConvertCrStatus` 无外部状态、无saved errno覆盖或回退机制，映射结果仅由入参决定
8. **成功值统一为0**：`URMA_SUCCESS` 与 `URMA_CR_SUCCESS` 均映射 0，调用方以返回值0判断成功

***

## 一、UrmaOperation 操作枚举 (urma_errno_converter.h)

`Convert`/`ConvertCrStatus` 的第二入参，标识错误发生时所属的操作流程：

| 枚举值 | 值 | 说明 |
| --- | --- | --- |
| URMA\_OP\_CONNECT | 0 | 连接建立 |
| URMA\_OP\_ACCEPT | 1 | 连接接受 |
| URMA\_OP\_WRITEV | 2 | TX数据通路 |
| URMA\_OP\_READV | 3 | RX数据通路 |
| URMA\_OP\_CREATE\_CTX | 4 | 创建context |
| URMA\_OP\_CREATE\_JETTY | 5 | 创建jetty |
| URMA\_OP\_IMPORT\_JETTY | 6 | 导入jetty |
| URMA\_OP\_BIND | 7 | 绑定 |
| URMA\_OP\_COUNT | 8 | 枚举计数 |

***

## 二、urma_status_t → errno 映射 (Convert)

### 2.1 urma_status_t 定义 (urma_opcode.h)

`urma_status_t` 为 `int` 别名，多数宏直接取同名 Linux errno 数值：

| 宏定义 | 值 | 说明 |
| --- | --- | --- |
| URMA\_SUCCESS | 0 | 成功 |
| URMA\_EAGAIN | EAGAIN(11) | 资源暂时不可用 |
| URMA\_ENOMEM | ENOMEM(12) | 内存不足 |
| URMA\_ENOPERM | EPERM(1) | 操作不允许 |
| URMA\_ETIMEOUT | ETIMEDOUT(110) | 操作超时 |
| URMA\_EINVAL | EINVAL(22) | 参数无效 |
| URMA\_EEXIST | EEXIST(17) | 已存在 |
| URMA\_EINPROGRESS | EINPROGRESS(115) | 操作进行中 |
| URMA\_FAIL | 0x1000(4096) | 通用失败(自定义值) |

### 2.2 Convert 映射表

`Convert(status, op)` 分两步查表：

**第一步 — 直接映射**（与op无关，同值透传）：

| urma\_status\_t | 映射errno |
| --- | --- |
| URMA\_SUCCESS | 0 |
| URMA\_EAGAIN | EAGAIN |
| URMA\_ENOMEM | ENOMEM |
| URMA\_EINVAL | EINVAL |

**第二步 — 按op兜底**（第一步未命中的所有status）：

| UrmaOperation | 映射errno |
| --- | --- |
| URMA\_OP\_CONNECT / URMA\_OP\_ACCEPT | ECONNREFUSED |
| URMA\_OP\_WRITEV / URMA\_OP\_READV | EIO |
| URMA\_OP\_CREATE\_CTX / URMA\_OP\_CREATE\_JETTY | ENODEV |
| URMA\_OP\_IMPORT\_JETTY / URMA\_OP\_BIND | ECONNREFUSED |
| default | EIO |

> **说明**: `URMA_ENOPERM`/`URMA_ETIMEOUT`/`URMA_EEXIST`/`URMA_EINPROGRESS`/`URMA_FAIL` 等未列入直接映射的status，均走第二步按op兜底。同一status在不同流程下产生不同errno（如建立阶段→ECONNREFUSED，数据通路→EIO，资源创建→ENODEV）。

***

## 三、urma_cr_status_t → errno 映射 (ConvertCrStatus)

### 3.1 urma_cr_status_t 定义 (urma_opcode.h)

completion record状态，从0开始的连续枚举：

| 枚举值 | 数值 | 说明 |
| --- | --- | --- |
| URMA\_CR\_SUCCESS | 0 | 成功 |
| URMA\_CR\_UNSUPPORTED\_OPCODE\_ERR | 1 | WR操作码不支持 |
| URMA\_CR\_LOC\_LEN\_ERR | 2 | 本地数据过长 |
| URMA\_CR\_LOC\_OPERATION\_ERR | 3 | 本地操作错误 |
| URMA\_CR\_LOC\_ACCESS\_ERR | 4 | 本地内存访问错误 |
| URMA\_CR\_REM\_RESP\_LEN\_ERR | 5 | 远端响应长度错误 |
| URMA\_CR\_REM\_UNSUPPORTED\_REQ\_ERR | 6 | 远端不支持请求 |
| URMA\_CR\_REM\_OPERATION\_ERR | 7 | 目标jetty无法完成操作 |
| URMA\_CR\_REM\_ACCESS\_ABORT\_ERR | 8 | 远端访问内存错误或中止操作 |
| URMA\_CR\_ACK\_TIMEOUT\_ERR | 9 | 重传超过最大次数(ACK超时) |
| URMA\_CR\_RNR\_RETRY\_CNT\_EXC\_ERR | 10 | RNR重试超限(远端jfr无buffer) |
| URMA\_CR\_WR\_FLUSH\_ERR | 11 | Jetty错误态，硬件已处理WR |
| URMA\_CR\_WR\_SUSPEND\_DONE | 12 | 硬件构造fake CQE, user\_ctx无效 |
| URMA\_CR\_WR\_FLUSH\_ERR\_DONE | 13 | 硬件构造fake CQE, user\_ctx无效 |
| URMA\_CR\_WR\_UNHANDLED | 14 | flush jetty/jfs返回，硬件未处理WR |
| URMA\_CR\_LOC\_DATA\_POISON | 15 | 本地数据中毒 |
| URMA\_CR\_REM\_DATA\_POISON | 16 | 远端数据中毒 |

### 3.2 ConvertCrStatus 映射表

`ConvertCrStatus(cr_status, op)` — 非成功状态按语义分组映射，`op` 参数当前不参与逻辑：

| urma\_cr\_status\_t | 映射errno | 备注 |
| --- | --- | --- |
| URMA\_CR\_SUCCESS | 0 | 成功 |
| URMA\_CR\_RNR\_RETRY\_CNT\_EXC\_ERR | EAGAIN | 远端jfr无buffer，可重试 |
| URMA\_CR\_ACK\_TIMEOUT\_ERR | ETIMEDOUT | 重传超时 |
| URMA\_CR\_LOC\_ACCESS\_ERR | EACCES | 内存访问错误 |
| URMA\_CR\_REM\_ACCESS\_ABORT\_ERR | EACCES | 远端访问中止 |
| URMA\_CR\_LOC\_LEN\_ERR | EMSGSIZE | 数据过长 |
| URMA\_CR\_REM\_RESP\_LEN\_ERR | EMSGSIZE | 响应长度错误 |
| URMA\_CR\_LOC\_OPERATION\_ERR | EIO | 本地操作错误 |
| URMA\_CR\_REM\_OPERATION\_ERR | EIO | 远端操作错误 |
| URMA\_CR\_WR\_FLUSH\_ERR | ENETRESET | Jetty错误态flush |
| URMA\_CR\_WR\_FLUSH\_ERR\_DONE | ENETRESET | flush完成 |
| URMA\_CR\_LOC\_DATA\_POISON | EIO | 本地数据中毒 |
| URMA\_CR\_REM\_DATA\_POISON | EIO | 远端数据中毒 |
| URMA\_CR\_UNSUPPORTED\_OPCODE\_ERR | EIO | default兜底 |
| URMA\_CR\_REM\_UNSUPPORTED\_REQ\_ERR | EIO | default兜底 |
| URMA\_CR\_WR\_SUSPEND\_DONE | EIO | default兜底 |
| URMA\_CR\_WR\_UNHANDLED | EIO | default兜底 |

***

## 四、设计要点

- **header-only**: `UrmaErrnoConverter` 以 `ALWAYS_INLINE` 形式实现于 `urma_errno_converter.h`，无独立 .cpp
- **纯函数无状态**: 两个Convert入口均不依赖/修改任何全局或单例状态，映射结果仅由入参决定，便于单测覆盖（`urma_errno_converter_test` 仅链接GTest，无mock依赖）
- **同值透传优先**: `urma_status_t` 的多数错误宏本身即Linux errno值，直接映射分支保证这些错误不损失语义；仅无法识别的status才按op兜底
- **op兜底语义分层**: 建立阶段(CONNECT/ACCEPT/IMPORT_JETTY/BIND)失败→ECONNREFUSED，数据通路(WRITEV/READV)失败→EIO，资源创建(CREATE_CTX/CREATE_JETTY)失败→ENODEV

***

## 附录A: Linux errno 检索表 (aarch64)

> 数值来自 Linux `asm-generic/errno-base.h` + `asm-generic/errno.h`（aarch64/x86\_64/所有新架构共用同一源文件，编号相同）。
> 仅收录本文档实际出现的 errno 符号。完整列表参见 [errno(3)](https://man7.org/linux/man-pages/man3/errno.3.html)。

| errno 符号 | 数值 | 标准解释 | 本文用途 |
| --- | --: | --- | --- |
| EIO | 5 | Input/output error | WRITEV/READV兜底; 操作/数据中毒/flush未列出状态兜底 |
| EAGAIN | 11 | Resource temporarily unavailable | URMA\_EAGAIN直接映射; RNR重试超限 |
| ENOMEM | 12 | Cannot allocate memory | URMA\_ENOMEM直接映射 |
| EACCES | 13 | Permission denied | 本地/远端内存访问错误 |
| ENODEV | 19 | No such device | CREATE\_CTX/CREATE\_JETTY兜底 |
| EINVAL | 22 | Invalid argument | URMA\_EINVAL直接映射 |
| EMSGSIZE | 90 | Message too long | 本地长度/远端响应长度错误 |
| ENETRESET | 102 | Network connection dropped because of reset | WR flush错误/完成 |
| ETIMEDOUT | 110 | Connection timed out | URMA\_ETIMEOUT定义值; ACK超时 |
| ECONNREFUSED | 111 | Connection refused | CONNECT/ACCEPT/IMPORT\_JETTY/BIND兜底 |
