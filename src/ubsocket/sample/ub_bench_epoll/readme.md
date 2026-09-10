# ub_bench_epoll

`ub_bench_epoll` 是一个基于 C++11 开发的高性能、多线程网络性能基准压测工具。专为评估**传统 TCP (AF_INET)** 与 **UB (AF_SMC)** 在低延迟、高并发场景下的极限性能与稳定性而设计。

该工具能够提供纳秒级精度的双向往返时延（RTT）统计，并支持高精度的 QPS 定量控速压测。

本工具**不链接** `libubsocket.so`，直接用 `make` 编译即可，运行时通过 `LD_PRELOAD` 无感劫持 socket 接口跑在 UB 通道上，应用侧零修改。

---

## 🏗️ 核心架构与设计

### 1. 线程模型：主从 Reactor (Single Acceptor + Multi-Workers)

传统的 `SO_REUSEPORT` 多线程监听机制在处理 `AF_SMC` 协议栈时，由于底层的 CLC (Connection Layer Control) 握手协议阶段存在复杂的状态机交互，在高并发建链时极易触发握手冲突，导致连接被内核重置（Connection reset by peer）。

为了彻底解决这一痛点，本工具采用了**主从 Reactor 架构**：

*   **独立 Acceptor 线程**：独占监听套接字（Listen FD），负责串行地接收所有入向连接。这种单点接收机制天然抹平了底层协议栈并发握手的冲突可能。
*   **无锁事件分发 (Eventfd 唤醒)**：Acceptor 线程通过轮询算法（Round-Robin）决定目标工作线程，将新接受的客户端套接字追加到该 Worker 的队列中，并通过 `eventfd` 写入 8 字节信号唤醒目标工作线程。
*   **多线程 Worker 运行**：每个 Worker 线程维护自己独立的 `epoll` 实例，负责处理已分发连接的读写（Echo 反弹）业务，做到了线程间的完全隔离与无锁化运行。

### 2. 并发窗口与网络模型

客户端采用 **Ping-Pong 模型（并发窗口为 1）**。每个测试线程在连接建立后，必须完整接收到上一个发送包的对端回显（Echo），才会触发下一轮的数据发送。这种模式能最真实地榨干单条连接/单核路径上的绝对物理时延极限。

### 3. epoll 边缘触发（ET）约束

UBSocket 的 `epoll_wait` **仅支持边缘触发模式**。为此本工具遵循以下编码约束：

*   所有 fd 在建链后立即 `SetNonBlocking`（`fcntl(F_GETFL/F_SETFL | O_NONBLOCK)`）。
*   `EPOLLIN` 到达后必须**循环 `recv` 直到返回 `EAGAIN`/`EWOULDBLOCK`** 才退出，否则 ET 模式下剩余数据不会再触发事件（见 `ub_bench_epoll.cpp` 收包循环）。
*   写路径同样要循环 `send` 直到 `EAGAIN`，并在 `EAGAIN` 时停止本轮发送、等下一次 `EPOLLOUT`。
*   本工具的连接 fd 采用**常驻注册** `EPOLLIN | EPOLLOUT | EPOLLET`（见 `cev.events = EPOLLIN | EPOLLOUT | EPOLLET`），写就绪由内核持续上报；发送侧在收到 `EAGAIN` 后自然退让，不会 busy loop。若需进一步降低空转，可改为"仅在 `EAGAIN` 时注册 `EPOLLOUT`、写完后摘除"的按需注册模式。
*   监听 fd 仅注册 `EPOLLIN`（**非 ET**），由 Acceptor 串行 `accept` 并循环到 `EAGAIN`。
*   每个 Worker 一个独立 `epoll_fd`，互不干扰。

---

## 📊 统计学原理与控速机制

### 1. 时延测量原理 (RTT)

工具内部基于 `<chrono>` 实现了纳秒级的计时。

*   **起点记录**：在客户端触发 `EPOLLOUT` 事件，向内核缓冲区写入请求数据的**第一个字节前**，捕获当前时间戳 `msg_start`。
*   **终点记录**：在客户端触发 `EPOLLIN` 事件，通过循环 `recv` 累加收满完整长度（`--size`）数据包的**最后一个字节后**，捕获结束时间戳 `msg_end`。

$$ \text{时延 (RTT)} = \text{msg\_end} - \text{msg\_start} $$

> **📌 关于单程时延 (One-Way Latency) 的说明**
> 本工具输出的为 **双向往返时延 (Round-Trip Time, RTT)**。在网络路径对称（去回程路由一致）的理想情况下，单程时延大约等于 RTT 除以 2。本工具未采用两端时间戳相减来算绝对单程，从而规避了分布式系统高频测试下对 PTP/NTP 硬件时钟同步的苛刻依赖。

### 2. 高精度 QPS 限速机制

当用户指定 `--qps` 参数时，工具会启用基于 Linux 高精度 `nanosleep` 的均摊控速算法：

1.  **QPS 均摊**：全局目标 QPS 会被均匀分配给 `--threads` 指定的每一个客户端线程：每个线程的 QPS = 全局 QPS / 线程数。
2.  **目标时间周期**：每个线程计算出发送一个数据包应占据的理论目标间隔：目标间隔(纳秒) = 1,000,000,000 / 线程的 QPS。
3.  **动态差额睡眠**：在完成一次 Ping-Pong 交互后，计算整个周期的实际耗时。若耗时小于目标间隔，则线程使用 `nanosleep` 精确挂起剩余的纳秒差额。若耗时已超过目标间隔，则不进行睡眠，立刻进行下一轮迭代，这能有效防止因网络波动导致发送率无限堆积。

---

## 🛠️ 构建与测试步骤

`ub_bench_epoll` 自身只依赖系统 libc/pthread，但要跑在 UB 通道上，必须先构建出 **UMQ** 与 **UBSocket** 的动态库。

下文用 `${UBS_COMM_ROOT}` 表示仓库根目录（即包含 `src/` 的那一层），实际使用时替换成自己的路径，或先 `export UBS_COMM_ROOT=<你的仓库根目录>`。

### 步骤 1：构建 UMQ

```bash
cd ${UBS_COMM_ROOT}/src/hcom/umq
mkdir -p build && cd build
cmake -DUSE_URMA_STUB=OFF ..
make -j32
```

*   `USE_URMA_STUB=OFF`：链接真实 URMA 驱动；置为 `ON` 则使用 CI 环境的 stub。

产出三个动态库：

| 库 | 路径 |
| :--- | :--- |
| `libumq.so` | `src/hcom/umq/build/src/` |
| `libumq_ub.so` | `src/hcom/umq/build/src/umq_ub/` |
| `libumq_buf.so` | `src/hcom/umq/build/src/qbuf/` |

### 步骤 2：构建 UBSocket

```bash
cd ${UBS_COMM_ROOT}/src/ubsocket
mkdir -p build && cd build
cmake -DUBSOCKET_ENABLE_INTERCEPT=ON -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
make -j32
```

| 选项 | 说明 |
| :--- | :--- |
| `UBSOCKET_ENABLE_INTERCEPT=ON` | **必须**。开启 POSIX socket 接口的 LD_PRELOAD 拦截，默认 `OFF`；不开启则 `LD_PRELOAD` 无法劫持 |
| `CMAKE_BUILD_TYPE=RELEASE` | 仅支持 `RELEASE` 与 `ASAN`，填其它值会直接报错退出 |
| `CMAKE_EXPORT_COMPILE_COMMANDS=ON` | 可选，生成 `compile_commands.json` 供 clangd 等工具使用 |
| `UBSOCKET_PROF_ENABLE` 等 | 其余编译开关见 `src/ubsocket/CMakeLists.txt` 的 `option()` 列表 |

产出 `src/ubsocket/build/csrc/libubsocket.so`，这就是运行时要 `LD_PRELOAD` 的库。

### 步骤 3：构建 ub_bench_epoll

```bash
cd ${UBS_COMM_ROOT}/src/ubsocket/sample/ub_bench_epoll
make
```

*   Makefile：`CC = g++`，`CFLAGS = -std=c++11 -Wall -Wextra -O2 -pthread`，源码 `ub_bench_epoll.cpp`，产物 `ub_bench_epoll`。
*   `make clean` 可清理产物。
*   `-O2`/`-O3` 优化对于降低压测工具自身的代码执行开销、确保高频测试下的微秒级时延准确性至关重要。

### 步骤 4：设置库搜索路径

UBSocket 通过 dlopen/LD_PRELOAD 方式依赖 UMQ，运行前需把 UMQ 的三个库目录加入 `LD_LIBRARY_PATH`：

```bash
export LD_LIBRARY_PATH=${UBS_COMM_ROOT}/src/hcom/umq/build/src:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${UBS_COMM_ROOT}/src/hcom/umq/build/src/umq_ub:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${UBS_COMM_ROOT}/src/hcom/umq/build/src/qbuf:${LD_LIBRARY_PATH}
```

> 若跳过这一步，运行时会报 `libumq.so: cannot open shared object file` 之类的错误。

### 步骤 5：运行

先按步骤 4 导出 `LD_LIBRARY_PATH`，再照下节「UB 模式运行指南」的命令启动服务端与客户端。

### 一键串联脚本

```bash
export UBS_COMM_ROOT=<你的仓库根目录>

# 1) UMQ
cd ${UBS_COMM_ROOT}/src/hcom/umq && mkdir -p build && cd build
cmake -DUSE_URMA_STUB=OFF .. && make -j32

# 2) UBSocket
cd ${UBS_COMM_ROOT}/src/ubsocket && mkdir -p build && cd build
cmake -DUBSOCKET_ENABLE_INTERCEPT=ON -DCMAKE_BUILD_TYPE=RELEASE -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && make -j32

# 3) ub_bench_epoll
cd ${UBS_COMM_ROOT}/src/ubsocket/sample/ub_bench_epoll && make

# 4) 运行时库搜索路径
export LD_LIBRARY_PATH=${UBS_COMM_ROOT}/src/hcom/umq/build/src:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${UBS_COMM_ROOT}/src/hcom/umq/build/src/umq_ub:${LD_LIBRARY_PATH}
export LD_LIBRARY_PATH=${UBS_COMM_ROOT}/src/hcom/umq/build/src/qbuf:${LD_LIBRARY_PATH}
```

---

## 📋 命令行参数汇总

工具第一个参数必须为角色定位：`sr` 代表服务端，`pp` 代表 Ping-Pong 客户端。

| 参数 | 适用角色 | 默认值 | 参数说明 | 示例 |
| :--- | :--- | :--- | :--- | :--- |
| `sr` / `pp` | **必填** | 无 | `sr`: 服务端模式；`pp`: 压测客户端模式。 | `sr` |
| `--trans` | 两端 | `ub`（`AF_SMC`） | 传输协议栈选择。`tcp` → `AF_INET`；其余值 → `AF_SMC`。 | `--trans tcp` |
| `--tcp` / `--ub` | 两端 | 无 | `--trans` 的快捷显式简写方式。 | `--ub` |
| `--threads` | 两端 | `1` | 服务端: Worker 工作线程数；客户端: 并发测试连接数。 | `--threads 16` |
| `-p` / `--port` | 两端 | `11111` | 服务端监听的端口，或客户端连接的目标端口。 | `-p 9999` |
| `-i` / `--ip` | 两端 | `0.0.0.0` | 服务端绑定的网卡 IP，或客户端连接的目标服务器 IP（客户端传 `0.0.0.0` 会自动改为 `127.0.0.1`）。 | `-i 141.61.17.202` |
| `--size` | 两端 | `1024` | 单次传输的数据包大小（单位：字节）。 | `--size 16384` |
| `--recv-api` | 两端 | `recv` | 收包所用的系统调用。`recv` → `recv()`；`readv` → `readv()`（内部包成 1 个 `iovec`）。**服务端与客户端同时生效**，可用于对比两条收包路径的时延差异，详见下方说明。 | `--recv-api readv` |
| `--time` | 仅客户端 | `10.0` | 测试持续的绝对时间（单位：秒）。指定后会把 `--count` 清零。 | `--time 60` |
| `--count` | 仅客户端 | `0` | 测试的总消息发送频次。设定该值后将覆盖时间控制。 | `--count 1000000` |
| `--qps` | 仅客户端 | `0` | 全局吞吐率限制（QPS）。`0` 表示不限速，以最大极限压测。 | `--qps 16000` |

> 所有 `key value` 形式的参数也都支持 `key=value` 写法（如 `--threads=16`）。

### `--recv-api`：`recv` 与 `readv` 两条收包路径

UBSocket 对 `recv` 和 `readv` 都做了劫持，但二者内部走的是**不同的两条分支**。本选项用于在 demo 中显式对比它们：

| 取值 | 实际调用 | LD_PRELOAD 模式下内部落点 | 说明 |
| :--- | :--- | :--- | :--- |
| `recv`（默认） | `recv(fd, buf, len, 0)` | `recv_copy` | 与本次改动前的行为完全一致，缺省即为该值 |
| `readv` | `readv(fd, &iov, 1)` | `readv_copy` | 包成单个 `iovec`，仅系统调用入口不同，数据流向等价 |

- **两端生效**：服务端回显前的收包、客户端等待应答的收包，由同一个选项同时切换。
- **建议两端取相同值**：若一端 `recv`、另一端 `readv`，测到的是混合路径而非单侧差异。
- **发送侧不受影响**：始终使用 `writev()`，本选项只切换收包。
- 统计输出的小节标题会随选项变化——选 `readv` 时打印 `Readv API Time` 而非 `Recv API Time`。
- 仅在 LD_PRELOAD（`--ub` / UB 模式）下才落到了 UBSocket 的 `*_copy` 分支；纯 TCP 且未预加载时，二者只是内核原生系统调用的差异。

缺省即 `recv`，因此**原有命令行无需任何改动**，输出结果与之前保持一致。

---

## 🚀 UB 模式运行指南 (LD_PRELOAD)

`ub_bench_epoll` 本体**不链接** `libubsocket.so`，而是通过 `LD_PRELOAD` 让动态链接器在运行时劫持 `socket/writev/recv/readv/epoll_wait` 等符号，应用零修改即可跑在 UB 通道上。因此**必须**同时设置 `LD_PRELOAD` 与 UBSocket 的环境变量。

### 1. 环境变量说明

| 环境变量 | 取值 | 默认值 | 说明 |
| :--- | :--- | :--- | :--- |
| `LD_PRELOAD` | so 路径 | 无 | 预加载 `libubsocket.so`，劫持标准 socket 接口 |
| `UBSOCKET_DEV_NAME` | 设备名，最长 64 字符 | 空（默认 bonding 设备） | 指定 UB 设备，普通 udma 设备调试时用 `urma_admin show` 查看，如 `udmac0d1e2` |
| `UBSOCKET_UB_TRANS_MODE` | `RC_TP` / `RM_TP` / `RM_CTP` / `RC_CTP` | `RM_CTP` | UB 协议模式 |
| `UBSOCKET_JETTY_TYPE` | `single` / `pool` | `pool` | Jetty 连接复用方式 |
| `UBSOCKET_JETTY_POOL_SIZE` | `[1, 1000]` | `800` | Jetty 连接池大小（`pool` 模式生效，会占用 fd） |
| `UBSOCKET_SHARE_JFR_ENABLE` | `true` / `false` | `true` | 是否开启共享 JFR（独立 Jetty 场景） |
| `UBSOCKET_FLOW_CONTROL_ENABLE` | `true` / `false` | `true` | 是否开启流控 |
| `UBSOCKET_LINK_PRIORITY` | `[-1, 15]`，`-1` 表示不设置 | `4` | 流量优先级，映射到 SL |
| `UBSOCKET_POOL_INITIAL_SIZE` | 正整数（MB） | — | 初始化时分配的 UB 内存池大小，必须配足否则初始化失败 |
| `UBSOCKET_POOL_MAX_SIZE` | `[INITIAL_SIZE + 64, 6144]`（MB） | `2048` | 内存池弹性扩容上限，单次最小扩容 64M，故 `MAX - INITIAL >= 64` |
| `UBSOCKET_BUF_POOL_DEPTH` | 正整数 | `24576` | 单进程线程内存池深度 |
| `UBSOCKET_DEGRADE_ENABLE` | `true` / `false` | `true` | UB 出错时是否允许降级成 TCP |
| `UBSOCKET_UB_HANDSHAKE_MODE` | `tfo` / `ub_sock_opt` | `ub_sock_opt` | UB 建链握手模式 |
| `UBSOCKET_PROF_ENABLE` | `true` / `false` | `false` | 是否打开 profiling 打点 |
| `UBSOCKET_PROF_MODE` | `fast` / `ext` | `fast` | 是否打开 profiling 百分位统计 |

> ⚠️ **`UBSOCKET_TRANS_MODE=ub` 不是本仓库 ubsocket 的配置项**（`src/` 下未读取该变量，也未被 `doc/ubsocket/UBSOCKET-USER-GUIDE.md` 收录）。下例中保留它是沿用既有启动习惯，实际是否生效取决于外部适配层；UB 通道的开启由命令行的 `--ub`（`AF_SMC`）决定。
>
> ⚠️ 旧版本 readme 中出现的 `UBSOCKET_ENABLE_SHARE_JFR` 是**错误名称**，正确名称为 `UBSOCKET_SHARE_JFR_ENABLE`。

> 完整的环境变量清单与取值约束见 `doc/ubsocket/UBSOCKET-USER-GUIDE.md`。

### 2. 完整启动命令

**① 服务端（sr）** —— 绑核到 CPU 96-111，监听 9999 端口，16 个 Worker：

```bash
taskset -ac 96-111 env LD_PRELOAD=${UBS_COMM_ROOT}/src/ubsocket/build/csrc/libubsocket.so \
  UBSOCKET_LINK_PRIORITY=0 \
  UBSOCKET_POOL_INITIAL_SIZE=2048 \
  UBSOCKET_POOL_MAX_SIZE=2112 \
  UBSOCKET_SHARE_JFR_ENABLE=false \
  UBSOCKET_UB_TRANS_MODE=RC_TP \
  UBSOCKET_JETTY_TYPE=single \
  UBSOCKET_TRANS_MODE=ub \
  UBSOCKET_FLOW_CONTROL_ENABLE=false \
  UBSOCKET_DEV_NAME="udmac0d1e2" \
  ./ub_bench_epoll sr -i 141.61.17.202 -p 9999 --time 60 --size 16384 --threads 16 --ub
```

**② 客户端（pp）** —— 同样绑核 96-111，16 并发连接，16KB 包长，压测 60 秒，全局限速 16000 QPS：

```bash
taskset -ac 96-111 env LD_PRELOAD=${UBS_COMM_ROOT}/src/ubsocket/build/csrc/libubsocket.so \
  UBSOCKET_LINK_PRIORITY=0 \
  UBSOCKET_SHARE_JFR_ENABLE=false \
  UBSOCKET_UB_TRANS_MODE=RC_TP \
  UBSOCKET_JETTY_TYPE=single \
  UBSOCKET_TRANS_MODE=ub \
  UBSOCKET_POOL_INITIAL_SIZE=2047 \
  UBSOCKET_POOL_MAX_SIZE=2112 \
  UBSOCKET_FLOW_CONTROL_ENABLE=false \
  UBSOCKET_DEV_NAME="udmac0d1e2" \
  ./ub_bench_epoll pp -i 141.61.17.202 -p 9999 --time 60 --size 16384 --threads 16 --ub --qps 16000
```

说明：

*   `taskset -ac 96-111` 中的 `-a` 表示对进程内所有线程生效，`-c` 后接 CPU 列表；绑核可避免跨 NUMA 与调度抖动，压测时务必固定。
*   服务端与客户端的 `--threads` 建议保持一致（本例均为 16，对应用需求中"最大 16 线程并发"）。
*   两端 `--size` 必须一致，否则收端无法收满一个完整包，RTT 统计会失真。
*   服务端命令里的 `--time 60` 实际不生效（仅客户端使用），保留无副作用。
*   客户端 `UBSOCKET_POOL_INITIAL_SIZE=2047` 与服务端 `2048` 不一致，疑为笔误；两者均满足 `MAX - INITIAL >= 64`，但建议统一为 `2048`。
*   若只跑 TCP 对照基线，去掉 `LD_PRELOAD` 与所有 `UBSOCKET_*` 变量，并把 `--ub` 换成 `--tcp` 即可。

---

## 📊 运行回显示例

### 1. 服务端运行日志 (sr)

```text
[INFO] === UBSocket bench server (sr) | threads=16 port=9999 trans=ub size=16384 ===
[INFO] acceptor thread: listening on 141.61.17.202:9999 (trans=ub)
[INFO] client thread 82410: connected to 141.61.17.202:9999
[INFO] client thread 82411: connected to 141.61.17.202:9999
... (省略部分连接接入日志)
```

### 2. 客户端运行日志 (pp)

```text
[INFO] === UBSocket bench client (pp) | threads=16 ip=141.61.17.202 port=9999 trans=ub size=16384 qps=16000 time ===
[INFO] client thread 82410: connected to 141.61.17.202:9999
[INFO] client thread 82411: connected to 141.61.17.202:9999
... (16 个测试线程建链成功)

[INFO] === client total: msgs=151584 bytes=9934209024 time=10.246s throughput=14794.23 msg/s, 7397.12 Mbps ===
[INFO] === Latency Stats (RTT) ===
[INFO]   Min:        52.800 us
[INFO]   Avg:        76.002 us
[INFO]   P50:        71.200 us
[INFO]   P90:        95.800 us
[INFO]   P99:       116.210 us
[INFO]   P99.9:     138.750 us
[INFO]   Max:     93325.830 us
```

> **💡 结果解读（以上述 64KB 大包回显为例）：**
> *   **总吞吐率 (Total Throughput)**：约 $\approx 1.5\text{ 万 QPS}$。
> *   **总带宽 (Total Bandwidth)**：约 $\approx 7.4\text{ Gbps}$。
> *   **延迟表现 (RTT)**：平均往返延迟为 **76.002 微秒**，P50 为 **71.200 微秒**，P99 时延 **116.210 微秒**。
>
> 输出分位为 **Min / Avg / P50 / P90 / P99 / P99.9 / Max**，覆盖需求要求的平均 / P50 / P90 / P99 / PMAX。
> 分位由 `PrintLatencyStats` 对排序后的样本数组按下标取整计算（`size * 0.50/0.90/0.99/0.999`），非插值。

---

## 🔍 常见问题

| 现象 | 排查方向 |
| :--- | :--- |
| 初始化失败 / 报内存池不足 | 调大 `UBSOCKET_POOL_INITIAL_SIZE`；确认 `UBSOCKET_POOL_MAX_SIZE - INITIAL >= 64` |
| 跑的是 TCP 而不是 UB | 确认命令行带 `--ub`（走 `AF_SMC`），且 `LD_PRELOAD` 指向正确的 `libubsocket.so` |
| `Connection reset by peer` | 检查是否走了 `SO_REUSEPORT` 多监听（本工具已用单 Acceptor 规避）；确认两端 `--size` 一致 |
| 吞吐上不去 | 检查是否绑核（跨 NUMA 影响大）、`UBSOCKET_LINK_PRIORITY`、流控开关 `UBSOCKET_FLOW_CONTROL_ENABLE` |
| Max 时延异常大（如 93ms） | 多为首包建链/内存池扩容抖动，可用 `--time` 延长压测窗口观察稳态分位 |
