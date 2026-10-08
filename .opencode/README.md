# opencode Skill 系统

本目录包含 opencode 的 skill 定义，用于指导 AI 在特定任务场景下遵循项目约定、避免陷阱、产出符合规范的代码。

## Skill 是什么

Skill 是一组结构化的指令文件，opencode 在对话中出现触发关键词时自动加载。每个 skill 目录包含一个 `SKILL.md`，其 YAML frontmatter 定义了 `name`、`description` 和触发关键词。

## Skill 层级结构

```
ut-gen (root)              ← 通用UT生成：mockcpp模式、fixture模板、CMake注册、常见陷阱、代码风格
  ├── ut-gen-umq           ← UMQ模块特定：umq_* mock、Share-JFR、errno映射调用点
  ├── ut-gen-core          ← core/socket特定：epoll mock、LibcApi函数指针、Connector/Acceptor状态机
  ├── ut-gen-common        ← common特定：singleton清理、Lock/ThreadPool测试策略
  ├── ut-gen-under-api     ← under_api特定：dlopen/dlsym mock、两后端模式
  ├── ut-gen-profiling     ← profiling特定：无mock纯逻辑、环形buffer测试
  └── ut-coverage-coord    ← 协调层：认领、进度、里程碑

add-tracepoint (standalone)  ← tracer/tracepoint新增：SplitTrace/PROF打点, ProfilingTPId枚举, 传参, 陷阱
```

**加载规则:** 写特定模块UT时，root skill(`ut-gen`)与对应子skill**一起加载**。协调/规划任务加载`ut-coverage-coord`。

## YAML Frontmatter 格式

```yaml
---
name: <skill_name>            # 与目录名一致
description: <描述>            # 含触发关键词的完整说明，opencode据此决定是否加载
---
```

`description` 中列出的关键词(opencode/UT/mockcpp/gtest等)决定自动触发时机。

## 全局规则

以下规则适用于所有skill，各SKILL.md不再单独复述:

1. **加载规则**: 写特定模块UT时，root skill(`ut-gen`)与对应子skill**一起加载**。协调/规划任务加载`ut-coverage-coord`。
2. **触发关键词**: 各skill YAML frontmatter的`description`字段是权威触发定义，SKILL.md正文不再复述关键词列表。
3. **构建命令**: 所有构建/运行命令见`ut-gen/SKILL.md` §构建与运行和`AGENTS.md` §Build Commands，各子skill工作流步骤引用而不复述。
4. **知识回流**: 覆盖率数据变更统一回流到`doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md`，各skill不再逐条列出。

## 如何使用 Skill

1. **自动触发** — 对话中出现关键词时 opencode 自动加载(关键词见各skill YAML frontmatter)
2. **手动请求** — 告诉 opencode "加载 skill ut-gen-umq"
3. **组合加载** — 写模块UT时: "加载 ut-gen + ut-gen-umq"

每个 SKILL.md 都有"如何使用本Skill"章节，说明触发方式和工作流程。

## 如何更新 Skill

发现新模式或陷阱时，按"知识回流"规则更新——每条回流目标应为**单一目标+判断条件**，而非二选一:

| 发现类型 | 判断条件 | 更新目标 | 示例 |
|----------|---------|----------|------|
| mockcpp通用模式 | 模式跨模块适用 | `ut-gen/SKILL.md` §mockcpp模式 | 新`.will().then()`用法 |
| 代码风格/构建gotchas | 与构建/架构有关 | `AGENTS.md` §已知陷阱 | 新CMake链接问题 |
| 模块特定陷阱 | 仅某模块出现 | `ut-gen-<module>/SKILL.md` §常见陷阱 | Share-JFR仅在umq模块 |
| 覆盖率数据 | 数字/百分比变更 | `UBSOCKET-COVERAGE-ANALYSIS.ch.md` | 基线变化 |

每个 SKILL.md 都有"知识回流"章节，含模块特定的回流更新检查清单。

## SKILL.md 写作范式 (正面用例)

各skill已有成熟的写作范式，创建/更新SKILL.md时参考:

| 范式 | 出处 | 特点 |
|------|------|------|
| 正面/反面对比 | ut-gen §mockcpp模式 | "正确的mock模式"代码 + "错误模式"代码并列，消除歧义 |
| 决策矩阵表 | ut-gen-core §关键类与构造函数依赖 | 4列(类→依赖→默认mock→stub可选)，每行明确决策 |
| 陷阱结构化条目 | ut-gen §常见陷阱#12 | **粗体标题**一句话 + 解释 + 具体case/代码，参见#12 Share-JFR条目格式 |
| 完整映射表 | ut-gen-umq §各调用点的errno映射 | 方法→API→op→Converter API，消除op赋值歧义 |
| 双受众 | ut-coverage-coord §如何使用本Skill | 对协调者4步 + 对同事5步，角色区分不同流程 |
| 三段式类测试 | ut-gen-common §单例模式与清理 | 测试设置代码 + 清理代码 + "**关键:**"警告段 |
| 条件分支 | ut-gen §ALWAYS_INLINE函数策略 | 分支类型→测试方法→示例表格，不可mock时给出替代策略 |

## 如何创建新 Skill

1. 在 `.opencode/skills/` 下创建目录: `<skill_name>/`
2. 创建 `SKILL.md`，包含:
   - YAML frontmatter(`name` + `description`含触发关键词)
   - 模块范围
   - 关键类与依赖(如有)
   - 测试模式/策略
   - 常见陷阱
   - 如何使用本Skill(触发方式+工作流程)
   - 知识回流(更新目标+检查清单)
   - 参考文件
3. 在父skill的description中添加触发关键词(如需联动)
4. 在 `ut-coverage-coord/SKILL.md` 的层级图中添加新节点

## 文档语言规则

所有 SKILL.md 遵循项目的中英混写规则(见 `ut-gen/SKILL.md` "文档语言规则"或 `AGENTS.md`):
- 代码符号→英文
- 技术术语→英文
- 描述性语句→中文
- 章节标题→中文

## 与其他文档的关系

| 文档 | 职责 | 与Skill关系 |
|------|------|------------|
| `AGENTS.md` | 项目级上下文(构建/架构/陷阱) | Skill的"知识回流"目标之一 |
| `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` | 权威覆盖率数据源 | Skill的"权威数据源"引用 |
| `doc/ubsocket/UBSOCKET-CLAIMING.md` | Sprint唯一入口 | 引用Skill中的陷阱列表 |
| `.opencode/skills/*/SKILL.md` | 模块级UT指导 | 本系统的组成单元 |