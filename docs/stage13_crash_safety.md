# Stage 13.3：真实 SIGKILL 崩溃安全测试

日期：2026-08-11

## 1. 本小任务解决什么问题

普通异常和进程被 `kill -9` 是两类不同的失败：

- 普通 C++ 异常会进行栈展开，局部对象的析构函数有机会关闭 fd、归还 buffer、
  删除未提交的临时文件；
- `SIGKILL` 不能被捕获、忽略或处理，内核会直接终止进程，C++ 析构函数不会运行。

因此，普通异常测试通过，并不能证明进程被强制终止时正式输出仍然安全。
Stage 13.3 使用真正的父子进程和真正的 `SIGKILL`，验证验收项 T6：

```text
运行中途被 kill -9
  -> 已存在的正式输出仍保持原来的完整内容
  -> 原本不存在的正式输出仍然不存在
  -> 半成品只存在于临时文件名下，不会冒充正式结果
```

本小任务没有修改生产代码。它测试的是 Stage 10 已有的
`PipelineExecutor::run_file()` 和 `AtomicOutputFile` 发布边界。

## 2. 它在整体架构中的位置

```text
Stage 10
  -> 建立三段流水线
  -> 同目录临时输出
  -> fsync 临时文件
  -> rename 为正式文件
  -> fsync 父目录

Stage 13.2
  -> 确定性验证 syscall 错误和 backend 构造失败

Stage 13.3
  -> 在 commit 前真实杀死整个流水线进程
  -> 验证正式文件名没有暴露半成品

Stage 13.4
  -> 大文件、TSan、O_DIRECT、无 liburing 等环境验收
```

13.3 给已有的可靠发布设计补上进程级证据，不增加新的数据处理功能。

## 3. 生产发布流程为什么能保护正式文件

`PipelineExecutor::run_file()` 的流程是：

```text
打开输入文件
  -> AtomicOutputFile 在输出目录创建 .output.bin.tmp.XXXXXX
  -> reader/process/writer 只操作这个临时文件
  -> 所有 worker 正常结束
  -> fsync(临时文件)
  -> rename(临时文件, 正式文件)
  -> fsync(父目录)
```

关键边界是 `rename()`。在它执行之前，旧正式文件和临时文件是两个不同的目录项：

```text
output.bin                 -> 旧的完整内容，或者不存在
.output.bin.tmp.ABC123     -> 正在增长的半成品
```

只有流水线完整成功并且临时文件 `fsync` 成功后，程序才会用一次同文件系统的
`rename()` 发布新结果。因此 commit 前终止进程，不会把临时文件改名为正式文件。

## 4. 测试为什么需要两个进程

不能让测试进程自己执行 `kill(getpid(), SIGKILL)`，否则 CTest 只能看到整个测试
突然死亡，没有父进程继续检查文件。

本测试分为两个角色：

```text
父测试进程
  -> 准备输入和旧输出
  -> fork + exec 同一个测试程序的 --child 模式
  -> 等待子进程到达受控位置
  -> 发送 SIGKILL
  -> waitpid 检查真实退出原因
  -> 检查正式输出和临时输出

子流水线进程
  -> 使用 SyncBackend
  -> 创建真实 PipelineExecutor
  -> 启动 reader / processor / writer 三个 worker
  -> 写入真实 AtomicOutputFile 临时文件
  -> 在第二个 block 的 processing 中阻塞，等待被杀
```

父进程能够活下来，因此可以在子进程死亡后充当正确性裁判。

## 5. 为什么使用 `fork + exec`

父进程先调用 `fork()` 创建子进程，再让子进程通过 `/proc/self/exe` 执行同一个
测试二进制的 `--child` 模式。

这样做有三个好处：

1. 子进程是一个全新初始化的 C++ 程序，再由它创建流水线线程；
2. 父进程只负责协调和检查，子进程只负责运行真实流水线；
3. 测试不需要额外的测试专用可执行文件或生产故障开关。

`/proc/self/exe` 是 Linux 提供的当前可执行文件入口，符合本项目的 Linux 定位。

## 6. 如何确定“现在可以杀了”

测试不能使用下面这种猜测：

```text
启动子进程 -> sleep(100ms) -> 希望它还没结束 -> kill
```

机器快慢、调度和 sanitizer 都会改变这个时间，使测试不稳定。本测试使用 pipe
建立明确的同步协议。

### 第一步：让第一块能够写出

`BlockingIncrementStage` 对每个 block 执行真实的逐字节 `+1` 变换：

```text
block 0：处理完成 -> 进入 writer -> 写进临时文件
block 1：处理 +1 -> 向 pipe 写入字符 R -> 阻塞在 pause()
block 2：受背压限制，不能让流水线完成
```

第二块进入 Stage 时，第一块已经离开 processor，位于写队列或 writer 中。

### 第二步：父进程等待确定证据

父进程先用 `poll()` 等待 pipe 中的 `R`，再检查同目录临时文件已经达到一个
block 的大小，并逐字节验证它正是处理后的第一块。

此时同时满足：

- 真实 CPU Stage 已运行；
- writer 已经真实写出部分结果；
- 第二块仍卡在 processor 中；
- `run_file()` 不可能到达 `commit()`。

父进程随后调用 `kill(child_pid, SIGKILL)`。

## 7. `waitpid()` 验证什么

只调用 `kill()` 还不够，因为测试必须确认目标进程真的按照预期死亡。

父进程用阻塞式 `waitpid()` 回收子进程，并检查：

```text
WIFSIGNALED(status) == true
WTERMSIG(status) == SIGKILL
```

这排除了以下假通过：

- 子进程提前正常退出；
- 子进程因程序错误收到其他信号；
- 父进程检查文件时子进程其实还活着；
- 测试结束后留下僵尸进程。

## 8. 两个自动测试场景

同一个 CTest 用例内部运行两个独立子进程。

### 场景 A：正式输出已经存在

父进程先创建一个包含固定旧数据的 `output.bin`。子进程的临时文件已经包含第一块
新数据后被杀。测试验证 `output.bin` 在发送信号前后都与旧数据逐字节一致。

这证明半成品不会覆盖调用者上一版可用结果。

### 场景 B：正式输出原本不存在

父进程不创建 `output.bin`。子进程写出一个 block 的临时数据后被杀。测试验证
发送信号前后 `output.bin` 都不存在。

这证明程序不会提前创建一个看起来像成功结果的空文件或半截文件。

两个场景还都会验证：

- 输入文件没有被修改；
- 临时文件恰好只有一个 block；
- 临时文件内容已经通过真实修改型 Stage；
- 子进程退出原因确实是 `SIGKILL`。

## 9. 新增类、函数和重要变量

| 名称 | 职责 | 设计原因 |
|---|---|---|
| `TempDirectory` | 用 `mkdtemp()` 创建每个场景独占的测试目录，父进程最后递归清理 | 防止测试文件互相冲突，也清理故意留下的孤儿临时文件 |
| `ChildProcess` | 保存 child pid；正常路径发送 `SIGKILL` 并 `waitpid`，异常路径析构时也兜底回收 | RAII 防止测试失败时留下运行中的子进程或僵尸进程 |
| `BlockingIncrementStage` | 对 block 做 `+1`，第二块时通知父进程并阻塞 | 同时提供真实 CPU 处理和确定性 crash 窗口 |
| `make_config()` | 固定 64 字节 block、3 个 buffer、容量 1 的队列 | 小输入也走有界 BufferPool、背压和三级线程 |
| `make_input()` | 生成固定三个 block 的输入 | 数据规模固定，测试结果可重复且内存有界 |
| `expected_first_output_block()` | 计算第一块的 `+1` oracle | 判断临时文件不是空壳，而是真正的处理结果 |
| `write_file()` | 完整写入并 fsync 测试 fixture | 父进程开始前准备稳定的输入和旧输出 |
| `file_matches()` | 多读一个字节并逐字节比较 | 同时检查内容和精确文件长度 |
| `parse_ready_fd()` | 解析 `--child` 收到的 pipe fd | 拒绝非法 child 参数 |
| `run_child_mode()` | 构造真实 backend、Stage、PipelineExecutor 并调用 `run_file()` | 子进程执行与生产相同的流水线路径 |
| `wait_for_ready_marker()` | 用 `poll()` 等待 Stage 发出的 `R` | 有超时、可诊断，不靠固定 sleep 猜时机 |
| `find_partial_temporary_file()` | 按 `.output.bin.tmp.` 前缀寻找达到一块大小的临时文件 | 从父进程观察真实 publication 状态 |
| `wait_for_partial_temporary_file()` | 在有限期限内等待第一块对父进程可见 | 将调度速度差异与无限等待分开 |
| `run_crash_case()` | 组织 fixture、子进程、同步、kill、wait 和全部断言 | 两种正式文件初始状态复用同一份严格协议 |

重要常量：

- `kBlockSize = 64`：每块 64 字节；
- `kInputBlockCount = 3`：保证第一块可写、第二块可阻塞、后面还有未完成数据；
- `kWaitTimeout = 5s`：只用于失败超时，不决定正常 kill 时机。

## 10. 为什么 SIGKILL 后允许留下临时文件

`SIGKILL` 不执行 `AtomicOutputFile` 析构函数，所以进程没有机会调用 `unlink()`。
因此一个形如下面的孤儿文件可能留下：

```text
.output.bin.tmp.ABC123
```

这不违反 T6，因为它不是正式输出名。正式消费者只读取 `output.bin`，看到的仍然是
旧的完整版本或“文件不存在”。

测试会先确认孤儿临时文件确实存在，以证明本次杀进程发生在真实半成品状态；然后由
父测试进程的 `TempDirectory` 清理整个测试目录。

生产环境如果需要长期回收孤儿临时文件，应另外定义启动扫描、文件命名、并发实例和
保留期限策略。13.3 不擅自实现这套恢复策略。

## 11. 本测试没有证明什么

- 它是进程 `SIGKILL` 测试，不是断电、磁盘缓存丢失或文件系统损坏模拟；
- kill 点明确位于 processing 尚未结束、rename 尚未发生的位置；
- 它不代替 50/200 GiB 有界内存验收；
- 它不代替 TSan；
- 它不证明真实 `O_DIRECT` 对齐合同；
- 它不证明没有 liburing 开发包时可以构建；
- 它不实现多进程并发写同一个最终路径的锁协议。

这些边界继续归 13.4 或项目明确的后续范围。

后续状态（2026-08-11）：13.4 已补当前 ext4 的 `O_DIRECT` 合同、无 liburing 独立
构建和 50 GiB T1；TSan 仍受 WSL2 runtime 限制，200 GiB 按用户决定未执行。
这不会改变 13.3 的证明范围：进程强杀仍不等于断电或文件系统损坏测试。

## 12. 构建和测试

定向测试：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target stage13_crash_safety_test -j
ctest --test-dir build -R '^stage13_crash_safety$' --output-on-failure
```

重复性检查：

```bash
ctest --test-dir build \
  --repeat until-fail:100 \
  -R '^stage13_crash_safety$' \
  --output-on-failure
```

2026-08-11 本轮实际结果：

- 普通 Debug 定向测试：`1/1` 通过；
- `stage13_crash_safety` 连续运行 100 次：全部通过；
- 普通 Debug 完整 CTest：`58/58` 通过；
- 严格警告 + ASan/UBSan 定向构建：通过，没有编译警告；
- ASan/UBSan 下全部 Stage 13 CTest：`3/3` 通过。

TSan、真实 `O_DIRECT`、断电和大文件验收没有在 13.3 运行，因此不会被写成已经通过。

## 13. 防坍缩自检

1. **有界内存是否变化？** 没有。生产路径未改；测试输入固定为三个 64 字节 block。
2. **背压是否还在？** 是。测试使用 3 个 buffer 和容量 1 的两条 SPSCQueue。
3. **是否仍是三级流水线？** 是。子进程运行真实 reader、processor、writer worker。
4. **是否有真实 processing？** 是。每个进入 Stage 的字节都执行 `+1`。
5. **buffer 所有权是否变化？** 没有，仍为 `BufferPool -> reader -> processor -> writer -> BufferPool`；SIGKILL 时由内核回收整个进程地址空间和 fd。
6. **是否绕过可靠落盘？** 没有。测试正是从进程外观察 `run_file()` 的临时文件与正式文件边界。
7. **是否提前实现 13.4？** 没有。此条描述 13.3 完成当时的边界；13.4 后续独立
   执行了大文件、Sanitizer、`O_DIRECT` 和构建 fallback 验收。

## 14. 面试一句话

> 我用父进程、pipe 同步和真实 `SIGKILL` 在第二块处理中冻结并杀死三段流水线；
> 父进程确认临时文件已有处理后的部分数据，但正式输出仍保持旧完整版本或不存在，
> 从而证明 temp-file + fsync + atomic rename 的发布边界不会暴露半成品。
