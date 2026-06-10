# Stage 1 Summary: C++20 Coroutine Learning

## 1. 当前阶段完成了什么

- 完成 Stage 1 的三个协程学习 demo：
  - `simple_task_demo`
  - `manual_resume_demo`
  - `delay_awaiter_demo`
- 学习并实现了最小 `SimpleTask` 协程返回类型。
- 学习 `promise_type`、`std::coroutine_handle`、`initial_suspend`、`final_suspend`、`co_return`、`return_void`、`unhandled_exception`。
- 学习 move-only RAII：`SimpleTask` 拥有 coroutine frame，析构时调用 `destroy()`。
- 学习 awaiter 协议：
  - `await_ready()`
  - `await_suspend(std::coroutine_handle<>)`
  - `await_resume()`
- 学习两种恢复协程方式：
  - `manual_resume_demo`：由 `main()` 手动恢复保存的 handle。
  - `delay_awaiter_demo`：由后台线程模拟外部事件，延迟后恢复 handle。
- 在 `docs/interview.md` 中补充 Stage 1 协程问答。
- 为每个 Stage 1 demo 添加 CTest smoke test。

## 2. 当前目录结构

```text
.
├── CMakeLists.txt
├── README.md
├── AGENTS.md
├── benchmark/
├── docs/
│   ├── anti_collapse_checklist.md
│   ├── benchmark.md
│   ├── design.md
│   ├── interview.md
│   ├── staged_prompts.txt
│   └── stage_summaries/
│       ├── stage0_summary.md
│       └── stage1_summary.md
├── examples/
│   └── coroutines/
│       ├── delay_awaiter_demo.cpp
│       ├── manual_resume_demo.cpp
│       └── simple_task_demo.cpp
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
└── tests/
    ├── stage0_smoke_test.cmake
    ├── stage1_delay_awaiter_demo_test.cmake
    ├── stage1_manual_resume_demo_test.cmake
    └── stage1_simple_task_demo_test.cmake
```

## 3. 新增或修改了哪些文件

- 修改 `CMakeLists.txt`
- 修改 `.gitignore`
- 修改 `docs/interview.md`
- 更新 `docs/stage_summaries/stage1_summary.md`
- 新增 `examples/coroutines/delay_awaiter_demo.cpp`
- 新增 `tests/stage1_delay_awaiter_demo_test.cmake`

Stage 1 已有文件：

- `examples/coroutines/simple_task_demo.cpp`
- `examples/coroutines/manual_resume_demo.cpp`
- `tests/stage1_simple_task_demo_test.cmake`
- `tests/stage1_manual_resume_demo_test.cmake`

## 4. 每个文件的作用

- `CMakeLists.txt`：注册 Stage 1 三个 demo target，并为 `delay_awaiter_demo` 链接 `Threads::Threads`。
- `.gitignore`：忽略本地个人练习目录 `examples/练习`。
- `docs/interview.md`：记录 Stage 1 协程面试解释。
- `examples/coroutines/simple_task_demo.cpp`：演示最小 coroutine return object、手动 resume 和 RAII destroy。
- `examples/coroutines/manual_resume_demo.cpp`：演示 `co_await` 如何把当前 coroutine handle 交给 awaiter，由外部手动恢复。
- `examples/coroutines/delay_awaiter_demo.cpp`：演示后台线程模拟外部事件，延迟后恢复 coroutine handle。
- `tests/stage1_simple_task_demo_test.cmake`：验证 simple task 输出顺序。
- `tests/stage1_manual_resume_demo_test.cmake`：验证 manual resume 控制流。
- `tests/stage1_delay_awaiter_demo_test.cmake`：验证 delay awaiter 控制流。

## 5. 当前能运行的命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/stage1_simple_task_demo
./build/stage1_manual_resume_demo
./build/stage1_delay_awaiter_demo
```

## 6. 当前测试结果

实际运行结果：

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`：通过。
- `cmake --build build -j`：通过。
- `ctest --test-dir build --output-on-failure`：通过，`4/4` tests passed。

CTest 当前测试：

- `stage0_smoke`
- `stage1_simple_task_demo`
- `stage1_manual_resume_demo`
- `stage1_delay_awaiter_demo`

## 7. 遇到过哪些 bug，怎么解决

- 初始 `simple_task_demo` 测试仍检查 skeleton 输出。
  - 修复：改成检查真实 `SimpleTask` 输出顺序。
- `done()` 初版没有处理 moved-from 对象。
  - 修复：改为 `return !handle_ || handle_.done();`。
- `manual_resume_demo` 初版 `resume_saved()` 可能重复 resume 同一个 handle。
  - 修复：恢复前复制 handle，随后清空 `saved_handle_`。
- `delay_awaiter_demo` 初版测试仍检查 skeleton 输出。
  - 修复：改成检查真实 delay awaiter 输出。
- `DelayAwaiter` 后台线程如果捕获 `this`，awaiter 生命周期可能悬空。
  - 修复：在 `await_suspend()` 中复制 `delay_`，线程捕获 delay 值和 handle，不捕获 `this`。
- 使用 `std::thread` 后需要更规范地链接线程库。
  - 修复：CMake 中添加 `find_package(Threads REQUIRED)` 和 `Threads::Threads` 链接。

## 8. 还没解决的问题

- Stage 1 的 demo 仍是教学代码，不是最终 coroutine 框架。
- `SimpleTask` 只支持 `void` 返回，不支持 `Task<T>`。
- `unhandled_exception()` 目前直接 `std::terminate()`，还没有异常保存与传播。
- `delay_awaiter_demo` 使用 detached thread，只用于学习。真实系统需要更严格的线程生命周期和调度器管理。
- 尚未接入 `io_uring`，这会在 Stage 4/5 处理。

## 9. 下一阶段要做什么

下一阶段是 Stage 2：Linux 文件 I/O 与同步基线。

Stage 2 目标：

- 学习 `open`、`close`、`read`、`write`、`pread`、`pwrite`、`fsync`、`errno`。
- 实现 RAII `FdGuard`。
- 实现同步 read-process-write baseline。
- 这个 baseline 只作为正确性 oracle，不作为最终 pipeline。

Stage 2 不能提前实现：

- `io_uring`
- backend fallback
- BufferPool
- bounded queue
- metrics
- 最终三阶段 pipeline

## 10. 面试时这一阶段可以怎么讲

Stage 1 我没有直接写 `io_uring`，而是先把 C++20 协程底层机制拆开学习。

我先实现了一个 move-only `SimpleTask`，它持有 `std::coroutine_handle<promise_type>`，负责在析构时销毁 coroutine frame。然后我实现了 `ManualResumeAwaiter`，观察 `co_await` 如何调用 `await_ready`、`await_suspend` 和 `await_resume`，并把当前协程 handle 暴露给外部恢复。最后我实现了 `DelayAwaiter`，用后台线程模拟外部异步事件延迟恢复协程。

这个阶段的关键结论是：协程本身不是性能来源，它是组织异步控制流的语言机制。后续接 `io_uring` 时，I/O 完成事件本质上会扮演 `DelayAwaiter` 后台线程的角色：保存 handle，完成后恢复 handle。

## 11. 防坍缩自检

1. 本阶段是否引入或破坏任何硬性约束？
   - 没有。本阶段只学习协程控制流，没有实现数据路径，不涉及读整文件、无界队列、BufferPool 或 pipeline。
2. 当前内存是否仍然有界？
   - 对当前 demo 来说是有界的。每个 demo 只创建少量 coroutine frame 和固定数量对象。
3. 当前是否能通过 T1：256MB 处理 50GB 不 OOM？
   - 不能。当前没有大文件处理 pipeline。
4. 如果不能，说明在哪个阶段满足。
   - Stage 8 引入 BufferPool、PipelineConfig 和 backpressure。
   - Stage 10 形成端到端 pipeline。
   - Stage 11/13 做 benchmark 和错误测试验证。
5. 本阶段新增代码中，buffer 所有权如何流转？
   - 本阶段没有数据 buffer。
   - 所有权重点是 coroutine frame：`SimpleTask` 唯一拥有 frame，禁止拷贝，允许移动，析构时 `destroy()`。
6. 哪些验收测试还不能跑？
   - T1/T1b/T2/T3/T4/T5/T6/T7/T8/T9 都还不能跑。
7. 将在什么阶段满足？
   - T1/T1b/T2：Stage 8 后具备机制，Stage 10/11 验证。
   - T3/T4/T5：Stage 10 pipeline 后验证。
   - T6/T7/T8：Stage 13 错误测试与安全测试补齐。
   - T9：Stage 10 引入真实处理 demo，Stage 11 分析。

## 12. 我在本阶段亲手写过哪些关键代码

- `SimpleTask::promise_type`
- `SimpleTask` 的 move-only RAII 管理
- `ManualResumeAwaiter`
- `DelayAwaiter`
- `await_ready()`
- `await_suspend(std::coroutine_handle<>)`
- `await_resume()`
- `resume_saved()` 的一次性恢复逻辑
- 后台线程延迟恢复 coroutine handle 的 demo

## 13. 我还需要补练哪些代码

- 不看已有代码，重新手写一个 `SimpleTask`。
- 给 `SimpleTask` 增加异常保存与传播。
- 把 detached thread 版本的 `DelayAwaiter` 改成可 join 的安全版本。
- 解释为什么 coroutine handle 不能在 coroutine frame 销毁后继续使用。
- 用自己的话画出 simple/manual/delay 三个 demo 的控制流。
