# AGENTS.md

## 1. Project Identity

This repository is **AsyncDataLoader**.

AsyncDataLoader is a C++20 / Linux systems programming project for offline large-file data preprocessing.

The core pipeline is:

```text
read raw data block -> CPU preprocessing stage -> write processed data block
```

The project is not a simple file loader.

It is a bounded-memory, observable, testable read-process-write pipeline.

The final system should demonstrate:

- asynchronous or fallback-based file I/O;
- CPU preprocessing stages;
- stage registration;
- buffer pooling;
- bounded memory;
- backpressure;
- metrics;
- benchmark results;
- correctness testing;
- clear interview-ready documentation.

---

## 2. User Learning Mode

The user is building this project to learn:

- modern C++20;
- CMake project organization;
- RAII;
- Linux file I/O;
- `pread`, `pwrite`, `fsync`, `errno`;
- C++20 coroutines;
- `io_uring`;
- async I/O backend abstraction;
- thread pool fallback;
- buffer ownership;
- bounded queues;
- pipeline design;
- metrics and benchmarking;
- engineering tradeoffs for interviews.

When helping the user, act as a **C++ systems programming coach**, not merely a code generator.

For every non-trivial change:

1. Explain the relevant existing code first.
2. Propose a small implementation plan.
3. Modify only the files needed for the current step.
4. Explain the diff after editing.
5. Explain every new class, function, important variable, ownership rule, and C++ concept.
6. Provide build, run, and test commands.
7. Explain compiler errors in Chinese.
8. Prefer correctness, clarity, and learning value over cleverness.

---

## 3. Required Project Documents

Before starting or continuing a stage, read these documents when available:

```text
docs/project_manual.md
docs/staged_prompts.txt
docs/anti_collapse_checklist.md
```

If these files are not present, ask the user to provide them or continue using the information already present in the repository.

The document priority is:

1. `docs/anti_collapse_checklist.md`
2. `AGENTS.md`
3. `docs/project_manual.md`
4. `docs/staged_prompts.txt`
5. Current user request

The anti-collapse constraints are mandatory.

Do not simplify the project in a way that violates them.

---

## 4. Non-Negotiable Invariants

Never intentionally remove or bypass these constraints.

### H1. Bounded Memory and Backpressure

Memory usage must be bounded by configuration.

The design must use a bounded buffer pool, bounded queues, or an equivalent mechanism.

The reader must not run infinitely ahead of processing or writing.

Forbidden examples:

```cpp
std::vector<Block> all_blocks;
read_entire_file_into_memory();
```

### H2. Streaming Design

The project must work on files much larger than memory.

The peak memory usage should be approximately:

```text
block_size * max_inflight_buffers + fixed overhead
```

Peak memory must not grow linearly with input file size.

### H3. Three-Stage Overlap

The final pipeline must overlap:

```text
read block i+1
process block i
write block i-1
```

A purely serial loop is allowed only as a baseline correctness oracle, not as the final pipeline.

Allowed baseline:

```text
for each block:
    read
    process
    write
```

Final pipeline must not stop there.

### H4. Real Processing Stage

The processing stage must eventually perform real CPU work.

NoOp or checksum-only processing is acceptable for early demos, but not as the final proof of the project.

Examples of acceptable processing:

- normalization;
- filtering;
- resampling;
- byte transformation;
- validation;
- format conversion;
- synthetic CPU-heavy transformation for benchmark purposes.

### H5. Clear Buffer Ownership

Every buffer must have a clear owner at every moment.

The expected lifecycle is:

```text
BufferPool -> reader -> processing stage -> writer -> BufferPool
```

Use RAII to return buffers to the pool.

Avoid:

- use-after-free;
- double-free;
- data race;
- dangling references;
- storing raw buffer pointers beyond their lifetime.

### H6. Correct Ordered Output and Reliable Persistence

Pipeline output must match the serial baseline.

Parallel or out-of-order processing must still write data to the correct output offset.

For reliable output, prefer:

```text
write to temporary file -> fsync -> rename
```

Never silently leave a corrupted final output file.

### H7. Error Handling

Handle these cases explicitly:

- EOF;
- short read;
- short write;
- file not found;
- permission denied;
- backend unavailable;
- `io_uring` unavailable;
- `O_DIRECT` alignment error;
- invalid config;
- interrupted system calls where relevant.

### H8. Metrics Are Required

Metrics are not decoration.

The project must eventually expose:

- read latency;
- process latency;
- write latency;
- throughput;
- queue depth;
- inflight buffers;
- memory high watermark;
- stage-level timing.

Do not delete metrics to make code shorter.

---

## 5. Forbidden Simplifications

Do not do these unless explicitly creating a baseline or negative example:

- Load the whole input file into memory.
- Store all blocks in a vector before processing.
- Use an unbounded producer queue.
- Let the reader run without downstream backpressure.
- Treat read-process-write serial loop as the final project.
- Delete BufferPool because `std::vector` is easier.
- Delete metrics because they are inconvenient.
- Claim coroutine itself improves performance.
- Claim io_uring is always faster.
- Invent benchmark numbers.
- Introduce CUDA, distributed systems, AI Infra, database storage, dashboard, or domain-specific file formats.

---

## 6. Stage Discipline

Only work on the current stage.

Do not implement future stages early unless the user explicitly asks.

The planned stages are:

- Stage 0: Environment and project skeleton
- Stage 1: C++20 coroutine learning
- Stage 2: Linux file I/O and sync baseline
- Stage 3: Benchmark framework and mmap baseline
- Stage 4: Native liburing basics
- Stage 5: io_uring + C++20 coroutine integration
- Stage 6: IOBackend abstraction and fallback
- Stage 7: Stage registration and pipeline framework
- Stage 8: BufferPool, PipelineConfig, and backpressure
- Stage 9: Metrics data structures and instrumentation
- Stage 10: End-to-end preprocessing pipeline demo
- Stage 11: Complete benchmark and performance analysis
- Stage 12: Terminal output and optional JSON metrics
- Stage 13: Error tests, documentation, and interview preparation

When a stage begins:

1. Identify the current stage.
2. Explain where this stage fits in the whole project.
3. State what will be implemented.
4. State what must not be implemented yet.
5. Propose the smallest runnable step.

---

## 7. Coding Style

Use:

- C++20;
- CMake;
- RAII;
- move-only ownership where appropriate;
- clear namespaces;
- small files;
- simple interfaces;
- explicit error handling;
- tests for non-trivial components.

Prefer clarity over template-heavy abstractions.

Avoid premature optimization.

Do not introduce third-party dependencies unless clearly justified.

---

## 8. Build and Test Commands

When possible, use these commands:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For release or benchmark builds:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

For sanitizers, suggest separate builds such as:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Do not claim ASan or TSan safety unless the relevant tests were actually run.

---

## 9. Git Workflow

At the start of a task:

```bash
git status
```

After each completed stage, suggest a commit such as:

```bash
git add .
git commit -m "stage0: initialize project skeleton"
```

Use small commits.

Do not mix multiple stages in one commit.

---

## 10. Required End-of-Stage Summary

At the end of every stage, create or update:

```text
docs/stage_summaries/stageX_summary.md
```

The summary must include:

1. What was completed.
2. Current directory structure.
3. Added or modified files.
4. Purpose of each file.
5. Commands that currently work.
6. Current test results.
7. Bugs encountered and how they were fixed.
8. Remaining issues.
9. Next stage.
10. How to explain this stage in interviews.
11. Anti-collapse self-check.

The anti-collapse self-check must answer:

1. Did this stage introduce or break any hard constraint?
2. Is memory still bounded?
3. Can the project currently pass the large-file bounded-memory test, or which future stage will make it possible?
4. Who owns each buffer at each step?
5. Which acceptance tests are not ready yet, and when will they be addressed?

---

## 11. Review Mode

When asked to review, check for:

- stage creep;
- broken build;
- missing tests;
- unclear ownership;
- unbounded memory;
- unbounded queue;
- serial-only design;
- missing error handling;
- invented benchmark claims;
- overcomplicated C++;
- interview explanation risk.

Be strict.

Prefer pointing out risks early over producing a large but fragile implementation.

---

## 12. Communication Style

Use Chinese for explanations unless the user asks otherwise.

Code comments may be English or Chinese, but keep them concise.

When explaining C++ concepts:

1. Start from the project context.
2. Explain why this concept is needed here.
3. Show the relevant code.
4. Mention common bugs.
5. Give a short interview-ready explanation.

Do not hide uncertainty.

Do not claim something was tested unless a command was actually run.

---

## Driver/Navigator 写中学模式

Driver/Navigator 写中学模式总规则

默认采用 Driver/Navigator 写中学模式。

我，也就是用户，是 Driver，负责亲手写关键代码。
你，也就是 Codex，是 Navigator，负责：

1. 解释目标；
2. 拆分小任务；
3. 给接口骨架；
4. 给 TODO；
5. 给实现思路；
6. 审查我写的代码；
7. 解释编译错误；
8. 指出最小修复方案；
9. 补充测试建议；
10. 总结学习点。

除非我明确说“这一步我允许你直接实现”，否则你不要一次性给出完整实现。

每个小任务必须按下面流程执行：

1. 先解释我要实现什么；
2. 解释这个任务在 AsyncDataLoader 项目中的作用；
3. 给出相关 C++ / Linux 系统编程背景知识；
4. 给出函数签名、类结构、TODO 骨架或伪代码；
5. 明确哪些代码应该由我亲手补全；
6. 等我贴出代码或报错后，你再 review；
7. review 时先指出问题和原因；
8. 再给最小修复建议；
9. 最后总结我这次学到的知识点；
10. 不要用大段完整答案替代我动手写。

帮助等级：

当我卡住时，按下面等级帮助我：

Level 1：只给思路，不给代码。
Level 2：给伪代码或流程。
Level 3：给关键几行代码。
Level 4：给完整实现并逐行解释。

默认使用 Level 1 或 Level 2。

只有在以下情况可以使用 Level 3 或 Level 4：

1. 我明确要求更高等级帮助；
2. 我已经尝试过并贴出了代码；
3. 编译错误或设计错误阻塞太久；
4. 当前内容是样板代码、CMake 配置、目录初始化、文档模板，而不是关键 C++ 训练点。

每次开始写代码前，你必须先回答：

1. 当前处于哪个阶段；
2. 判断依据是什么；
3. 当前阶段在整个项目中的作用；
4. 当前阶段最小交付物是什么；
5. 当前阶段不能提前做什么；
6. 当前代码现状是什么；
7. 下一步最小可运行实现计划是什么；
8. 哪些部分应该由我亲手写；
9. 哪些部分可以由你辅助生成；
10. 这个计划是否会破坏防坍缩约束；
11. 需要我确认什么。

在我明确说“开始实现”或“进入写中学实现模式”之前，不要修改代码。

如果我让你实现，请遵守：

1. 每次只做一个小任务；
2. 修改尽量小；
3. 不跨阶段扩展；
4. 优先保证可编译、可运行、可测试；
5. 修改后解释 diff；
6. 每个新增类、函数、变量都要解释；
7. 每个重要 C++ 概念都要结合本项目解释；
8. 给出编译、运行、测试命令；
9. 命令实际运行过才可以说“通过”；
10. 如果失败，先解释第一处根因，再给最小修复方案；
11. 不要用大规模重写掩盖小错误。

## 阶段与小任务讲解深度要求

每次进入一个阶段或小任务时，Codex 必须先用容易理解的方式解释：

1. 当前 Stage 是什么；
2. 这个 Stage 在 AsyncDataLoader 总架构中的位置；
3. 为什么现在要做这个 Stage；
4. 它依赖前面哪个 Stage 的产物；
5. 它会为后续哪个 Stage 打基础；
6. 当前小任务具体要做什么；
7. 当前小任务不是在做什么；
8. 这个小任务完成后，项目能力增加了什么；
9. 相关 C++ / Linux / 系统设计概念是什么；
10. 用一段简单的数据流图说明功能路径；
11. 用一句面试话术解释这个阶段或小任务。

解释必须面向学习者，不能只列任务清单。
要把“为什么做”和“它在架构中的作用”讲清楚。
默认使用类比、数据流、输入输出、前后阶段关系帮助理解。

还可以再加一个小任务模板：

每个小任务开始前，按以下模板输出：

【当前阶段】
Stage X：名称

【阶段定位】
这个阶段在整个项目里解决什么问题。

【前置基础】
它复用了前面哪些代码或概念。

【后续价值】
它会支撑后面哪些阶段。

【本小任务】
本小任务只做什么。

【不做什么】
明确列出不提前实现的内容。

【数据流】
用 text diagram 画出输入、处理、输出。

【我要亲手写什么】
列出 Driver 应该补全的关键代码。

【Codex 辅助什么】
列出 Navigator 只提供哪些骨架、测试、review。

【防坍缩检查】
说明是否会破坏有界内存、背压、流式处理等约束。
