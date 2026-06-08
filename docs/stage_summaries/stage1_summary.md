# Stage 1 Summary: C++20 Coroutine Learning

## 1. 当前阶段完成了什么

- 进入 Stage 1：C++20 协程专项学习。
- 新增 `stage1_simple_task_demo` 可执行程序。
- 实现一个最小 `SimpleTask` 协程返回类型，用于学习：
  - `promise_type`
  - `std::coroutine_handle`
  - `initial_suspend`
  - `final_suspend`
  - `co_return`
  - 手动 `resume()`
  - RAII 销毁 coroutine frame
- 新增 CTest smoke test，验证 Stage 1 demo 的输出顺序。
- 保留 Stage 0 smoke test，确认 Stage 1 没破坏原有骨架。

## 2. 当前目录结构

```text
.
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── README_USAGE.md
├── benchmark/
├── docs/
│   ├── anti_collapse_checklist.md
│   ├── benchmark.md
│   ├── design.md
│   ├── interview.md
│   ├── project_manual.docx
│   ├── staged_prompts.txt
│   └── stage_summaries/
│       ├── stage0_summary.md
│       └── stage1_summary.md
├── examples/
│   └── coroutines/
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
    └── stage1_simple_task_demo_test.cmake
```

## 3. 新增或修改了哪些文件

- 修改 `CMakeLists.txt`
- 新增 `examples/coroutines/simple_task_demo.cpp`
- 新增 `tests/stage1_simple_task_demo_test.cmake`
- 新增 `docs/stage_summaries/stage1_summary.md`

当前工作区还存在 Driver/Navigator 文档相关未提交变更：

- `.agents/skills/asyncdataloader-cpp-coach/SKILL.md`
- `AGENTS.md`
- `docs/CODEX_START_PROMPTS.txt`
- `docs/CODEX_START_PROMPTS_DRIVER_NAVIGATOR.txt`

这些不是 Stage 1 协程 demo 的核心代码路径。

## 4. 每个文件的作用

- `CMakeLists.txt`：新增 `stage1_simple_task_demo` target，并注册 `stage1_simple_task_demo` CTest。
- `examples/coroutines/simple_task_demo.cpp`：Stage 1 第一个协程学习 demo，演示一个手写 coroutine return object 如何持有、恢复和销毁 coroutine frame。
- `tests/stage1_simple_task_demo_test.cmake`：运行 Stage 1 demo，检查输出中包含阶段标识、`before resume`、协程体输出、`after resume` 和 `done=true`。
- `docs/stage_summaries/stage1_summary.md`：本阶段交接总结。

## 5. 当前能运行的命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/stage1_simple_task_demo
```

Stage 1 demo 当前输出：

```text
AsyncDataLoader - Stage 1 simple task demo
before resume
Hello from coroutine!
after resume
done=true
```

这个输出证明：

- 调用 `hello_coroutine()` 后，协程体没有立即执行。
- `initial_suspend()` 返回 `std::suspend_always`，让协程先挂起。
- 调用 `task.resume()` 后，协程体开始执行。
- `co_return` 后协程到达 `final_suspend()`，`done()` 返回 true。

## 6. 当前测试结果

实际运行结果：

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`：通过。
- `cmake --build build -j`：通过。
- `./build/stage1_simple_task_demo`：通过，输出顺序符合预期。
- `ctest --test-dir build --output-on-failure`：通过，`2/2` tests passed。

CTest 当前测试：

- `stage0_smoke`：通过。
- `stage1_simple_task_demo`：通过。

## 7. 遇到过哪些 bug，怎么解决

- 初始 Stage 1 CTest 仍检查 `coroutine skeleton`，但 demo 已经改成真实 `SimpleTask` 输出，导致测试失败。
  - 修复：把测试从 skeleton 语义改为 `stage1_simple_task_demo`，检查真实 demo 输出。
- 第一版测试没有先检查进程退出码。
  - 修复：在 `tests/stage1_simple_task_demo_test.cmake` 中添加 `result != 0` 检查。
- `done()` 初版只调用 `handle_.done()`，对 moved-from 对象不安全。
  - 修复：改成 `return !handle_ || handle_.done();`。
- 移动构造需要表达不抛异常。
  - 修复：给 `SimpleTask(SimpleTask&& other)` 添加 `noexcept`。
- `std::terminate()` 需要明确头文件来源。
  - 修复：显式 include `<exception>`。

## 8. 还没解决的问题

- Stage 1 只完成了第一个 `SimpleTask` demo。
- 尚未实现 `manual_resume_demo`。
- 尚未实现 `delay_awaiter_demo`。
- 尚未把协程问答整理进 `docs/interview.md`。
- 当前 `SimpleTask` 只支持 `void` 返回，不支持 `Task<T>` 返回值和异常传播。
- 当前 `unhandled_exception()` 直接 `std::terminate()`，后续需要学习如何保存并传播异常。
- 当前还没有 `co_await` awaiter 示例。

## 9. 下一阶段要做什么

严格来说，下一步仍在 Stage 1 内，不进入 Stage 2。

Stage 1 后续小任务：

1. 实现 `manual_resume_demo`：学习 `await_ready`、`await_suspend`、`await_resume`，理解 awaiter 如何保存 `coroutine_handle`。
2. 实现 `delay_awaiter_demo`：学习异步等待模型，模拟“外部事件完成后恢复协程”。
3. 整理 `docs/interview.md` 的 C++20 协程问答。
4. 生成 Stage 1 数据流说明图或文字版流程图。

完成整个 Stage 1 后，才进入 Stage 2：Linux 文件 I/O 与同步基线。

## 10. 面试时这一阶段可以怎么讲

这一阶段我没有直接接 `io_uring`，而是先学习 C++20 协程的底层生命周期。我手写了一个最小 `SimpleTask`，让它作为 coroutine return object 持有 `std::coroutine_handle<promise_type>`。

我能解释一个协程函数被调用后的关键流程：

```text
调用 coroutine 函数
-> 分配 coroutine frame
-> 构造 promise_type
-> get_return_object() 返回 SimpleTask
-> initial_suspend() 决定是否先挂起
-> 外部 resume() 后进入函数体
-> co_return 调用 return_void()
-> final_suspend()
-> SimpleTask 析构时 destroy coroutine frame
```

这为后续 Stage 5 做准备。后续把 `io_uring` 接进来时，I/O 后端完成 CQE 后，本质上就是拿到先前保存的 `coroutine_handle` 并恢复协程。协程本身不是性能来源，它主要负责把异步控制流写得更清晰。

## 11. 防坍缩自检

1. 本阶段是否引入或破坏任何硬性约束？
   - 没有。本阶段只学习 C++20 协程生命周期，没有实现数据路径，不涉及文件读取、队列、BufferPool 或 pipeline。
2. 当前内存是否仍然有界？
   - 当前 demo 只创建一个很小的 coroutine frame，没有大文件缓冲区。对当前 demo 来说内存是固定且有界的。
3. 当前是否能通过 T1：256MB 处理 50GB 不 OOM？
   - 不能。当前没有大文件处理 pipeline。
4. 如果不能，说明在哪个阶段满足。
   - Stage 8 引入 BufferPool、PipelineConfig 和背压控制后具备有界内存机制。
   - Stage 10 形成端到端预处理 pipeline。
   - Stage 11/13 通过 benchmark 和错误测试验证大文件 bounded-memory 行为。
5. 本阶段新增代码中，buffer 所有权如何流转？
   - 本阶段没有数据 buffer。
   - 新增的所有权重点是 coroutine frame：`SimpleTask` 拥有 `std::coroutine_handle` 指向的 coroutine frame，拷贝被禁用，移动会转移所有权，析构时调用 `destroy()`。
6. 哪些验收测试还不能跑？
   - T1/T1b：有界内存大文件与规模不变性。
   - T2：背压消融对照。
   - T3：三级重叠生效。
   - T4/T5：输出正确与乱序写正确。
   - T6：崩溃安全。
   - T7：ASan/TSan 满负荷测试。
   - T8：backend fallback。
   - T9：真实重 CPU Stage。
7. 将在什么阶段满足？
   - T1/T1b/T2：Stage 8 后具备机制，Stage 10/11 验证。
   - T3/T4/T5：Stage 10 pipeline 后验证。
   - T6/T7/T8：Stage 13 错误测试与安全测试补齐。
   - T9：Stage 10 引入真实处理 demo，Stage 11 做性能分析。

## 12. 我在本阶段亲手写过哪些关键代码

用户亲手写了 `examples/coroutines/simple_task_demo.cpp` 中的核心协程代码：

- `SimpleTask::promise_type`
- `get_return_object()`
- `initial_suspend()`
- `final_suspend()`
- `return_void()`
- `unhandled_exception()`
- `using Handle = std::coroutine_handle<promise_type>`
- `SimpleTask` 构造函数
- 禁用拷贝构造和拷贝赋值
- 移动构造和移动赋值
- 析构函数中 `handle_.destroy()`
- `resume()`
- `done()`
- `hello_coroutine()`
- `main()` 中创建 task、手动 resume、检查 done 状态

这些是 Stage 1 的关键 C++ 训练点，不是单纯样板代码。

## 13. 我还需要补练哪些代码

- 不看现有文件，重新手写一个最小 `SimpleTask`。
- 故意把 `initial_suspend()` 改成 `std::suspend_never`，观察输出顺序变化，并解释原因。
- 故意允许拷贝 `SimpleTask`，推理为什么会 double destroy。
- 写一个 `ManualResumeAwaiter`，让 `await_suspend()` 保存 `coroutine_handle`，再由外部手动恢复。
- 把 `unhandled_exception()` 从 `std::terminate()` 改成保存 `std::exception_ptr`，在 `resume()` 或 `result()` 中重新抛出。

