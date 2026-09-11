# 版本配套说明

## 软件版本配套说明

<table style="undefined;table-layout: fixed; width: 435px"><colgroup>
<col style="width: 201px">
<col style="width: 234px">
</colgroup>
<thead>
  <tr>
    <th>项目</th>
    <th>版本配套</th>
  </tr></thead>
<tbody>
  <tr>
    <td>操作系统</td>
    <td>openEuler 22.03 LTS<br>openEuler 24.03 LTS</td>
  </tr>
  <tr>
    <td>RDMA-Core</td>
    <td>42.7</td>
  </tr>
  <tr>
    <td>GCC</td>
    <td>12.3.1（openEuler 24.03 LTS 默认）</td>
  </tr>
    <tr>
    <td>CCA</td>
    <td>VPP V300R024C10SPC001</td>
  </tr>
</tbody>
</table>

## 硬件版本配套说明

<table style="undefined;table-layout: fixed; width: 435px"><colgroup>
<col style="width: 201px">
<col style="width: 234px">
</colgroup>
<thead>
  <tr>
    <th>硬件</th>
    <th>版本配套</th>
  </tr></thead>
<tbody>
  <tr>
    <td>服务器名称</td>
    <td>TaiShan服务器</td>
  </tr>
    <tr>
    <td>处理器</td>
    <td>鲲鹏处理器</td>
  </tr>
  <tr>
    <td>网卡</td>
    <td>Mellanox CX5（仅使用RDMA通信协议时必须。使用其他通信协议不需要</td>
  </tr>
    <tr>
    <td>CPU</td>
    <td>通过系统文件"/sys/devices/system/cpu/cpu0/regs/identification/midr_el1"中获取CPU厂商信息判断，当前配套机型鲲鹏处理器型号为0x48。</td>
  </tr>
</tbody>
</table>

## 更新说明

当前版本对外开源。

## 已解决的问题

修复UBC场景拆链过程中偶现的崩溃问题。

## 遗留问题

无

## 版本配套文档

<table style="undefined;table-layout: fixed; width: 855px"><colgroup>
<col style="width: 200px">
<col style="width: 285px">
<col style="width: 119px">
</colgroup>
<thead>
  <tr>
    <th>文档名称</th>
    <th>内容简介</th>
    <th>交付形式</th>
  </tr></thead>
<tbody>
  <tr>
    <td><a href="../zh/release_notes.md">版本说明书</a></td>
    <td>本文档提供UBS Comm的版本发布信息。</td>
    <td>开源仓</td>
  </tr>
    <tr>
    <td><a href="ubscomm_installation_deployment.md">安装部署</a></td>
    <td>本文档提供UBS Comm安装部署说明。</td>
    <td>开源仓</td>
  </tr>
  <tr>
    <td><a href="../zh/ubscomm_user_guide.md">用户指南</a></td>
    <td>本文档提供UBS Comm特性介绍、安装部署及使用指导。</td>
    <td>开源仓</td>
  </tr>
    <tr>
    <td><a href="../zh/ubscomm_api_reference.md">API参考</a></td>
    <td>本文档提供UBS Comm API接口说明。</td>
    <td>开源仓</td>
  </tr>
      <tr>
    <td><a href="../zh/ubscomm_security_technical_whitepaper.md">安全技术白皮书</a></td>
    <td>本文档提供基于UBS Comm的应用使能方案的安全。</td>
    <td>开源仓</td>
  </tr>
</tbody>
</table>
