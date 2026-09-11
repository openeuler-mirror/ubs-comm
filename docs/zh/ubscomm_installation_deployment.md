# 安装部署

## 环境要求<a name="ZH-CN_TOPIC_0000002565998390"></a>

**硬件要求<a name="section69005151510"></a>**

**表 1** 硬件要求<a id="硬件要求"></a>

<a name="zh-cn_topic_0000001614371366___d0e1171"></a>
<table><tbody><tr id="zh-cn_topic_0000002555962033_row310mcpsimp"><th class="firstcol" valign="top" width="27%" id="mcps1.2.3.1.1"><p id="zh-cn_topic_0000002555962033_p312mcpsimp"><a name="zh-cn_topic_0000002555962033_p312mcpsimp"></a><a name="zh-cn_topic_0000002555962033_p312mcpsimp"></a>服务器名称</p>
</th>
<td class="cellrowborder" valign="top" width="73%" headers="mcps1.2.3.1.1 "><p id="zh-cn_topic_0000002555962033_p314mcpsimp"><a name="zh-cn_topic_0000002555962033_p314mcpsimp"></a><a name="zh-cn_topic_0000002555962033_p314mcpsimp"></a>TaiShan服务器</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002555962033_row315mcpsimp"><th class="firstcol" valign="top" width="27%" id="mcps1.2.3.2.1"><p id="zh-cn_topic_0000002555962033_p317mcpsimp"><a name="zh-cn_topic_0000002555962033_p317mcpsimp"></a><a name="zh-cn_topic_0000002555962033_p317mcpsimp"></a>处理器</p>
</th>
<td class="cellrowborder" valign="top" width="73%" headers="mcps1.2.3.2.1 "><p id="zh-cn_topic_0000002555962033_p319mcpsimp"><a name="zh-cn_topic_0000002555962033_p319mcpsimp"></a><a name="zh-cn_topic_0000002555962033_p319mcpsimp"></a>鲲鹏处理器</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002555962033_row187781906534"><th class="firstcol" valign="top" width="27%" id="mcps1.2.3.3.1"><p id="zh-cn_topic_0000002555962033_zh-cn_topic_0000001614371366_p769mcpsimp"><a name="zh-cn_topic_0000002555962033_zh-cn_topic_0000001614371366_p769mcpsimp"></a><a name="zh-cn_topic_0000002555962033_zh-cn_topic_0000001614371366_p769mcpsimp"></a>网卡</p>
</th>
<td class="cellrowborder" valign="top" width="73%" headers="mcps1.2.3.3.1 "><p id="zh-cn_topic_0000002555962033_p1426061222412"><a name="zh-cn_topic_0000002555962033_p1426061222412"></a><a name="zh-cn_topic_0000002555962033_p1426061222412"></a>Mellanox CX5 (仅使用RDMA通信协议时必须，使用其他通信协议不需要)</p>
</td>
</tr>
<tr id="zh-cn_topic_0000002555962033_row97552279531"><th class="firstcol" valign="top" width="27%" id="mcps1.2.3.4.1"><p id="zh-cn_topic_0000002555962033_p22811117141412"><a name="zh-cn_topic_0000002555962033_p22811117141412"></a><a name="zh-cn_topic_0000002555962033_p22811117141412"></a>CPU</p>
</th>
<td class="cellrowborder" valign="top" width="73%" headers="mcps1.2.3.4.1 "><p id="zh-cn_topic_0000002555962033_p1928119174141"><a name="zh-cn_topic_0000002555962033_p1928119174141"></a><a name="zh-cn_topic_0000002555962033_p1928119174141"></a>通过系统文件<span class="filepath" id="zh-cn_topic_0000002555962033_filepath1937013192419"><a name="zh-cn_topic_0000002555962033_filepath1937013192419"></a><a name="zh-cn_topic_0000002555962033_filepath1937013192419"></a>“/sys/devices/system/cpu/cpu0/regs/identification/midr_el1”</span>中获取CPU厂商信息判断，当前配套机型鲲鹏处理器型号为0x48。</p>
</td>
</tr>
</tbody>
</table>

**软件版本<a name="section15923759174210"></a>**

**表 2** 软件要求<a id="软件要求"></a>

|软件名称|软件版本|
|--|--|
|OS|openEuler 22.03 LTS<br>openEuler 24.03 LTS|
|RDMA-Core|42.7|
|GCC|12.3.1（openEuler 24.03 LTS 默认）|
|CCA|VPP V300R024C10SPC001|

## 节点规划

所有集群节点均需安装`ubs-comm`主包。

## 安装步骤

**前提条件<a name="section1340093619408"></a>**

前置依赖可通过 dnf 安装：

```cmd
$ dnf install -y cmake gcc gcc-c++ make git rdma-core-devel openssl-devel libboundscheck time
```

libboundscheck 也可通过源码安装：

    [https://gitee.com/openeuler/libboundscheck](https://gitee.com/openeuler/libboundscheck)

    1. 下载发行版本。最新的release版本是v1.1.16。

        [https://gitee.com/openeuler/libboundscheck/releases/tag/v1.1.16](https://gitee.com/openeuler/libboundscheck/releases/tag/v1.1.16)

    2. 编译。

        ```cmd
        make CC=gcc
        ```

    3. 编译后根目录下lib目录中，存在libboundscheck.so。

        ```cmd
        cp lib/libboundscheck.so /usr/lib64
        ```

> 说明：openEuler 24.03 系统 dnf 仓库提供的 libboundscheck 版本为 v1.1.11，可直接用于编译和运行；仅当需要 v1.1.16 的特定修复时才需按上述步骤源码编译。

**操作步骤<a name="section078195334012"></a>**

若环境上已安装UBS Comm，则先卸载再安装，否则直接安装即可。

1. 执行以下命令，卸载rpm。

    ```cmd
    rpm -e ubs-comm-lib-1.0.0
    rpm -e ubs-comm-devel-1.0.0
    ```

2. 执行以下命令，安装rpm。

    ```cmd
    rpm -ivh ubs-comm-lib-1.0.0-*rpm --force
    rpm -ivh ubs-comm-devel-1.0.0-*rpm --force (开发包，运行时可选)
    ```

## 容器镜像部署（可选）<a name="容器镜像部署可选"></a>

容器环境部署有两种方式：

- 基于 openEuler 基础环境从零安装
- 基于镜像构建容器环境

### 方式一：基于 openEuler 基础环境从零安装

不使用镜像时，参照本章「前提条件」安装依赖后，在ubs-comm源码根目录执行：

```cmd
UMQ_BUILD=on UBSOCKET_BUILD=on ./build.sh
```

> 说明：该方式需手动安装全部构建依赖（见「前提条件」），耗时较长。

### 方式二：基于镜像构建容器环境

基于镜像构建容器环境，首先需要获取镜像。获取镜像有两种方式：

- 直接从镜像仓库拉取预构建镜像
- 从 Dockerfile 构建镜像

**步骤 1：获取镜像**

先确认本机 CPU 架构，不同架构的镜像获取方式不同：

```cmd
$ uname -m    # x86_64 或 aarch64
```

> 架构说明：
>
> - 预构建镜像当前**仅提供 x86_64（amd64）架构**，仅适用于 x86_64 机器。
> - aarch64（Kunpeng）机器请使用选项二（Dockerfile 构建）：`docker/Dockerfile` 的 `FROM hub.oepkgs.net/openeuler/openeuler:24.03-lts-sp3` 为多架构镜像（amd64/arm64/loong64），`docker build` 会自动拉取与本机架构匹配的基础镜像，无需修改 Dockerfile。

选项一：直接拉取预构建镜像（仅 x86_64）

```cmd
docker pull swr.cn-north-4.myhuaweicloud.com/ubscore/ubs-comm-openeuler:24.03-sp3-1.0.0
```

选项二：从 Dockerfile 构建镜像（x86_64 与 aarch64 均适用）

Dockerfile 位于仓库 `docker/Dockerfile`，内容如下（仅打包编译依赖环境，不含源码）：

```dockerfile
ARG BASE_IMAGE=hub.oepkgs.net/openeuler/openeuler:24.03-lts-sp3
FROM ${BASE_IMAGE}

ARG REPO_DIR=/workspace

# 安装构建/UT 依赖（openEuler 工具链 + ubs-comm 构建依赖）
# 镜像仅提供预装依赖的环境，不打包源码；源码通过挂载 /workspace 提供
RUN dnf install -y \
        cmake gcc gcc-c++ make git \
        rdma-core-devel openssl-devel libboundscheck time \
        python3 python3-pip \
        gtest gtest-devel gmock gmock-devel \
    && dnf clean all

WORKDIR ${REPO_DIR}
CMD ["/bin/bash"]
```

构建环境镜像：

```cmd
cd ubs-comm
docker build -f docker/Dockerfile -t ubs-comm-openeuler:24.03-sp3-1.0.0 .
```

**步骤 2：创建容器**

按步骤 1 选择的获取方式，使用对应的镜像名创建容器：

- 选项一（预构建镜像）：

  ```cmd
  docker run -d --privileged --name ubs-comm-ttfhw \
      -v /home/workspace/ubs-comm-verify:/workspace \
      swr.cn-north-4.myhuaweicloud.com/ubscore/ubs-comm-openeuler:24.03-sp3-1.0.0 \
      sleep infinity
  ```

- 选项二（Dockerfile 构建）：

  ```cmd
  docker run -d --privileged --name ubs-comm-ttfhw \
      -v /home/workspace/ubs-comm-verify:/workspace \
      ubs-comm-openeuler:24.03-sp3-1.0.0 \
      sleep infinity
  ```

> 说明：
>
> - 构建/UT 场景无需挂载 NPU 设备；镜像默认工作目录为 `/workspace`，不建议挂载整个 `/home` 目录。
> - **代码必须事先克隆到宿主机 `/home/workspace/ubs-comm-verify` 目录**，镜像不包含源码，必须通过 `-v` 挂载提供。
> - `docker run` 执行后，建议先验证挂载成功：
>
>   ```cmd
>   $ ls /home/workspace/ubs-comm-verify/build.sh
>   ```
>
>   若文件不存在，请先执行 `git clone <仓库URL> --depth 1 --branch <分支> /home/workspace/ubs-comm-verify`。

**步骤 3：进入容器**

```cmd
docker exec -it ubs-comm-ttfhw bash
```

> 镜像不包含源码，进入容器前请确认 `/workspace` 已正确挂载宿主机代码目录：
>
> ```cmd
> $ ls /workspace/build.sh
> ```
>
> 若提示 No such file 或为空，请检查 `docker run` 的 `-v` 挂载参数，然后重新执行步骤 2。

进入容器后，在 `/workspace` 下执行：

```cmd
cd /workspace
UMQ_BUILD=on UBSOCKET_BUILD=on USE_URMA_STUB=ON ./build.sh   # Release 构建 HCOM+UMQ+UBSocket，产物在 dist/ 目录
```

> 说明：
>
> - 容器内无完整 URMA SDK，使用 `USE_URMA_STUB=ON` 启用 stub 头文件编译 `umq_ub`。
> - `UMQ_BUILD=on UBSOCKET_BUILD=on` 由 `build.sh` 转发至 `build/build_umq_and_ubsocket.sh`，完整构建 HCOM、UMQ 和 UBSocket 三个组件。仅执行 `./build.sh` 只会构建 HCOM。
>
> 推荐使用方式二（镜像构建），可复用预装依赖的镜像环境，无需每次手动安装依赖。
