## 1. 配置项 (Settings)

### 1.1 UrmaSetting — URMA 模块配置类

定义: `urma/urma_setting.h`，默认值与环境变量加载: `urma/urma_setting.cpp`，`UrmaSetting::Init()` 启动时加载。

| 配置项目或变量名 | 类型 | 默认值 | 取值范围 | 说明 | 是否对外 | 环境变量名 | brpc变量名 | 处理 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `UBS_DEV_NAME` | std::string | "" | 非空(必填) | UB 设备名称；指定后按 EID_INDEX 精确选卡，为空自动选卡 | 是 | `UBS_DEV_NAME` | `ubsocket_ub_dev`(默认空=不覆盖env) | — |
| `UBS_DEV_EID` | std::string | 空 | — | 仅赋默认值，范围内无消费方 | 否 | — | — | — |
| `UBS_DEV_EID_INDEX` | uint32_t | 0 | 0-256 | EID 序号选择 | 是 | `UBS_DEV_EID_INDEX` | `ubsocket_ub_eid_idx` | — |
| `UBS_TRANS_MODE` | ub_trans_mode | RM_CTP | RC_TP/RM_TP/RM_CTP/RC_CTP | 传输模式，conn/acceptor/connector ops 创建选项 | 否 | — | — | — |
| `UBS_LINK_PRIORITY` | int8_t | -1 (NOT_SET) | -1~15（-1=不设置，建链换算为 UINT8_MAX 交由底层默认） | link/jetty 优先级（SL），经 CreateJfsJfrJetty 传入 jetty 创建 | 是 | `UBS_LINK_PRIORITY` | `ubsocket_link_priority`(默认空=不覆盖env) | — |
| `UBS_EPOLL_MODE` | EpollHandleMode | THREAD_PER_CONNECTION | 0-1 (0=独立poller线程, 1=集成到epoll) | poller 线程模型 | 是 | `UBS_EPOLL_MODE` | `ubsocket_epoll_handle_mode` | — |
| `UBS_POLLER_COUNT` | uint16_t | 2 | 1-16 | 每 context 创建 poller 数，仅 MODE 0 生效（≤16 为消费层硬约束） | 是 | `UBS_POLLER_COUNT` | `ubsocket_ub_poller_count` | — |
| `UBS_POLLER_CQ_DEPTH` | uint32_t | 16384 | ≥4096 | CQ 队列深度（上限受设备 max_jfc_depth） | 是 | `UBS_POLLER_CQ_DEPTH` | `ubsocket_ub_poller_cq_depth` | — |
| `UBS_POLLER_BUSY_POLLING` | bool | false | 0-1 | 独立线程 poller 下的忙轮询模式（env 入口已去除，恒默认值） | 否 | — | — | — |
| `UBS_BUF_POOL_SPACE_SIZE` | uint32_t | 32 (GB) | — | 内存池总大小 | 否 | — | — | — |
| `UBS_BUF_EXPAND_SIZE_MB` | uint16_t | 16 (MB) | — | GC 申请内存块粒度 | 否 | — | — | — |
| `UBS_BUF_INIT_SIZE_MB` | uint16_t | 256 (MB) | — | tc 初始化内存大小 | 否 | — | — | — |
| `UBS_BUF_PAGE_SIZE_KB` | std::string | "large" | "large"(64K) / "tiny"(4K, 纯CTP) | 内存池页/块类型（urma 页粒度与 umq 块粒度共用，档位换算在各池消费点执行，两档均整除 16MB block） | 是 | `UBS_BUF_PAGE_SIZE_KB` | `ubsocket_ub_buf_page_type` | — |
| `UBS_BUF_ASYNC_EXPAND` | bool | true | — | 内存池异步扩容 | 否 | — | — | — |
| `UBS_BAD_UBPORT_TTL` | uint64_t | 300 | — | 故障端口冷却时间 | 否 | — | — | — |
| `UBS_KEEPALIVE_TIME` | uint32_t | 180 | — | keepalive 空闲触发时间，0=禁用（env 入口已去除，恒默认值） | 否 | — | — | — |
| `UBS_KEEPALIVE_INTVL` | uint32_t | 0 | — | 触发后探测周期，当前不使用（env 入口已去除，恒默认值） | 否 | — | — | — |
| `UBS_KEEPALIVE_PROBES` | uint32_t | 1 | — | 探测次数上限，超过断链，目前固定为 1（env 入口已去除，恒默认值） | 否 | — | — | — |

### 1.2 GlobalSetting — 全局配置类

定义: `common/ubsocket_global_setting.h`，默认值与环境变量加载: `ubsocket_global_setting.cpp`（`LoadEnv()`/`AddRules()`）。

| 配置项目或变量名 | 类型 | 默认值 | 取值范围 | 说明 | 是否对外 | 环境变量名 | brpc变量名 | 处理 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `UBS_ALLOWED_PROTOCOL` | uint32_t | 0 | 协议位集 | 允许的协议（`UBS_PROTOCOL_TCP`/`UBS_PROTOCOL_UB_RM_RTP`/`UBS_PROTOCOL_UB_RC_RTP`） | 是(API) | — | — | — |
| `UBS_NATIVE_TCP_MODE` | bool | false | true/false | 纯 TCP 透传模式；`UBS_ALLOWED_PROTOCOL==TCP` 时 LoadEnv 自动置 true | 是(API/派生) | — | — | — |
| `UBS_CLI_ENABLED` | bool | true | true/false | CLI 监听使能（env 入口已去除，恒默认值） | 否 | — | — | — |
| `UBS_PROF_ENABLE` | bool | false | true/false | 性能打点开关 | 是 | `UBS_PROF_ENABLE` | `ubsocket_prof_enable` | — |
| `UBS_PROF_MODE` | std::string | "fast" | fast/ext | 打点模式（fast 高性能 7 字段, ext 含百分位 11 字段） | 是 | `UBS_PROF_MODE` | `ubsocket_prof_mode` | — |
| `UBS_PROF_DUMP_INTERVAL_MIN` | uint16_t | 1 | 1-5 | 打点 dump 周期（分钟） | 是 | `UBS_PROF_DUMP_INTERVAL_MIN` | `ubsocket_prof_dump_interval_min` | — |
| `UBS_PROF_DUMP_PATH` | std::string | "/tmp/ubsocket/profiling" | 非空,长度≤512 | 打点 dump 路径 | 是 | `UBS_PROF_DUMP_FILE_PATH` | `ubsocket_prof_dump_file_path` | — |
| `UBS_OPT_LINK_HANDSHAKE_ENABLE` | bool | true | TRUE/FALSE | UB 通信建链握手开关：TRUE=UB 优化链路握手（ub_sock_opt，默认）；FALSE=TFO 握手 | 是 | `UBS_OPT_LINK_HANDSHAKE_ENABLE` | `ubsocket_opt_link_handshake_enable` | — |
| `UBS_ENABLE_DEGRADE` | bool | true | true/false | 允许降级 TCP（env 入口已去除，恒默认值） | 否 | — | — | — |
| `UBS_BACKUP_LINK_ENABLED` | bool | true | true/false | 主备链路开关（env 入口已去除，恒默认值） | 否 | — | — | — |
| `UBS_READV_UNLIMITED` | bool | true | true/false | readv 读取 max_size 开关（env 入口已去除，恒默认值） | 否 | — | — | — |
| `UBS_DIRECT_IO_ENABLE` | bool | false | true/false | 非 ubsocket 托管 fd（sock 表查不到）在 accept/listen/connect/readv/writev/send 等入口直接透传 libc，避免误报 EBADF（rpc_dump 等自建 fd 工具场景，上游 !113 引入） | 是 | `UBS_DIRECT_IO_ENABLE` | `ubsocket_direct_io_enable` | — |
| `UBS_CONNECTS_MODE` | ConnMode | "full_async" | "sync" / "half_async" / "full_async" | 建链模式（server/client 共用一档）：sync=两侧同步；half_async=server 半异步线程池（client 不支持半异步，回退同步）；full_async=server 全异步 reactor + client 全异步 connect。env/gflag 字符串经 `ConnModeFromStr` 解析为枚举，调用点经 `GetConnMode()` 取值比较 | 是 | `UBS_CONNECTS_MODE` | `ubsocket_connects_mode` | — |
| `UBS_CONNECTS_POLL_THREAD_NUM` | uint32_t | 1 | ≥1 | 统一建链线程数：半异步 ExecutorService 与全异步 ConnDispatcher 共用（旧名 `UBSOCKET_SERVER_ASYNC_HANDLER_NUM` 兼容读取） | 是 | `UBS_CONNECTS_POLL_THREAD_NUM` | `ubsocket_connects_poll_thread_num` | — |
