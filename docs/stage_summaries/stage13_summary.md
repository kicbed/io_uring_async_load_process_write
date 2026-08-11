# Stage 13 Summary：错误测试、环境验收与面试准备

日期：2026-08-11

## 1. 本阶段完成了什么

Stage 13 在不重写生产流水线的前提下，完成了五个小任务：

- **13.1 验收审计**：建立 H1-H8、T1-T9、生产实现和自动测试的覆盖矩阵，禁止把
  部分证据写成全通过。
- **13.2 确定性错误测试**：为文件 I/O 与 BackendFactory 增加内部操作表 seam，
  稳定覆盖 `EACCES`、`EINTR`、短写、零进度、部分成功后失败、显式 fail-fast 和
  Auto 构造期 fallback。
- **13.3 崩溃安全**：父进程使用 `fork + exec` 启动真实三-worker child，通过 pipe
  冻结第二块处理，在临时文件已有部分结果时发送真实 `SIGKILL`，验证最终文件仍为
  旧完整版本或仍不存在。
- **13.4 环境验收**：支持完整无-liburing 构建；增加真实文件系统 `O_DIRECT` 三维
  对齐合同测试；执行 Debug/Release/ASan/UBSan/TSan 探测；完成一次 50 GiB T1。
- **13.5 文档与面试包装**：同步 README、design、benchmark、interview、验收矩阵、
  环境说明和原始 evidence bundle，并写出 1 分钟/3 分钟介绍与简历表达。

最终正常数据流没有改变：

```text
BackendFactory -> selected IOBackend
  -> reader acquires one BufferHandle and reads one block
  -> bounded read/process queue
  -> registered CPU Stage transforms valid bytes in place
  -> bounded process/write queue
  -> writer pwrite()s at the recorded offset
  -> BufferHandle destructor returns the lease
  -> fsync(temp) -> atomic rename -> fsync(parent directory)
  -> bounded streaming verifier
```

Stage 13 增强的是可证伪性、环境兼容性和解释边界，不是增加第四个数据阶段。

## 2. 当前相关目录结构

```text
.
|-- CMakeLists.txt
|-- README.md
|-- include/
|   |-- backend/detail/backend_factory_operations.h
|   `-- util/detail/file_io_operations.h
|-- src/
|   |-- backend/backend_factory.cpp
|   `-- util/file_io.cpp
|-- tests/
|   |-- stage6_backend_factory_test.cpp
|   |-- stage6_backend_fallback_demo_test.cmake
|   |-- stage13_file_io_error_paths_test.cpp
|   |-- stage13_backend_factory_failure_test.cpp
|   |-- stage13_crash_safety_test.cpp
|   |-- stage13_odirect_contract_test.cpp
|   `-- stage13_no_liburing_build_test.cmake
`-- docs/
    |-- design.md
    |-- benchmark.md
    |-- interview.md
    |-- stage13_acceptance_matrix.md
    |-- stage13_deterministic_error_tests.md
    |-- stage13_crash_safety.md
    |-- stage13_environment_acceptance.md
    |-- benchmark_results/
    |   `-- 2026-08-11-stage13-t1-50g/
    |       |-- README.md
    |       |-- commands.md
    |       |-- environment.md
    |       `-- raw/
    |           |-- t1-50g.csv
    |           |-- input-sha256.txt
    |           `-- command-observations.txt
    `-- stage_summaries/stage13_summary.md
```

Stage 0-12 的 backend、coroutine、BufferPool、Pipeline、Metrics、Benchmark 和
Reporter 文件全部保留。

## 3. 新增或修改的文件

### 生产代码与构建

- `include/util/detail/file_io_operations.h`
  - 定义内部 `FileIOOperations` 函数指针表和四个 `*_with()` 算法入口。
- `src/util/file_io.cpp`
  - 公共 API 继续调用真实 syscall；内部算法支持确定性注入，并正确保留 errno、
    重试 `EINTR`、循环短写、推进指针/offset、处理零进度和部分成功。
- `include/backend/detail/backend_factory_operations.h`
  - 定义内部 backend constructor 表和 `create_backend_with()` 策略入口。
- `src/backend/backend_factory.cpp`
  - 把真实构造函数接入统一策略；无 liburing 构建中显式 Uring 返回 `ENOSYS`，Auto
    继续 ThreadPool/Sync；非法 queue depth 仍保持 `invalid_argument`。
- `CMakeLists.txt`
  - 新增 `ASYNCDATALOADER_ENABLE_LIBURING`；按探测结果裁剪 Uring 源码、链接、教学
    target 和测试；注册五项 Stage 13 测试及 `O_DIRECT` skip code 77。

### 测试

- `tests/stage13_file_io_error_paths_test.cpp`
  - 测试 open `EACCES`、read/fsync `EINTR`、短写续写、部分成功后 `ENOSPC` 和
    零进度 `EIO`。
- `tests/stage13_backend_factory_failure_test.cpp`
  - 用脚本化 constructor 测试显式选择和 Auto fallback 的捕获边界。
- `tests/stage13_crash_safety_test.cpp`
  - 父/子进程、pipe 同步、真实三 worker、真实 `SIGKILL` 和两种 final-file 场景。
- `tests/stage13_odirect_contract_test.cpp`
  - 当前文件系统上的 aligned read，以及 address/length/offset 三种 `EINVAL`；不支持
    的环境返回 skip，不把环境缺失当成功。
- `tests/stage13_no_liburing_build_test.cmake`
  - 新建独立无-liburing build，编译 factory 和完整 demo，验证 Auto `abc -> bcd`、
    显式 Uring 失败及无正式输出；失败时保留完整诊断后清理唯一测试目录。
- `tests/stage6_backend_factory_test.cpp`
  - 同时适配有/无 Uring 编译能力的工厂合同。
- `tests/stage6_backend_fallback_demo_test.cmake`
  - 根据构建能力分别验证显式 Uring 成功或清晰失败，并保持 Auto fallback 断言。

### 文档与证据

- `README.md`
  - 标记 Stage 13 完成，加入无-liburing 构建、Stage 13 测试、T1 和剩余边界入口。
- `docs/design.md`
  - 记录错误 seam、进程崩溃测试、构建能力、`O_DIRECT` tri-state 和不变量。
- `docs/benchmark.md`
  - 加入实际 50 GiB T1 记录，并继续限制单样本/WSL2/零数据的性能解释。
- `docs/interview.md`
  - 加入 Stage 13 常见追问、1/3 分钟介绍和可防守的简历描述。
- `docs/stage13_acceptance_matrix.md`
  - 成为最终 H/T 状态账本：T1/T4/T6/T8 有证据，未完成项仍明确保留。
- `docs/stage13_deterministic_error_tests.md`
  - 解释内部 seam、函数流和 13.2 的证明边界。
- `docs/stage13_crash_safety.md`
  - 解释同步 kill 点、最终文件保证、孤儿临时文件和断电边界。
- `docs/stage13_environment_acceptance.md`
  - 解释 13.4 所有代码、环境、命令、实跑结果与防坍缩检查。
- `docs/benchmark_results/2026-08-11-stage13-t1-50g/`
  - 保存实际环境、命令、输入身份、原始 CSV 和外层命令观察；不保存巨大数据文件。
- `docs/stage_summaries/stage13_summary.md`
  - 本交接总结。

## 4. 核心函数、逻辑和 C++/系统概念

### 文件 I/O 错误算法

公共 `write_all_at()` 取得真实 `FileIOOperations` 后进入
`write_all_at_with()`。后者维护“已完成字节数、当前数据指针、剩余长度、当前文件
offset”四个同步状态：成功写 N 字节就一起推进；`EINTR` 不推进并重试；其他错误
立即返回 errno 和已经完成的字节数；非空请求返回 0 被转换为 `EIO`，避免死循环。

函数指针表是很小的 dependency-injection seam。它比全局测试开关安全，因为每次
调用明确传入依赖，没有跨测试残留；又比为四个 syscall 创建抽象基类更简单。

### BackendFactory 的两层 fallback

`create_backend_with()` 只拥有选择策略，`BackendFactoryOperations` 提供构造动作：

```text
explicit -> 调一次指定 constructor，任何错误原样返回
Auto -> Uring constructor system_error
     -> ThreadPool constructor system_error
     -> Sync
```

无 liburing 时，CMake 不编译/链接 Uring 实现，而 `create_uring()` 仍能用 `ENOSYS`
表达缺少能力。这样“构建图是否含某功能”和“运行时策略如何选择”既分离又一致。

### 崩溃安全为什么必须从进程外测

`SIGKILL` 不可捕获，不能期待 child 的 RAII 析构清理。父进程必须独立存活，借助 pipe
知道 child 已到确定 kill 点，再由 `waitpid()` 判断退出信号并检查 final path。
测试允许 orphan temp，却不允许 partial final；这精确对应 atomic rename 的保证。

### `O_DIRECT` 为什么有三个对齐量

对齐内存地址只是第一步。系统还可能要求请求长度和文件 offset 对齐。测试分别只错
一个变量，才能知道哪个合同被验证。CTest skip 77 表示“本机无法提供该实验”，
不会把 portability 条件写成产品错误，也不会冒充 pass。

### 50 GiB 为什么仍然有界

50 GiB 只让 reader 循环 6,400 次。物理 payload 槽位始终最多 24 个，每条队列最多
8 个 work item，metrics 没有 per-block vector，验证只复用两个 8 MiB block。因此
文件规模增加的是时间和磁盘字节，不是与文件大小线性增长的内存。

## 5. 当前可用命令

默认 Debug：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Release：

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

无 liburing 完整构建：

```bash
cmake -S . -B build-stage13-no-uring \
  -DCMAKE_BUILD_TYPE=Debug \
  -DASYNCDATALOADER_ENABLE_LIBURING=OFF
cmake --build build-stage13-no-uring -j
ctest --test-dir build-stage13-no-uring --output-on-failure
```

只跑 Stage 13：

```bash
ctest --test-dir build -R '^stage13_' --output-on-failure
```

ASan/UBSan 和 50 GiB 命令分别见
`docs/stage13_environment_acceptance.md` 与 evidence bundle 的 `commands.md`。

## 6. 当前测试结果

2026-08-11 实际执行结果：

- Debug 完整 configure/build：通过；CTest `60/60` 通过；
- Release 完整 configure/build：通过；CTest `60/60` 通过；
- 无 liburing 完整 Debug configure/build：通过；CTest `55/55` 通过；
- 13.2 两项确定性测试各连续运行 100 次：全部通过；
- 13.3 真实 SIGKILL 测试连续运行 100 次：全部通过；
- 严格警告 + ASan/UBSan Stage 13：`5/5` 通过，无编译警告；
- 当前 ext4 `O_DIRECT` 合同测试：Pass，未 skip；
- TSan 两个目标编译通过，但运行时均在测试主体前以
  `ThreadSanitizer: unexpected memory mapping`、退出码 66 失败；T7 未通过；
- 50 GiB T1：输出验证通过、159,640 KiB RSS < 300 MiB、in-flight 19/24、两条
  queue peak 8/8；
- 200 GiB：按用户决定未执行。

没有把 TSan 环境失败、CTest skip 机制或单样本吞吐包装成成功结论。

## 7. 遇到的问题与最小修复

- **root 下真实 permission 测试不稳定**：root 可能绕过普通文件 mode。修复为内部
  syscall 操作表，确定性注入 `EACCES`，并保留环境集成证据与包装合同的区别。
- **短写/EINTR 很难稳定制造**：使用脚本化 `pwrite` 序列运行同一生产循环，验证
  指针、长度和 offset 的最小正确推进。
- **backend 构造失败依赖机器**：把 constructor 变成内部操作表，测试只替换动作，
  不复制 fallback 策略。
- **`SIGKILL` kill 点可能竞态**：Stage 在第二块通过 pipe 精确通知并阻塞；父进程还
  检查第一块已经进入 temp file，双条件满足后才杀。
- **强杀不会执行 RAII**：不错误期待 child 删除 temp；T6 只检查 final name，父测试
  最后清理自己拥有的唯一临时目录。
- **CMake 原来硬依赖 liburing**：用 ENABLE/HAS 两层能力和可选 source/library 列表
  裁剪构建图；再用嵌套独立构建防止“当前机器碰巧已链接”的假阳性。
- **无-liburing CMake 失败日志只显示第一段**：`fail_with_cleanup()` 改读完整 `ARGV`，
  让 stdout/stderr 都能保留。
- **`O_DIRECT` 在不同文件系统合同不同**：采用 pass/skip/fail 三态；生产路径不被
  强行改成 direct I/O。
- **TSan 在 WSL2 runtime 启动失败**：直接运行两个二进制确认同为 exit 66；记录为
  环境阻塞，没有用 ASan 结果替代 TSan。
- **最初担心大文件资源**：先做 1 GiB 安全演练；用户随后明确授权 50 GiB，检查
  865 GiB 可用空间后执行，保存文本 evidence，再逐项删除临时大文件。

## 8. 剩余问题与验收边界

- **T1**：本环境 50 GiB 已通过。
- **T1b**：历史 1-4 GiB 与本次 50 GiB RSS 基本平台化，但缺 200 GiB，未完整通过。
- **T2**：没有受控无界消融模式；生产主路径继续保持有界。
- **T3**：三级 worker 和 overlap/no-overlap harness 存在，但当前记录没有满足稳定
  加速判据。
- **T4**：pipeline 与 serial oracle/有界 verifier 的正确性已有自动覆盖。
- **T5**：单 processor worker，不是多核乱序完成，未实现。
- **T6**：真实 `SIGKILL` 自动覆盖已通过；不等于断电或文件系统损坏测试。
- **T7**：ASan/UBSan 通过；TSan 受当前 WSL2 runtime 阻塞，完整 T7 未通过。
- **T8**：运行期和编译期 fallback 均有自动覆盖。
- **T9**：ByteIncrement 是真实修改型 Stage，但尚无 CPU-heavy 重叠实验。
- `O_DIRECT` 只有当前文件系统的隔离 read contract，生产 pipeline 仍使用 buffered I/O。
- 不支持 CUDA、分布式系统、数据库、Dashboard、生产部署或具体学科文件解析；这些
  仍是明确的项目外边界。

## 9. 下一阶段

计划中的 Stage 0-13 已完成，没有预定义 Stage 14。安全的下一步是：

1. review 当前 Stage 13 diff；
2. 按用户决定创建一个独立 Stage 13 commit；
3. 若以后要补 T1b/T2/T3/T5/T7/T9，先把其中一项定义成新的明确范围，再设计最小
   可运行任务，不能在本总结后悄悄扩张主路径。

建议提交信息：

```bash
git add .
git commit -m "stage13: complete reliability tests and final documentation"
```

本轮没有替用户执行 git commit。

## 10. 面试解释

一句话：

> 我实现了有界 read-process-write 流水线后，没有停在 happy path：用可注入 syscall
> seam 覆盖 EINTR/短写，用真实 SIGKILL 验证原子发布，用独立 no-liburing 构建验证
> fallback，再用 50 GiB 输入证明 192 MiB BufferPool 配置下峰值 RSS 约 156 MiB且
> 输出正确，同时明确保留 TSan、200 GiB 和多核乱序等尚未完成边界。

回答时建议按“架构机制 -> 所有权 -> 失败语义 -> 真实证据 -> 未完成边界”的顺序，
不要只背 API 名称，也不要说“协程天然更快”或“io_uring 永远最快”。完整 1/3 分钟
话术和简历 bullet 见 `docs/interview.md`。

## 11. 防坍缩自检

1. **本阶段是否引入或破坏硬约束？**
   没有。没有整文件 vector、无界队列、reader 无限超前或串行替代最终 pipeline；
   BufferPool、双有界队列、metrics、真实 Stage 和可靠发布全部保留。
2. **当前内存是否仍有界？能否通过 T1？**
   有界上限仍由 `block_size * max_inflight_buffers + fixed overhead` 决定。本环境已用
   50 GiB 实跑通过 T1：192 MiB payload 配置、159,640 KiB process peak RSS、输出
   验证通过。T1b 因缺 200 GiB 不完整。
3. **每一步谁拥有 buffer？**
   pool 始终拥有物理 allocation；reader-local handle 拥有 lease，move 到第一队列，
   再 move 到 processor、第二队列和 writer；writer-local work item 析构后 lease 自动
   回 pool。backend/kernel/worker 只在一次 read 完成前借用地址，不取得所有权。
4. **错误路径会不会 use-after-free/double return？**
   队列失败销毁排队 work item，move-only handle 只允许一个 lease owner；线程 join 和
   backend/task 生命周期保持原顺序。ASan/UBSan 定向测试通过，但 TSan 未能在当前
   环境运行，所以不声称完整线程安全验收。
5. **哪些 acceptance test 尚未完成，何时处理？**
   T1b、T2、T3、T5、完整 T7、T9。计划阶段已结束，它们只会在用户明确开启新的
   验收/设计范围后逐项处理，不能由当前文档或小规模结果自动升级为通过。
