# Stage 13.4：环境依赖验收与构建回退

日期：2026-08-11

## 1. 本小任务解决什么问题

13.2 已经用确定性注入验证了稀有 errno 和 backend 构造失败策略，13.3 又用真实
`SIGKILL` 验证了发布边界。13.4 处理剩下不能只靠“假返回值”证明的环境问题：

1. 没有 liburing 时，项目是否真的还能配置、编译、链接和运行；
2. 当前文件系统是否真的执行 `O_DIRECT` 的地址、长度、offset 对齐合同；
3. 50 GiB 输入是否能在约 300 MiB RSS 上限内完整处理并校验；
4. ASan/UBSan 和 TSan 在当前工具链上分别得到什么真实结果。

本任务不增加新的数据 Stage，不把主路径改成 `O_DIRECT`，不实现无界队列对照，
也不为了补齐 T5/T9 提前重写 processor 调度。

## 2. 它在整体架构中的位置

```text
Stage 6 backend 策略
        +
Stage 8 AlignedBuffer / 有界 BufferPool
        +
Stage 10 三 worker + 可靠发布
        +
Stage 11 Release sweep / RSS 记录
        +
Stage 13.2/13.3 错误与崩溃测试
        |
        v
Stage 13.4 真实环境验收
        |
        v
Stage 13.5 最终文档、面试边界和阶段总结
```

可以把 13.4 理解成“验车”：前面阶段已经造好了发动机、刹车和仪表，这一步不再
换架构，而是在缺少某个零件、不同路面、大负载和检查工具下确认它实际会怎样表现。

## 3. 编译期无 liburing 回退

### 3.1 CMake 如何决定能力

新增选项：

```cmake
ASYNCDATALOADER_ENABLE_LIBURING=ON|OFF
```

默认 `ON` 时，CMake 使用 pkg-config 探测 liburing；显式 `OFF` 或探测失败时：

- 不编译 `uring_read_awaiter.cpp` 和 `uring_backend.cpp`；
- 不链接 `PkgConfig::LIBURING`；
- 不创建只属于 Stage 4/5/6 的 Uring 教学目标和测试；
- 定义 `ASYNCDATALOADER_HAS_LIBURING=0`，让工厂知道本二进制没有该能力；
- 其余 Sync、ThreadPool、Pipeline、Metrics、Benchmark 和错误测试继续构建。

这里把两个概念分开：

```text
ENABLE_LIBURING：用户是否允许 CMake 尝试启用
HAS_LIBURING：本次构建最终是否真的拥有该能力
```

这样不会出现“选项打开，但系统没有库，代码却假设一定存在”的矛盾。

### 3.2 BackendFactory 如何运行

`create_uring()` 在有 liburing 的构建中创建 `UringBackend`。在无 liburing 构建中：

1. queue depth 为 0 仍抛 `std::invalid_argument`，因为非法配置不能被 fallback 隐藏；
2. 合法的 Uring 请求抛带 `ENOSYS` 的 `std::system_error`，清楚表达“本构建没实现”；
3. 显式 `BackendKind::Uring` 原样失败；
4. `BackendKind::Auto` 捕获构造期 `system_error`，继续尝试固定线程池，必要时再到 Sync。

数据流如下：

```text
无 liburing build + --backend=auto
  -> BackendFactory::create()
  -> create_uring() throws ENOSYS
  -> Auto catches construction system_error
  -> ThreadPoolBackend
  -> 完整 reader/process/writer pipeline
  -> abc 被 ByteIncrementStage 处理为 bcd

无 liburing build + --backend=uring
  -> create_uring() throws ENOSYS
  -> 不 fallback
  -> 创建正式输出前返回非零状态
```

好处是 benchmark 标签不会撒谎：用户明确要求 Uring 时，程序不会偷偷跑 Sync 再把
结果写成 Uring；只有 Auto 才代表“允许系统帮我选择”。

### 3.3 自动测试如何闭环

`stage13_no_liburing_build_test.cmake` 每次创建独立 `/tmp` 构建树，执行：

```text
configure(-DASYNCDATALOADER_ENABLE_LIBURING=OFF)
  -> build stage6_backend_factory_test + preprocess_pipeline_demo
  -> factory policy test
  -> Auto pipeline: abc -> bcd, selected_backend=thread_pool
  -> explicit Uring: 必须失败且不能创建正式输出
  -> 删除独立测试目录
```

它不是在当前已经链接 liburing 的进程里设置一个布尔变量，而是真正重新走了一遍
CMake、编译器和链接器，因此能发现漏掉的 Uring 源文件引用或链接依赖。

## 4. 真实 `O_DIRECT` 对齐合同

### 4.1 为什么只做隔离合同测试

生产流水线目前使用 buffered `pread/pwrite`。直接把它改成 `O_DIRECT` 会立刻引入
尾块填充、输出截断、运行时对齐查询、文件系统不支持时回退等新设计，超出 Stage 13。

因此这里验证 H7 所需的“对齐错误可被真实环境观察”，但不冒充完整 direct-I/O
backend：

```text
普通方式写 8192 字节 fixture 并 fsync
  -> O_RDONLY | O_DIRECT 重新打开
  -> 项目 AlignedBuffer(8192, 4096)
  -> 对齐读取 4096 字节并比较内容
  -> 分别制造三种错位并期待 EINVAL
```

三种错位是：

| 维度 | 测试值 | 为什么必须单独测 |
|---|---:|---|
| buffer address | `data() + 1` | 内存地址可能不满足 DMA/文件系统对齐 |
| byte count | `4096 - 1` | 地址对齐不代表长度也对齐 |
| file offset | `1` | buffer 和长度正确也不能保证文件位置合法 |

### 4.2 关键函数

- `TemporaryFile`：RAII 持有 fixture fd 和路径，普通退出时自动 `unlink`；
- `prepare_fixture()`：调用项目 `write_all_at()` 和 `fsync_fd()` 准备可靠测试内容；
- `expect_einval()`：只判断一个错位维度，避免三种错误混在一个断言里；
- `skip()`：返回 77，交给 CTest 标记为 **Skipped**；
- `fail()`：表示环境支持该测试，但出现了意外行为或测试自身错误。

结果必须区分：

```text
Pass：当前文件系统接受对齐读，并以 EINVAL 拒绝三种错位
Skip：不支持 O_DIRECT，或本机合同与固定 4096 假设不同
Fail：出现不在合同内的错误或正确数据校验失败
```

当前 `/tmp` 位于 ext4，测试实际为 **Pass**。这不推出其他文件系统也必然如此。

## 5. 50 GiB 有界内存 T1

### 5.1 输入与配置

用户最终决定执行 50 GiB、跳过 200 GiB。输入通过 `fallocate -l 50G` 在同一 ext4
文件系统预分配：

```text
logical bytes:          53,687,091,200
allocated 512B blocks: 104,857,616
du bytes:               53,687,099,392
SHA-256: ab743e145f643a1f6237b7390baf2e6edc71d83997f5bf4ed40d975fb50ba423
```

它不是只有 50 GiB 逻辑长度、几乎不占磁盘块的 sparse 占位。内容是预分配的零数据，
因此仍不能把一次吞吐值推广到其他数据和存储环境。

运行配置：

```text
build: Release
requested backend: Auto
selected backend: io_uring
block size: 8 MiB
max inflight buffers: 24
BufferPool payload cap: 192 MiB
each queue capacity: 8
thread-pool workers configured: 2
samples: 1
RSS limit: 300 MiB
```

### 5.2 整体如何流转

```text
50 GiB input
  -> reader 每次先 acquire 一个池 lease
  -> IOBackend 只读当前 8 MiB block
  -> read/process 有界队列（峰值 8/8）
  -> ByteIncrementStage 原地把 0 变成 1
  -> process/write 有界队列（峰值 8/8）
  -> writer 按原 offset 写临时文件
  -> lease 析构回池（in-flight 峰值 19/24）
  -> 6400 块结束
  -> fsync(temp) -> rename -> fsync(directory)
  -> 两个固定 verification block 流式比较
  -> sweep 校验 metrics 上限并发布 CSV
```

文件变大只增加循环次数，不增加池槽位和队列容量，这正是流式有界设计的核心。

### 5.3 实际结果

```text
blocks written:        6,400
bytes written:         53,687,091,200
verification:          passed
output committed:      sweep 已强制检查为 true
in-flight peak:        19 / 24
read/process peak:     8 / 8
process/write peak:    8 / 8
whole-process RSS:     159,640 KiB
RSS acceptance:        159,640 KiB < 300 MiB，passed
pipeline elapsed:      435,414.277 ms
whole sweep wall time: 523.54 s（包含有界输出验证）
```

因此 T1 在本环境通过。T1b 需要 1/50/200 GiB 同配置比较；已有历史 1-4 GiB 平台
证据和本次 50 GiB 点，但 200 GiB 按用户决定不执行，所以 T1b 仍是“未完整通过”。

原始 CSV、命令、环境和 checksum 位于：
`docs/benchmark_results/2026-08-11-stage13-t1-50g/`。

## 6. Sanitizer 结果

### ASan/UBSan

严格警告加 ASan/UBSan 的四个 Stage 13 C++ 目标编译通过，Stage 13 五项 CTest
实际为 `5/5` 通过。第五项是嵌套无-liburing 构建测试，本身另起普通 Debug 构建。

这说明本轮可执行路径没有被 ASan/UBSan 报告，但不能把定向测试写成所有负载下的
绝对安全证明。

### TSan

两个并发核心测试在 GCC TSan 下可以编译，但直接启动时都在进入测试逻辑前失败：

```text
FATAL: ThreadSanitizer: unexpected memory mapping ...
exit code: 66
```

这发生在 `stage10_queue_lifecycle_test` 和 `stage10_pipeline_executor_test` 的测试主体
之前，是当前 WSL2 与 TSan runtime 的环境冲突。正确结论是：

- 不是 TSan 检测到了某行项目 data race；
- 也不是项目已经通过 TSan；
- 完整 T7 需要在能够正常运行 TSan 的原生 Linux 环境重试。

## 7. 当前环境

```text
OS: Ubuntu 22.04.5 LTS, WSL2
kernel: 6.18.33.2-microsoft-standard-WSL2
arch: x86_64
CPU: Intel Core i7-12800HX, 24 logical CPUs
memory/swap: 15 GiB / 4 GiB
compiler: GCC 11.4.0
CMake/CTest: 3.22.1
liburing: 2.0
workspace and /tmp: /dev/sdd, ext4
```

## 8. 构建与复现命令

默认与 Release 全回归：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

完整无-liburing 构建：

```bash
cmake -S . -B build-stage13-no-uring \
  -DCMAKE_BUILD_TYPE=Debug \
  -DASYNCDATALOADER_ENABLE_LIBURING=OFF
cmake --build build-stage13-no-uring -j
ctest --test-dir build-stage13-no-uring --output-on-failure
```

Stage 13 环境测试：

```bash
ctest --test-dir build \
  -R '^(stage13_odirect_contract|stage13_no_liburing_build)$' \
  --output-on-failure
```

50 GiB 的精确命令不在这里重复，以免手工版本漂移；以 evidence bundle 的
`commands.md` 为准。

## 9. 防坍缩自检

1. **是否把文件整体读入内存？** 没有。50 GiB 输入按 8 MiB block 流动，验证也只
   复用两个固定 block。
2. **是否引入无界队列？** 没有。两条队列容量均为 8，实际峰值恰好为 8。
3. **reader 能否无限超前？** 不能。队列满或 24 个 lease 用完都会阻塞。
4. **buffer 所有权是否变化？** 没有，仍为
   `BufferPool -> reader -> processor -> writer -> BufferPool`。
5. **是否删除真实 processing？** 没有。50 GiB 每个有效字节都执行 `+1 mod 256`，
   随后逐块验证。
6. **是否绕过可靠落盘和 metrics？** 没有。运行经过 temp/fsync/rename/dir-fsync，
   sweep 依赖正式 metrics 判定队列、in-flight、RSS 和 verification。
7. **是否夸大环境结论？** 没有。T1 只绑定本次环境；T1b、完整 T7 和其他缺口仍
   明确保留。

## 10. 面试一句话

> 我把 fallback 分成运行期和编译期两层验证：无 liburing 时完整 pipeline 仍能构建
> 并由 Auto 选择线程池；同时用真实 ext4 `O_DIRECT` 测试三维对齐错误，并让不支持的
> 文件系统报告 skip。最终 50 GiB 流式验收在 192 MiB BufferPool 配置下峰值 RSS
> 159,640 KiB且逐块校验通过，但 200 GiB 和 WSL2 上无法运行的 TSan 都没有被冒充
> 为通过。
