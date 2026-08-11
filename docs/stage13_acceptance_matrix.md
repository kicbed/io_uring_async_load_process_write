# Stage 13：错误与验收覆盖矩阵

日期：2026-08-11

## 1. 本小任务的目标

本文件最初由 Stage 13.1 建立，现在作为 Stage 13 完成后的最终验收账本：把防坍缩
硬约束、T1-T9、生产实现、自动测试、环境实跑和仍未完成的部分一一对应。

Stage 13 的完成表示“计划内的错误测试、环境验收和文档已收口”，不表示 T1-T9
全部通过。未实现的调度或性能目标继续明确标红，不能用文档措辞把它们变成成功。

状态含义：

- **自动覆盖**：仓库中存在能够判错的自动测试；是否在本轮通过，单独记录。
- **部分覆盖**：核心机制或小规模证据存在，但尚未满足完整验收条件。
- **未覆盖**：没有满足该验收目标的实现或自动测试。
- **环境受限**：测试路径存在，但当前运行环境无法完成规定验证。

## 2. Stage 13 的五个小任务

| 小任务 | 目标 | 当前状态 |
|---|---|---|
| 13.1 | 盘点 H1-H8、T1-T9、错误路径和现有证据 | **完成：本文件** |
| 13.2 | 补确定性错误测试，优先处理尚未覆盖的 H7 边界 | **完成：见 13.2 说明与两组自动测试** |
| 13.3 | 增加 `kill -9` 崩溃可靠性测试，验证最终文件不出现半成品 | **完成：真实子进程 SIGKILL 测试** |
| 13.4 | 执行或明确隔离大文件、TSan、`O_DIRECT` 和构建 fallback | **完成：50 GiB T1、真实 O_DIRECT、无 liburing 全构建、Sanitizer 记录** |
| 13.5 | 最终整理 README、design、benchmark、interview 和 Stage 13 总结 | **完成** |

阶段总结见 `docs/stage_summaries/stage13_summary.md`。

## 3. 当前系统的数据流和错误流

### 正常路径

```text
preprocess_pipeline_demo::main
  -> parse_options() / validate_metrics_json_path()
  -> BackendFactory::create()
  -> MetricsRegistry + Pipeline + ByteIncrementStage
  -> PipelineExecutor::run_file()
       -> open_read_only(input)
       -> reject_same_input_and_output()
       -> AtomicOutputFile 创建同目录临时文件
       -> PipelineExecutor::run()
            -> 固定大小 AlignedBufferPool
            -> 两个固定容量 SPSCQueue<BlockWorkItem>
            -> writer jthread
            -> processor jthread
            -> reader jthread
            -> join 三个 worker
       -> AtomicOutputFile::commit()
            -> fsync(临时文件)
            -> rename(临时文件, 最终文件)
            -> fsync(父目录)
  -> verify_incremented_output() 有界二次校验
  -> MetricsRegistry::snapshot()
  -> 可选 write_metrics_json_atomic()
  -> terminal / key=value 输出
```

### Worker 失败路径

```text
reader / processor / writer 抛异常
  -> run_guarded() 捕获
  -> fail_all() 保存第一份 exception_ptr
  -> 两个 SPSCQueue::fail()
       -> 唤醒阻塞的 push()/pop()
       -> 销毁排队的 BlockWorkItem
       -> BufferHandle 析构并把 lease 归还 BufferPool
  -> join_workers()
  -> PipelineExecutor::run() 重新抛出第一份异常
  -> AtomicOutputFile 析构并删除未提交临时文件
  -> main() 输出错误并返回非零状态
```

这样设计的关键价值是：正常路径和异常路径使用同一套 RAII 所有权规则，
不需要维护一套容易遗漏 buffer 的手写清理流程。

## 4. 关键函数职责

| 函数或类型 | 做什么 | 为什么放在这里 |
|---|---|---|
| `parse_options()` | 解析 CLI 并拒绝非法数字、未知 backend 和矛盾选项 | 在启动线程和创建输出前尽早失败，避免产生副作用 |
| `BackendFactory::create()` | 创建显式 backend，或按 io_uring -> thread pool -> sync 选择 Auto fallback | 把选择策略集中起来，流水线只依赖统一 `IOBackend` |
| `Pipeline::add_stage()` | 接收 `unique_ptr<Stage>` 并取得 Stage 唯一所有权 | 防止 Stage 悬空，同时保留可注册处理链 |
| `PipelineExecutor::run_file()` | 管理输入文件和临时输出文件，并在成功后提交 | 把“执行流水线”和“发布最终文件”组合成安全入口 |
| `PipelineExecutor::run()` | 建池、建队列、启动三个 worker、汇总异常并 join | 集中维护线程、队列和 BufferPool 的生命周期顺序 |
| `reader_loop()` | 获取 lease，读取一个 block，附加 offset/有效字节数并推入队列 | reader 只有拿到池中 buffer 才能前进，因此天然受背压限制 |
| `complete_read()` | 启动 backend Task，并通过 `wait_one()` 推进异步完成 | 统一同步、线程池和 io_uring 的完成方式 |
| `processor_loop()` | 只处理 `valid_data()`，然后移动同一个 work item | 尾块不会处理未读空间，也不复制 payload |
| `writer_loop()` | 按 work item 的原始 offset 完整写出有效字节 | 完成顺序变化时仍具备按位置写正确的基础 |
| `SPSCQueue::push()/pop()` | 在队列满/空时等待，并移动 work item 所有权 | 固定容量提供背压，移动语义避免 buffer 复制和双重所有权 |
| `SPSCQueue::close()` | 表示正常 EOF，保留并排空已入队数据 | 把“正常结束”和“错误停止”分开 |
| `SPSCQueue::fail()` | 保存第一份异常、清空队列并唤醒所有等待方 | 防止线程永久阻塞，并通过 RAII 回收排队 buffer |
| `AlignedBufferPool::acquire()/release()` | 等待可用槽位并回收 lease | 让内存上限由配置决定，而不是由文件大小决定 |
| `BufferHandle` | move-only lease；析构时自动调用 pool 的 `release()` | 用类型系统表达唯一所有权，异常展开时也能自动归还 |
| `Pipeline::process()` | 按注册顺序执行 Stage，并自动记录 Stage 延迟 | 处理逻辑可扩展，计时不侵入具体 Stage |
| `write_all_at()` | 重试 `EINTR`，循环处理短写，保持正确 offset | 单次 `pwrite()` 不保证写完全部数据 |
| `AtomicOutputFile::commit()` | 文件 fsync、原子 rename、目录 fsync | 避免把未完成输出暴露成正式文件 |
| `verify_incremented_output()` | 使用两个固定 block 逐块校验输入与输出 | 提供正确性 oracle，同时不把整个文件载入内存 |

## 5. H1-H8 硬约束覆盖

| 约束 | 当前状态 | 主要实现/测试证据 | 仍需完成 |
|---|---|---|---|
| H1 有界内存与背压 | **T1 实跑覆盖，T2 仍缺** | `AlignedBufferPool`、固定容量 `SPSCQueue`；50 GiB run 为 159,640 KiB RSS，in-flight 19/24、queue 8/8 | T1b 缺 200 GiB；T2 无界负面对照尚无 |
| H2 三级重叠 | 部分覆盖 | `PipelineExecutor::run()` 创建 reader/processor/writer 三个 `jthread`；Stage 11 有同拓扑 overlap/no-overlap harness | 当前实验未形成稳定加速证据，T3 不宣告通过 |
| H3 流式处理 | **50 GiB 实跑覆盖，规模序列仍不完整** | `reader_loop()` 每次只获取一个池 buffer；验证只使用两个固定 block；历史 64 MiB-4 GiB 和本次 50 GiB RSS 仍约 156 MiB | T1b 要求的 200 GiB 点未执行 |
| H4 真实处理 Stage | 部分覆盖 | `ByteIncrementStage` 真实修改每个字节；Stage 7/10 测试验证输出 | 还没有 CPU-heavy Stage 的受控 T9 分析 |
| H5 清晰 buffer 生命周期 | 部分覆盖 | move-only `BufferHandle`、`BlockWorkItem`、队列失败清理；严格警告 + ASan/UBSan Stage 13 `5/5` 通过 | 当前 WSL2 的 TSan runtime 在测试主体前失败，完整 T7 未通过 |
| H6 顺序正确与可靠落盘 | 部分覆盖 | 显式 file offset、`write_all_at()`、`AtomicOutputFile`；Stage 10/11 正确性测试；`stage13_crash_safety` 真实验证 commit 前 `SIGKILL` 不暴露半成品 | T5 多核乱序处理仍未实现 |
| H7 错误与边界 | 广泛自动覆盖 | 原有 EOF/短读等路径；13.2 覆盖 errno/短写/构造失败；真实 ext4 `O_DIRECT` 三维错位通过；无 liburing 完整构建和 Auto pipeline 通过 | native io_uring wait 的真实信号中断路径仍只有重试实现/注入证据 |
| H8 可观测性 | 自动覆盖 | read/process/write Counter、Gauge、Histogram、per-Stage latency；Stage 9/10/12 测试 | 后续只需确保 Stage 13 改动不破坏这些指标 |

## 6. T1-T9 验收矩阵

| 验收 | 当前判定 | 证据 | 缺口/后续归属 |
|---|---|---|---|
| T1：256 MiB 处理 50 GiB | **通过（本环境）** | Release Auto/io_uring 完成 53,687,091,200 字节；输出验证通过；峰值 RSS 159,640 KiB < 300 MiB；queue/in-flight 均未越界 | 单环境单样本，不是跨机器性能结论 |
| T1b：1/50/200 GiB RSS 基本不变 | **未完整通过，部分强证据** | 历史 1/2/4 GiB 约 155.7 MiB，本次 50 GiB 为约 155.9 MiB | 用户决定不执行 200 GiB，因此缺规定的最后规模点 |
| T2：有界/无界背压消融 | **未覆盖** | 生产路径有界；单元测试证明满队列和空池会阻塞 | 没有隔离且限规模的无界负面对照；是否实现需单独评估，不能进入主路径；13.4 |
| T3：三级重叠生效 | **未通过，部分证据** | 三线程架构和同拓扑 one-buffer/多-buffer harness 已存在 | 现有五次 WSL2 结果没有稳定加速，缺稳定时间线/受控证据；13.4 |
| T4：输出等于串行 oracle | **自动覆盖** | `stage10_pipeline_executor`、`stage10_preprocess_pipeline_demo`、`stage11_bench_end_to_end` 和 bounded verifier | Stage 13 应保持回归通过 |
| T5：多核乱序仍按 offset 正确 | **未覆盖** | 当前 writer 使用显式 offset，但 processor 只有一个线程且 FIFO | 多核乱序调度尚未实现；不能用现有 FIFO 测试冒充 T5 |
| T6：`kill -9` 崩溃安全 | **自动覆盖** | `stage13_crash_safety` 在临时文件已有一块处理结果、第二块仍阻塞时真实发送 `SIGKILL`；旧正式文件保持完整，原本不存在的正式文件仍不存在 | 孤儿临时文件回收不是 T6，若需要生产恢复策略必须单独设计 |
| T7：ASan + TSan 满负荷 | **环境受限，未通过** | 严格警告 + ASan/UBSan Stage 13 `5/5` 通过；两个 TSan 目标可编译 | 两个 TSan 二进制均在测试主体前因 `unexpected memory mapping` 退出 66；需原生 Linux 满负荷重试 |
| T8：禁用 io_uring 后 fallback | **自动覆盖** | 运行期候选禁用、注入构造失败、以及 `ASYNCDATALOADER_ENABLE_LIBURING=OFF` 的 55-test 全构建；Auto 输出正确且显式 Uring `ENOSYS` fail-fast | 无剩余构建 fallback 缺口；不代表每台机器都优先选同一 backend |
| T9：CPU-heavy Stage 下保持重叠 | **未通过，部分证据** | `ByteIncrementStage` 是实际修改型 Stage，不是 NoOp/checksum | 处理强度不足以完成 CPU-heavy 特征与重叠分析；13.4 |

## 7. H7 错误路径明细

| 错误或边界 | 当前状态 | 现有证据 | 下一步 |
|---|---|---|---|
| EOF/空文件 | 自动覆盖 | Stage 2、6、10 的短读、EOF、空输入测试 | 保持回归 |
| EOF 前短读 | 自动覆盖 | `stage2_file_io` 及三个 Stage 6 backend 测试 | 保持回归 |
| 短写 | 自动覆盖 | `stage13_file_io_error_paths` 验证 2+3 字节续写、指针/offset 推进、部分成功后 `ENOSPC` 和零字节 `EIO` | 保持回归 |
| 文件不存在 | 自动覆盖 | `stage2_file_io`、Stage 4/5/6 demo、`stage10_reliable_output` | 保持回归 |
| permission denied | 自动覆盖包装合同 | `stage13_file_io_error_paths` 注入 `EACCES` 并验证 open 包装层原样保留，不依赖 root 身份 | 13.4 如需文件系统 DAC 集成证据，应与包装合同分开记录 |
| 无效 fd/操作错误 | 自动覆盖 | Sync、ThreadPool、io_uring backend 保留 `EBADF`；writer 测试保留 `ESPIPE` | 保持回归 |
| backend 不可用/fallback | 自动覆盖选择策略 | 禁用候选、注入 Uring/ThreadPool 构造 `system_error`，并验证显式 fail-fast 与 Auto fallback | 保持回归；运行时操作错误仍不得跨 backend 重试 |
| 编译期无 liburing | 自动覆盖 | CMake 可显式关闭/自动探测缺失；嵌套 CTest 真正重新配置、编译和链接，Auto pipeline 验证 `abc -> bcd` | Uring-only 教学目标按能力省略，不伪装为 skip 后的 Uring 成功 |
| `O_DIRECT` 对齐错误 | 环境自动覆盖 | `stage13_odirect_contract` 在当前 ext4 上验证对齐读，并让错位 address/length/offset 分别返回 `EINVAL` | 主 pipeline 仍为 buffered I/O；其他文件系统可报告 CTest skip，不能声称端到端 direct-I/O backend |
| 非法配置/CLI | 自动覆盖 | `PipelineConfig::validate()`、Stage 3/6/8/10/11/12 参数测试 | 保持回归 |
| `EINTR` | 部分覆盖 | `stage13_file_io_error_paths` 确定性验证 `read_at()`、`write_all_at()`、`fsync_fd()` 重试；io_uring wait 有重试实现 | native io_uring wait 的真实信号路径尚无独立运行证据；13.4 评估 |
| processor/writer 异常 | 自动覆盖 | `stage10_pipeline_executor` 验证异常回到调用方 | 保持回归 |
| 队列失败时释放 lease | 自动覆盖 | `stage10_queue_lifecycle` 验证阻塞方被唤醒且 pool 全部回收 | 保持回归 |
| rename/发布失败 | 自动覆盖 | `stage10_reliable_output`、`stage12_pipeline_reporter`；`stage13_crash_safety` 覆盖 commit 前真实进程强杀 | 保持回归；断电不等同于进程 SIGKILL |
| JSON 路径冲突/非法值 | 自动覆盖 | `stage12_pipeline_reporter`、`stage12_preprocess_pipeline_demo` | 保持回归 |

## 8. 现有自动测试的主要证据入口

| 关注点 | CTest 名称 | 源文件 |
|---|---|---|
| Linux I/O、短读、errno、fsync | `stage2_file_io` | `tests/stage2_file_io_test.cpp` |
| backend 合同和 fallback | `stage6_*` | `tests/stage6_*` |
| Stage 注册与真实变换 | `stage7_*` | `tests/stage7_*` |
| 配置、BufferPool、RAII、背压 | `stage8_*` | `tests/stage8_*` |
| 有界 metrics 和自动计时 | `stage9_*` | `tests/stage9_*` |
| work item、队列失败、三级执行、可靠输出 | `stage10_*` | `tests/stage10_*` |
| serial oracle、overlap harness、RSS sweep、分析工具 | `stage11_*` | `tests/stage11_*` |
| terminal/JSON 及可靠 JSON 发布 | `stage12_*` | `tests/stage12_*` |
| File I/O 的 `EACCES`、`EINTR`、短写和错误进度 | `stage13_file_io_error_paths` | `tests/stage13_file_io_error_paths_test.cpp` |
| backend 构造失败、显式 fail-fast 和 Auto fallback | `stage13_backend_factory_failures` | `tests/stage13_backend_factory_failure_test.cpp` |
| commit 前真实 `SIGKILL` 与正式文件隔离 | `stage13_crash_safety` | `tests/stage13_crash_safety_test.cpp` |
| 当前文件系统的 aligned read 与 address/length/offset 错位 | `stage13_odirect_contract` | `tests/stage13_odirect_contract_test.cpp` |
| 无 liburing 的独立 configure/build/Auto pipeline/显式失败 | `stage13_no_liburing_build` | `tests/stage13_no_liburing_build_test.cmake` |

## 9. Stage 13.4/13.5 最终边界

13.4 在不改变生产流水线拓扑的前提下完成了三项真实环境证据：

1. `stage13_odirect_contract` 使用项目 `AlignedBuffer` 在 ext4 上完成一次真实对齐读，
   并让 address、length、offset 三种错位分别返回 `EINVAL`；不支持的文件系统返回
   CTest skip 77，而不是假装通过。
2. `stage13_no_liburing_build` 在独立目录用
   `ASYNCDATALOADER_ENABLE_LIBURING=OFF` 重新配置和编译；Auto 完整 pipeline 选择
   ThreadPool 并正确处理数据，显式 Uring 以 `ENOSYS` 失败且不创建正式输出。
3. Release 50 GiB T1 使用正式 BufferPool、双有界队列、真实 Stage、可靠发布、metrics
   和 bounded verifier，峰值 RSS 159,640 KiB且输出验证通过。

13.5 将这些结论和边界同步到 README、design、benchmark、interview、环境记录、原始
evidence bundle 和阶段总结。Stage 13 至此完成，但没有提前实现 T2 的无界消融、T5
的多核乱序 processor 或 T9 的 CPU-heavy 实验。

当前仍不能升级为“通过”的项目级目标是 T1b、T2、T3、T5、完整 T7 和 T9。尤其：

- 200 GiB 按用户决定不执行，所以 T1b 不完整；
- WSL2 的 TSan runtime 在测试主体前失败，不能声称无 data race；
- isolated `O_DIRECT` read contract 不等于主流水线已支持 direct I/O。

## 10. 本轮验证记录

最终主要验证命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure

cmake -S . -B build-stage13-no-uring \
  -DCMAKE_BUILD_TYPE=Debug \
  -DASYNCDATALOADER_ENABLE_LIBURING=OFF
cmake --build build-stage13-no-uring -j
ctest --test-dir build-stage13-no-uring --output-on-failure

cmake -S . -B build-stage13-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-stage13-asan \
  --target stage13_file_io_error_paths_test \
           stage13_backend_factory_failure_test \
           stage13_crash_safety_test \
           stage13_odirect_contract_test -j
ctest --test-dir build-stage13-asan -R '^stage13_' --output-on-failure
```

2026-08-11 本轮实际结果：

- Debug CMake configure：通过；
- Debug 完整构建：通过；
- 最终 Debug CTest：`60/60` 通过，0 个失败；
- Release 完整构建与 CTest：`60/60` 通过，0 个失败；
- 无 liburing 完整 Debug 构建与 CTest：`55/55` 通过，0 个失败；
- 13.2 的两项测试各连续运行 100 次：全部通过；
- 13.3 的真实 SIGKILL 测试连续运行 100 次：全部通过；
- 当前 ext4 的 `stage13_odirect_contract`：Pass，没有走 skip；
- 严格警告 + ASan/UBSan 定向构建：通过，没有编译警告；
- ASan/UBSan Stage 13 CTest：`5/5` 通过，0 个失败；
- TSan 两个并发测试：均在测试主体前报 `unexpected memory mapping` 并退出 66，
  因此是环境阻塞，不是通过或已定位的项目 race；
- 50 GiB T1：6400 块全部完成，verification passed，159,640 KiB RSS < 300 MiB，
  in-flight 19/24、两个 queue peak 均为 8/8；
- 200 GiB：按用户决定未执行。

原始 50 GiB CSV、输入 SHA-256、环境和命令已归档；50 GiB 临时输入/输出已经清理。
这些结果完成了 T1、T6 和 T8 在本阶段范围内的证据，但不改变 T1b、T2、T3、T5、
完整 T7 和 T9 的未完成状态，也不等价于断电测试或端到端 `O_DIRECT` 支持。

## 11. 防坍缩自检

1. **是否破坏硬约束？**
   没有。确定性 seam 不改变公共入口；构建 fallback 只裁剪不可用 backend；环境测试
   使用真实有界流水线，没有删除 BufferPool、队列、metrics 或可靠发布。
2. **内存是否仍有界？**
   是。仍由 `block_size * max_inflight_buffers`、两个固定队列和固定 metrics
   上限控制；50 GiB 实跑的 in-flight/queue 峰值均未超过配置，RSS 为 159,640 KiB。
3. **现在能否宣告 T1？**
   可以在本次声明环境下宣告 T1 通过；不能宣告 T1b，因为 200 GiB 未执行。
4. **buffer 所有权是否变化？**
   没有，仍为 `BufferPool -> reader -> processor -> writer -> BufferPool`。
5. **哪些验收仍未完成？**
   T1b、T2、T3、T5、完整 T7 和 T9。T1、T4、T6、T8 有明确自动或环境证据；
   `O_DIRECT` 只证明当前文件系统的隔离合同，不升级成 direct-I/O 主路径。

## 12. 面试一句话

> 我用确定性 syscall/backend 注入覆盖稀有失败，用真实 SIGKILL 验证原子发布，用独立
> no-liburing 构建验证 fallback，再以 50 GiB 流式运行证明 192 MiB BufferPool 配置下
> RSS 约 156 MiB且输出正确；同时把 T1b、TSan 和未实现的调度实验明确留在边界表中。
