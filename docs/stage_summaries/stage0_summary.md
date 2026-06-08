# Stage 0 Summary: Environment and Project Skeleton

## 1. 当前阶段完成了什么

- 建立 CMake + C++20 最小工程骨架。
- 添加 `asyncdataloader_stage0` 可执行程序。
- 添加 Stage 0 smoke test，并接入 CTest。
- 建立 `include/`、`src/`、`tests/`、`benchmark/`、`examples/`、`docs/` 基础目录。
- 添加 README、design、benchmark、interview 文档模板。
- 添加 `.gitignore`，忽略本地构建目录和 Windows `Zone.Identifier` 元数据。

## 2. 当前目录结构

```text
.
├── CMakeLists.txt
├── README.md
├── AGENTS.md
├── benchmark/
├── examples/
├── include/
│   ├── backend/
│   ├── buffer/
│   ├── config/
│   ├── coroutine/
│   ├── metrics/
│   ├── pipeline/
│   └── util/
├── src/
│   └── main.cpp
├── tests/
│   └── stage0_smoke_test.cmake
└── docs/
    ├── anti_collapse_checklist.md
    ├── benchmark.md
    ├── design.md
    ├── interview.md
    ├── project_manual.docx
    ├── staged_prompts.txt
    └── stage_summaries/
        └── stage0_summary.md
```

## 3. 新增或修改了哪些文件

- 新增 `.gitignore`
- 新增 `CMakeLists.txt`
- 新增 `README.md`
- 新增 `src/main.cpp`
- 新增 `tests/stage0_smoke_test.cmake`
- 新增 `docs/design.md`
- 新增 `docs/benchmark.md`
- 新增 `docs/interview.md`
- 新增 `docs/stage_summaries/stage0_summary.md`
- 新增空目录占位文件：`benchmark/.gitkeep`、`examples/.gitkeep`、`include/*/.gitkeep`

## 4. 每个文件的作用

- `.gitignore`：避免把本地 build 产物和 `Zone.Identifier` 元数据提交进仓库。
- `CMakeLists.txt`：定义 C++20 项目、Stage 0 可执行程序和 CTest smoke test。
- `README.md`：记录当前项目定位和最小构建命令。
- `src/main.cpp`：Stage 0 最小 hello 程序，只证明工程可编译可运行。
- `tests/stage0_smoke_test.cmake`：执行可执行程序并检查输出中包含项目名和当前阶段。
- `docs/design.md`：设计文档模板，记录最终架构约束。
- `docs/benchmark.md`：benchmark 文档模板，强调尚无实测数字。
- `docs/interview.md`：面试笔记模板，记录当前阶段的项目边界。
- `docs/stage_summaries/stage0_summary.md`：本阶段交接总结。
- `.gitkeep`：保留后续阶段需要的空目录。

## 5. 当前能运行的命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/asyncdataloader_stage0
```

## 6. 当前测试结果

实际运行结果：

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`：通过。
- `cmake --build build -j`：通过，生成 `asyncdataloader_stage0`。
- `ctest --test-dir build --output-on-failure`：通过，`1/1` tests passed。
- `./build/asyncdataloader_stage0` 输出：`AsyncDataLoader - Stage 0 project skeleton`。

## 7. 遇到过哪些 bug，怎么解决

- TDD 红灯阶段，`tests/stage0_smoke_test.cmake` 指向尚不存在的可执行程序，按预期失败。
- 初版失败信息只打印 stderr，信息不够清楚；已改为同时打印 `execute_process` 的 `result`，明确显示 `No such file or directory`。

## 8. 还没解决的问题

- 尚未确认 liburing 开发库是否可用；可以在 Stage 0 的后续小步或 Stage 4 前确认。
- 尚未实现任何真实数据 I/O、协程、backend、BufferPool、Pipeline 或 Metrics。
- 尚未添加 C++ 单元测试框架；当前只有 CTest smoke test。

## 9. 下一阶段要做什么

- 如果继续完善 Stage 0：最小确认 liburing 头文件和链接环境是否可用，但不写 liburing demo。
- Stage 1：C++20 coroutine learning，学习 `Task<T>`、`promise_type`、`coroutine_handle`、awaiter 生命周期。

## 10. 面试时这一阶段可以怎么讲

Stage 0 只是工程地基：我先建立 CMake/C++20 骨架、目录边界、文档模板和最小 smoke test，保证后续每个阶段都能在可编译、可测试的工程里增量推进。这个阶段没有声称性能收益，也没有提前实现 pipeline。

## 11. 防坍缩自检

1. 本阶段是否引入或破坏任何硬性约束？
   - 没有。本阶段没有实现数据路径，不存在读整文件、无界队列、串行 pipeline 冒充最终方案等问题。
2. 当前内存是否仍然有界？
   - 当前程序只输出一行文本，没有数据缓冲区。真正的有界内存机制将在 Stage 8 通过 `BufferPool`、`PipelineConfig` 和背压控制实现。
3. 当前是否能通过 T1：256MB 处理 50GB 不 OOM？
   - 不能。T1 需要真实 pipeline、BufferPool、背压和端到端处理流程，预计 Stage 8 具备内存上限机制，Stage 10/11/13 才能端到端验证。
4. 如果还不能通过 T1，说明它会在哪个阶段满足。
   - Stage 8 建立基础机制；Stage 10 形成端到端 demo；Stage 11/13 补 benchmark 和错误测试验证。
5. 本阶段新增代码中，buffer 所有权如何流转？
   - 本阶段没有数据 buffer。后续目标生命周期仍是 `BufferPool -> reader -> processing stage -> writer -> BufferPool`。
6. 哪些验收测试还不能跑？
   - T1/T1b/T2/T3/T4/T5/T6/T7/T8/T9 都还不能跑，因为当前只有工程骨架。
7. 这些验收测试将在什么阶段满足？
   - T1/T1b/T2：Stage 8 之后开始具备条件，Stage 10/11 验证。
   - T3/T4/T5：Stage 10 端到端 pipeline 后验证。
   - T6/T7/T8：Stage 13 错误测试与安全测试阶段补齐。
   - T9：Stage 10 引入真实处理 demo，Stage 11 benchmark 分析。
