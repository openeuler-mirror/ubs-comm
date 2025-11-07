# HCOM项目贡献

这篇文章主要介绍了如何进行`HCOM`的贡献

## 加入贡献

`HCOM`项目欢迎新成员加入并贡献，新成员需要积极的为`HCOM`项目做出贡献。我们乐于接受提交`Issue`/处理`Issue`、代码、文档、检视意见、测试等类型的贡献。

### 1.提交 `Issue` / 处理 `Issue`
`Issue` 是用来记录和追踪开发者的想法、反馈、任务和缺陷。您可以通过提交`Issue`到`HCOM`项目进行贡献。常见的`Issue`场景有：

a）报告 bug

b）提交建议

c）记录一个待完成任务

d）指出文档缺失/安装问题

e）答疑交流

#### 1.1 找到 `Issue` 列表：

在`HCOM`的代码仓中，点击工具栏目的 “Issues”，您可以找到其 `Issue` 列表

#### 1.2 提交 `Issue`：

如果您准备上报`Bug`或者提交需求，为`HCOM`贡献自己的意见或建议，可以在`HCOM`仓库上提交`Issue`（请参考 [Issue 提交指南](https://gitee.com/openeuler/community/blob/master/zh/contributors/issue-submit.md) )。为了吸引更广泛的注意，您也可以把`Issue`的链接附在邮件内，通过 [邮件列表](https://www.openeuler.openatom.cn/zh/community/mailing-list/) 发送给所有人。

#### 1.3 参与 `Issue` 内的讨论：

每个 Issue 下面可能有参与者们的交流和讨论，如果您感兴趣，可以在评论框中发表自己的意见。

#### 1.4 找到愿意处理的 `Issue`：

如果您愿意处理其中的一个 `Issue`，可以将它分配给自己。只需要在评论框内输入`/assign`或`/assign @yourself`，机器人就会将`Issue`分配给您，您的名字将显示在负责人列表里。

### 2.贡献编码
#### 2.1 搭建开发环境

1.开发环境准备：如果您想参与编码贡献，需要准备`HCOM`的开发环境，请参考`doc`目录下的《HCOM用户指南》搭建并准备开发环境。

2.下载和构建软件包：如果您想下载、修改、构建及验证`HCOM`提供的软件包，请参考`README.md`进行编译、构建、用例验证。


#### 2.2 下载代码和拉分支

如果要参与代码贡献，您还需要了解如何在 `Gitee` 下载代码，通过 `PR`（`Pull Request`） 合入代码等。`HCOM` 使用 `Gitee` 代码托管平台，想了解具体的指导，请参考 [Gitee Workflow Guide](https://gitee.com/openeuler/community/blob/master/zh/contributors/Gitee-workflow.md) 。该托管平台的使用方法类似`GitHub`，如果您以前使用过`GitHub`，本节的内容您可以大致了解甚至跳过。

#### 2.3 修改构建和本地验证

在本地分支上完成修改后，进行构建和本地验证，请参考构建软件包。

#### 2.4 提交一个 `PR`（`Pull Request`）

当您提交一个`PR`的时候，就意味您已经开始给社区贡献代码了。请参考  [openEuler 社区 PR 提交指导](https://gitee.com/openeuler/community/blob/master/zh/contributors/pull-request.md) 。为了使您的提交更容易被接受，您需要：

1.代码要遵循以下几个原则

可读性 - 重要代码应充分注释，`API`应具备文档，代码风格应遵循现有规范。

优雅性 - 新增功能、类或组件应设计精良。

可测试性 - 新增代码的 70% 应被单元测试覆盖。

2.准备完善的提交信息

3.如果一次提交的代码量较大，建议将大型的内容分解成一系列逻辑上较小的内容，分别进行提交会更便于检视者理解您的想法

### 3.检视代码
`HCOM`非常欢迎所有参与的人都能成为活跃的检视者。可以参考 [社区成员](https://gitee.com/openeuler/community/blob/master/community-membership_cn.md) ，该文档描述了不同贡献者的角色职责。

当成为`HCOM`项目的`Committer`或`Maintainer`角色时，便拥有审核代码的责任与权利。强烈建议本着[社区行为准则](https://gitee.com/openeuler/community/blob/master/code-of-conduct.md) ，超越自我，相互尊重和促进协作。在检视其他人的`PR`时，可以重点关注包括：

1.贡献背后的想法是否合理

2.贡献的架构是否正确

3.贡献是否完善

### 4.测试
测试是所有贡献者的责任，对于社区版本来说，`sig-QA` 组是负责测试活动的社区官方组织。如果您希望在自己的基础架构上开展测试活动，可以参考：[社区测试体系介绍](https://gitee.com/openeuler/QA/blob/master/%E7%A4%BE%E5%8C%BA%E6%B5%8B%E8%AF%95%E4%BD%93%E7%B3%BB%E4%BB%8B%E7%BB%8D.md) 。

为了成功发行一个社区版本，`openEuler` 需要完成多种测试活动。不同的测试活动，测试代码的位置也有所不同，成功运行测试所需的环境细节也会有差异，有关的信息可以参考 [测试指南](https://gitee.com/openeuler/QA/blob/master/%E7%A4%BE%E5%8C%BA%E5%BC%80%E5%8F%91%E8%80%85%E6%B5%8B%E8%AF%95%E8%B4%A1%E7%8C%AE%E6%8C%87%E5%8D%97.md) 。

### 5.社区安全问题披露
[安全处理流程](https://gitee.com/openeuler/security-committee/blob/master/docs/zh/vulnerability-management-process/security-process.md) ——简要描述了处理安全问题的过程。

[安全披露信息](https://gitee.com/openeuler/security-committee/blob/master/docs/zh/vulnerability-management-process/security-disclosure.md) ——如果您希望报告安全漏洞，请参考此页面。

### 6.遗漏事项
您如果发现了本指南不足的点，或者您对某些特定步骤感到困惑，请告诉我们！或者您可以选择提交一个`PR`来解决这个问题
