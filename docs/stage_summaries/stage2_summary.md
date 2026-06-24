# Stage 2 Summary: Linux File I/O and Sync Baseline

## 1. 当前阶段完成了什么

- 实现 Linux 文件描述符 RAII 封装 `FdGuard`。
- 实现同步文件 I/O helper：
  - `open_read_only`
  - `read_at`
  - `write_all_at`
  - `fsync_fd`
- 实现同步 read-process-write baseline：
  - 按 `block_size` 流式读取输入文件。
  - 对每个 block 做简单 CPU transformation：ASCII 字母大小写翻转。
  - 按 offset 写入输出文件。
  - 写完后调用 `fsync_fd`。
- 实现 `Timer`，使用 `std::chrono::steady_clock` 计时。
- 添加可运行 demo：`stage2_sync_preprocess_demo`。
- 为 Stage 2 组件添加 CTest 覆盖。

这个阶段的 baseline 是后续 pipeline 的正确性 oracle，不是最终 pipeline。

## 2. 当前目录结构

```text
.
├── CMakeLists.txt
├── AGENTS.md
├── README.md
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
│       ├── stage1_summary.md
│       └── stage2_summary.md
├── examples/
│   ├── coroutines/
│   │   ├── delay_awaiter_demo.cpp
│   │   ├── manual_resume_demo.cpp
│   │   └── simple_task_demo.cpp
│   └── stage2_sync_preprocess_demo.cpp
├── include/
│   ├── baseline/
│   │   └── sync_preprocess_baseline.h
│   ├── backend/
│   ├── buffer/
│   ├── config/
│   ├── coroutine/
│   ├── metrics/
│   ├── pipeline/
│   └── util/
│       ├── fd_guard.h
│       ├── file_io.h
│       └── timer.h
├── src/
│   ├── baseline/
│   │   └── sync_preprocess_baseline.cpp
│   ├── main.cpp
│   └── util/
│       ├── fd_guard.cpp
│       ├── file_io.cpp
│       └── timer.cpp
└── tests/
    ├── stage0_smoke_test.cmake
    ├── stage1_delay_awaiter_demo_test.cmake
    ├── stage1_manual_resume_demo_test.cmake
    ├── stage1_simple_task_demo_test.cmake
    ├── stage2_fd_guard_test.cpp
    ├── stage2_file_io_test.cpp
    ├── stage2_sync_preprocess_baseline_test.cpp
    ├── stage2_sync_preprocess_demo_test.cmake
    └── stage2_timer_test.cpp
```

## 3. 新增或修改了哪些文件

- 修改 `CMakeLists.txt`
- 新增 `include/util/fd_guard.h`
- 新增 `src/util/fd_guard.cpp`
- 新增 `include/util/file_io.h`
- 新增 `src/util/file_io.cpp`
- 新增 `include/util/timer.h`
- 新增 `src/util/timer.cpp`
- 新增 `include/baseline/sync_preprocess_baseline.h`
- 新增 `src/baseline/sync_preprocess_baseline.cpp`
- 新增 `examples/stage2_sync_preprocess_demo.cpp`
- 新增 `tests/stage2_fd_guard_test.cpp`
- 新增 `tests/stage2_file_io_test.cpp`
- 新增 `tests/stage2_sync_preprocess_baseline_test.cpp`
- 新增 `tests/stage2_sync_preprocess_demo_test.cmake`
- 新增 `tests/stage2_timer_test.cpp`
- 新增 `docs/stage_summaries/stage2_summary.md`

当前 `docs/CODEX_START_PROMPTS_DRIVER_NAVIGATOR.txt` 是本地提示词文件，已从版本控制撤下并由 `.gitignore` 忽略；它不是 Stage 2 功能收尾的一部分，本阶段未依赖它。

## 4. 每个文件的作用

- `include/util/fd_guard.h` / `src/util/fd_guard.cpp`
  - Move-only RAII wrapper。
  - 析构时自动 `close(fd)`。
  - 禁止拷贝，允许移动，避免 double close。

- `include/util/file_io.h` / `src/util/file_io.cpp`
  - 封装 Linux `open`、`pread`、`pwrite`、`fsync`。
  - 显式返回 `error_number`，避免异常路径隐藏系统错误。
  - `read_at` 处理 EOF 和短读。
  - `write_all_at` 循环处理短写，并对 `EINTR` 重试。
  - `fsync_fd` 对 `EINTR` 重试并返回 `errno`。

- `include/util/timer.h` / `src/util/timer.cpp`
  - 基于 `std::chrono::steady_clock` 的简单耗时计时器。
  - 提供 `elapsed_ns()` 和 `elapsed_ms()`。

- `include/baseline/sync_preprocess_baseline.h` / `src/baseline/sync_preprocess_baseline.cpp`
  - Stage 2 同步 baseline API。
  - 输入：`input_path`、`output_path`、`block_size`。
  - 输出：`bytes_read`、`bytes_written`、`error_number`。
  - 使用单个 `block_size` 大小 buffer 流式处理文件。

- `examples/stage2_sync_preprocess_demo.cpp`
  - 可执行 demo。
  - 命令行参数：`<input> <output> <block_size>`。
  - 调用同步 baseline，并输出 `bytes_read`、`bytes_written`、`elapsed_ms`。

- `tests/stage2_fd_guard_test.cpp`
  - 验证 `FdGuard` 析构关闭 fd、move constructor、move assignment。

- `tests/stage2_file_io_test.cpp`
  - 验证 `open_read_only`、`read_at`、短读、`write_all_at`、`fsync_fd`。

- `tests/stage2_sync_preprocess_baseline_test.cpp`
  - 验证 baseline 能按小 block 处理 `"abcXYZ"`，输出 `"ABCxyz"`。

- `tests/stage2_sync_preprocess_demo_test.cmake`
  - 运行 demo，可证伪命令行入口是否真正生成正确输出文件。

- `tests/stage2_timer_test.cpp`
  - 验证 `Timer` elapsed 时间会随时间增长。

## 5. 当前能运行的命令

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/stage2_sync_preprocess_demo <input> <output> <block_size>
```

demo 示例：

```bash
./build/stage2_sync_preprocess_demo input.bin output.bin 4096
```

输出格式：

```text
bytes_read=<N> bytes_written=<N> elapsed_ms=<ms>
```

## 6. 当前测试结果

实际运行结果：

- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`：通过。
- `cmake --build build -j`：通过。
- `ctest --test-dir build --output-on-failure`：通过，`9/9` tests passed。

当前 CTest 测试：

- `stage0_smoke`
- `stage1_simple_task_demo`
- `stage1_manual_resume_demo`
- `stage1_delay_awaiter_demo`
- `stage2_fd_guard`
- `stage2_file_io`
- `stage2_sync_preprocess_baseline`
- `stage2_timer`
- `stage2_sync_preprocess_demo`

## 7. 遇到过哪些 bug，怎么解决

- `fsync_fd` 初始 stub 返回成功，无法报告 `EBADF`。
  - 修复：调用 `::fsync`，对 `EINTR` 重试，失败时保存并返回 `errno`。

- `Timer::elapsed_ns()` / `elapsed_ms()` 初版计算了 duration 但没有 `return`。
  - 修复：显式返回 `duration_cast` 或 `duration<double, std::milli>` 的 `count()`。

- `Timer::elapsed_ms()` 初版写成 `std::chrono::milli`。
  - 修复：`milli` 是 `std::milli`。

- demo 初版只打印 usage 并返回 1。
  - 修复：解析命令行参数，调用 baseline，打印结果。

- demo 输出格式初版不满足测试契约。
  - 修复：输出 `bytes_read=<N>` 和 `bytes_written=<N>`。

## 8. 还没解决的问题

- `docs/project_manual.md` 不存在，当前仓库只有 `docs/project_manual.docx`。
- `stage2_sync_preprocess_demo` 里 `std::stoull(argv[3])` 对非法参数会抛异常；Stage 13 错误测试阶段应补更严格的命令行参数错误测试。
- 当前可靠落盘只是 `fsync(output fd)`；最终项目需要 `temporary file -> fsync -> rename -> fsync(parent directory)`，预计 Stage 10/13 完善。
- 当前 baseline 是串行 read-process-write，只能作为 oracle，不能作为最终 pipeline。
- 当前没有 BufferPool、backpressure、有界队列、metrics registry、backend fallback、io_uring。
- 当前没有 ASan/TSan 验证，不能声称线程/内存安全。
- 当前没有真实 benchmark 数字，也没有 mmap 对比。

## 9. 下一阶段要做什么

下一阶段是 Stage 3：Benchmark 框架与 mmap 对比基线。

Stage 3 应该做：

- 建立最小 benchmark 框架。
- 对 sync baseline 做可重复计时。
- 添加 mmap sequential scan 对比基线。
- 输出 CSV 或可记录的结果格式。
- 明确 benchmark 公平性和环境说明。

Stage 3 不能做：

- 不提前实现 io_uring。
- 不提前做 backend fallback。
- 不做最终 pipeline。
- 不编造 benchmark 数字。
- 不声称 mmap 或 sync 一定更快。

## 10. 面试时这一阶段可以怎么讲

Stage 2 我实现的是 Linux 文件 I/O 同步基线。

我先用 RAII 封装文件描述符，`FdGuard` 是 move-only 类型，防止拷贝导致 double close。然后封装了 `pread`、`pwrite`、`fsync`，所有接口都返回显式错误码，能处理 EOF、短读、短写和 `EINTR`。

同步 baseline 按固定 `block_size` 读取文件，每次只持有一个 block buffer，处理后按 offset 写回输出文件，并在结束时 `fsync`。这个 baseline 不追求并发性能，它的价值是作为后续异步 pipeline 的正确性 oracle：后面无论 io_uring、threadpool 或多 stage pipeline 怎么实现，输出都必须与这个串行 baseline 一致。

这一阶段还实现了 `Timer` 和一个可运行 demo，证明当前工程可以从输入文件生成处理后的输出文件。

## 11. 防坍缩自检

1. 本阶段是否引入或破坏任何硬性约束？
   - 没有破坏硬性约束。当前串行 read-process-write 明确只是 baseline/oracle，不是最终 pipeline。

2. 当前内存是否仍然有界？
   - 对 Stage 2 baseline 来说，内存主要是一个 `block_size` 大小的 `std::vector<char>` 加固定开销，不随输入文件大小线性增长。
   - 但还没有 Stage 8 的 `BufferPool`、`PipelineConfig`、`max_inflight_buffers` 和 backpressure。

3. 当前能否通过 T1：256MB 处理 50GB 不 OOM？
   - 还不能声称通过。虽然 baseline 是流式的，但尚未实现项目最终要求的有界内存 pipeline、背压和大文件 RSS 验证。
   - T1 的机制预计在 Stage 8 引入，Stage 10/11/13 验证。

4. 本阶段新增代码里，buffer 所有权是谁？
   - `sync_preprocess_baseline` 内部局部变量 `std::vector<char> block` 独占 buffer。
   - `read_at` 只临时写入这个 buffer，不保存指针。
   - CPU preprocessing 原地修改 `block[0..bytes_read)`。
   - `write_all_at` 只临时读取这个 buffer，不保存指针。
   - 循环结束后 buffer 析构释放。
   - 文件描述符由 `FdGuard` 拥有，析构自动关闭。

5. 哪些验收测试还不能跑？
   - T1/T1b/T2：缺 BufferPool、配置化内存上限、背压和 RSS 验证。
   - T3：缺三阶段重叠 pipeline。
   - T4/T5：pipeline 尚不存在，不能和 baseline 做完整对比。
   - T6：缺 temporary file + rename + crash safety 测试。
   - T7：未跑 ASan/TSan。
   - T8：缺 backend fallback。
   - T9：缺真实重 CPU Stage 和 pipeline 重叠验证。

6. 这些验收测试将在什么阶段满足？
   - T1/T1b/T2：Stage 8 建立机制，Stage 10/11/13 验证。
   - T3/T4/T5/T9：Stage 10 端到端 pipeline 后验证。
   - T6/T7/T8：Stage 13 错误测试和安全测试补齐。
