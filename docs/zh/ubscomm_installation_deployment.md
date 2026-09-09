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
|GCC|7.3.0|
|CCA|VPP V300R024C10SPC001|

## 安装步骤

**前提条件<a name="section1340093619408"></a>**

前置依赖libboundscheck（华为开源的安全函数库），可通过以下方式安装。

- 有欧拉yum镜像源时，可以直接yum install安装。

    ```cmd
    yum install libboundscheck
    ```

- 源码编译安装。

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
