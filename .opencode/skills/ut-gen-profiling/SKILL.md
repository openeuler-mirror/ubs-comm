---
name: ut-gen-profiling
description: Module-specific skill for writing UT for files under src/ubsocket/csrc/profiling/. Load together with ut-gen skill when testing profiling tracer, tracepoint code. Trigger on keywords: ProfTracer, ProfTracepoint, Prof, profiling, tracer, tracepoint.
---

# Profiling模块 UT 生成子skill

> **权威数据源**: `doc/ubsocket/UBSOCKET-COVERAGE-ANALYSIS.ch.md` — Profiling模块84.2%已达标(222行), 无需额外UT。
> **协调跟踪**: `.opencode/skills/ut-coverage-coord/SKILL.md` — 文件认领与进度。

## 模块范围

`src/ubsocket/csrc/profiling/` 下的文件:
- `ubsocket_prof.h/.cpp` — Prof (性能入口, getter/setter, 环形buffer)
- 以及 `csrc/common/ubsocket_profiling.h` 中的profiling相关代码(链接到Prof的宏)

## 关键类

### Prof

简单性能类，含getter/setter方法。可能包含:
- 启动/停止性能追踪
- 获取性能数据/计数
- 重置计数器

**无系统调用依赖** — 纯逻辑 + 内部环形buffer状态。直接测试，无需mock。

### ProfTracer (如存在于prof.cpp)

使用环形buffer + `pthread_mutex` 实现线程安全的trace记录。

**测试ProfTracer:**
- 记录trace点(正常路径)
- 环形buffer满(溢出路径)
- 并发记录(多线程)
- 重置/清除traces

**mock策略:** ProfTracer无外部依赖(pthread_mutex除外)。无需mock。

### ProfTracepoint (如存在)

单个trace点记录。可能是简单struct或方法，存储timestamp + 元数据。

**测试:** 验证正确的timestamp捕获和元数据存储。无需mock。

## 测试模式

### 简单getter/setter测试

```cpp
Prof prof;
prof.SetTraceEnabled(true);
EXPECT_TRUE(prof.IsTraceEnabled());

prof.SetTraceEnabled(false);
EXPECT_FALSE(prof.IsTraceEnabled());
```

### 环形buffer测试

```cpp
ProfTracer tracer(16);  // 环形buffer大小16
tracer.Record(TracePoint{...});
EXPECT_EQ(tracer.GetCount(), 1);

// 超容量填充
for (int i = 0; i < 20; i++) {
    tracer.Record(TracePoint{...});
}
EXPECT_EQ(tracer.GetCount(), 16);  // 上限为buffer大小
```

### 线程安全记录测试

```cpp
ProfTracer tracer(256);
std::vector<std::thread> threads;
for (int i = 0; i < 4; i++) {
    threads.emplace_back([&tracer]() {
        for (int j = 0; j < 100; j++) {
            tracer.Record(TracePoint{...});
        }
    });
}
for (auto &t : threads) { t.join(); }
// 无crash, 无数据损坏
EXPECT_LE(tracer.GetCount(), 256);
```

## GlobalSetting依赖

`GlobalSetting::UBS_TRACE_ENABLED` 控制性能追踪是否活跃。SetUp中设置:

```cpp
void SetUp() override
{
    GlobalSetting::UBS_TRACE_ENABLED = true;  // 或false测试禁用路径
}
```

## 优先级

低优先级模块 — 行数少，逻辑简单。重点:
- 环形buffer溢出路径
- 线程安全验证
- 禁用路径(trace_enabled = false)覆盖率

## 如何使用本Skill

### 触发与加载

触发关键词见本skill YAML frontmatter `description`字段。加载规则见`.opencode/README.md` §全局规则——写profiling模块UT时需与`ut-gen`(root skill)一起加载。

### 工作流程

1. **确认必要性** — Profiling模块84.2%已达标，仅补充缺失路径时使用
2. **读源码** — 识别getter/setter/环形buffer/线程安全逻辑
3. **设计case** — 按本skill §测试模式(纯逻辑，无需mock)
4. **编写测试** — 直接构造对象，验证状态
5. **构建验证** — 命令见ut-gen §构建与运行

## 知识回流

按`.opencode/README.md` §如何更新Skill回流。Profiling模块特定判断:

| 发现类型 | 判断条件 | 回流目标 |
|----------|---------|----------|
| 环形buffer/线程安全测试策略 | 涉及环形buffer溢出或并发trace | 本skill §测试模式 |
| 跨模块适用 | 不限于profiling模块 | `ut-gen` §mockcpp模式/§常见陷阱 |

### 回流更新检查清单

- [ ] 新环形buffer测试策略 → 本skill §测试模式

## 参考文件

| 文件 | 用途 |
|------|------|
| `src/ubsocket/csrc/profiling/ubsocket_prof.h/.cpp` | Prof源码 |
| `src/ubsocket/csrc/common/ubsocket_profiling.h` | 性能宏 |