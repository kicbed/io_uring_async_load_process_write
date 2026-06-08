# CODEX_WORKFLOW.md

## 1. Basic Principle

Use Codex as:

1. C++ learning coach;
2. pair programmer;
3. strict reviewer;
4. stage handoff generator.

Do not use Codex as a one-shot full-project generator.

---

## 2. Start Codex

From project root:

```bash
cd AsyncDataLoader
git status
codex
```

---

## 3. First Prompt for Every Stage

Use this prompt at the beginning of each stage:

```text
当前阶段是：【填写阶段名称】。

请先不要修改代码。

请完成以下事情：
1. 阅读 AGENTS.md。
2. 阅读 docs/project_manual.md。
3. 阅读 docs/staged_prompts.txt 中当前阶段的要求。
4. 阅读 docs/anti_collapse_checklist.md。
5. 总结当前阶段目标。
6. 总结本阶段不能提前做什么。
7. 给出最小可运行实现计划。
8. 指出本阶段和防坍缩约束的关系。

等我确认后再开始修改代码。
```

---

## 4. Start Implementation Prompt

```text
按刚才确认的最小计划开始实现。

要求：
1. 每次只完成一个小任务；
2. 不提前实现后续阶段；
3. 修改后解释 diff；
4. 给出编译和运行命令；
5. 遇到错误先解释根因，再给修复方案；
6. 保持项目可编译。
```

---

## 5. Diff Explanation Prompt

```text
请解释刚才的 diff：

1. 修改了哪些文件；
2. 每个文件为什么要改；
3. 新增的类、函数、变量分别做什么；
4. 涉及哪些 C++ 知识点；
5. 这里最容易出什么 bug；
6. 我应该重点阅读哪些代码。
```

---

## 6. Compiler Error Prompt

```text
编译失败了。请先不要直接大改。

请先解释：
1. 第一处根因是什么；
2. 是 CMake 问题、头文件问题、链接问题、系统调用问题还是 C++ 语法问题；
3. 最小修复方案是什么；
4. 为什么这个修复是安全的。

我确认后你再修改。
```

---

## 7. Learning Card Prompt

```text
请把刚才涉及的 C++ 知识整理成学习卡片：

1. 概念名称；
2. 在本项目中为什么需要它；
3. 最小代码例子；
4. 常见坑；
5. 面试时怎么解释。
```

---

## 8. Reviewer Prompt

```text
请以严格 reviewer 身份审查当前代码。

重点检查：
1. 是否超出当前阶段；
2. 是否破坏防坍缩约束；
3. 是否存在无界内存或无界队列；
4. 是否存在资源泄漏或所有权不清；
5. 是否缺少错误处理；
6. 是否缺少测试；
7. 是否有不诚实的 benchmark 表述；
8. 是否有面试解释风险；
9. 下一步最小修复建议是什么。
```

---

## 9. End-of-Stage Prompt

```text
请生成本阶段交接总结，并保存到：

docs/stage_summaries/stageX_summary.md

要求包括：

1. 当前阶段完成了什么；
2. 当前目录结构；
3. 新增/修改了哪些文件；
4. 每个文件的作用；
5. 当前能运行的命令；
6. 当前测试结果；
7. 遇到过哪些 bug，怎么解决；
8. 还没解决的问题；
9. 下一阶段要做什么；
10. 面试时这一阶段可以怎么讲；
11. 防坍缩自检。

防坍缩自检必须回答：

1. 本阶段是否引入或破坏任何硬性约束？
2. 当前内存是否仍然有界？
3. 当前是否能通过 T1：256MB 处理 50GB 不 OOM？如果不能，说明在哪个阶段满足。
4. 本阶段新增代码中，buffer 所有权如何流转？
5. 哪些验收测试还不能跑？将在什么阶段满足？
```

---

## 10. Git Commit

After each completed stage:

```bash
git status
git add .
git commit -m "stageX: short description"
```

Example:

```bash
git commit -m "stage0: initialize project skeleton"
```
