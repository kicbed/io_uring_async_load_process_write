# Stage 13.2：确定性错误路径测试

日期：2026-08-11

## 1. 本小任务解决什么问题

正常路径容易测试：准备一个文件，读取、处理、写回，再比较输出即可。
错误路径更难，因为下面这些情况不会稳定地自然发生：

- `pread()` 或 `pwrite()` 恰好被信号打断并返回 `EINTR`；
- `pwrite()` 只写出请求数据的一部分；
- `pwrite()` 返回 0，导致循环无法继续前进；
- 已写出一部分数据后，下一次写入返回 `ENOSPC`；
- 当前机器上的 io_uring 构造恰好失败；
- 测试进程以 root 身份运行时，文件权限位未必能稳定制造 `EACCES`。

如果测试依赖这些事件“碰巧发生”，测试会时好时坏。因此 13.2 增加两个很小的
内部依赖注入接点，让测试能够安排系统调用或 backend 构造的返回序列，同时继续
执行生产代码中的重试、短写处理和 fallback 策略。

本小任务没有实现 `O_DIRECT`，没有做 `kill -9` 崩溃测试，也没有改变流水线的
reader/process/writer 拓扑。这些分别属于 13.4、13.3 和已经完成的 Stage 10。

## 2. 它在整个项目中的位置

```text
Stage 2 的 Linux I/O 包装
          |
          v
Stage 6 的 backend 选择策略
          |
          v
Stage 10 的完整三段流水线与可靠落盘
          |
          v
Stage 13.2：让稀有失败分支可以稳定、自动、重复地验证
          |
          v
Stage 13.3/13.4：真实进程崩溃与环境依赖验收
```

13.2 复用了已有实现，不新建另一套业务算法。它补的是“失败时是否仍按设计工作”
的证据，为最终错误处理文档和面试说明提供可重复验证的依据。

## 3. 文件 I/O 的生产流与测试流

### 3.1 生产流

```text
公开函数 write_all_at()
  -> system_file_io_operations()
       选择真正的 system_write_at()
  -> write_all_at_with()
       执行 EINTR 重试、短写续写、错误返回逻辑
  -> ::pwrite()
```

公开 API 没有变化。项目其他代码仍调用 `write_all_at()`，也不会看见测试脚本。

### 3.2 测试流

```text
stage13_file_io_error_paths_test
  -> 准备 scripted_write_at 的返回序列
       第 1 次：-1 / EINTR
       第 2 次：2 字节
       第 3 次：3 字节
  -> write_all_at_with()
       执行同一份生产重试和续写算法
  -> scripted_write_at()
       返回预先安排的结果并记录每次调用参数
```

这里替换的只有最底层 `pwrite()` 动作，真正要验证的循环没有复制到测试中。
因此测试能够发现这些错误：

- `EINTR` 后错误地移动了 buffer 指针；
- 短写后没有把 offset 从 100 推进到 102；
- 短写后仍请求原来的 5 字节，而不是剩余 3 字节；
- 部分成功后丢失已完成的字节数；
- 0 字节写导致无限循环。

## 4. 新增的文件 I/O 类型和函数

| 类型或函数 | 职责 | 为什么这样写 |
|---|---|---|
| `FileIOOperations` | 保存 `open/read/write/fsync` 四个函数指针 | 它是一个很薄的内部 syscall 表，不需要模板、继承或全局可变生产开关 |
| `system_file_io_operations()` | 返回指向真实 Linux 包装函数的操作表 | 生产入口始终显式选择真实系统调用 |
| `open_read_only_with()` | 使用传入的 open 动作并保留失败时的 `errno` | 可稳定验证 `EACCES`，不依赖测试用户身份 |
| `read_at_with()` | 使用传入的 read 动作，并在 `EINTR` 时重试 | 测试可以精确验证重试次数和最终错误 |
| `write_all_at_with()` | 循环写完、重试 `EINTR`、推进指针与 offset | 把单次 `pwrite()` 的不完整结果转换成“全部写完或明确失败”的合同 |
| `fsync_fd_with()` | 在 `EINTR` 时重试，并保留最终同步错误 | 可靠落盘需要区分“被打断”和“真正失败” |
| `system_open_read_only()` 等 | 把统一函数签名转接到 `::open()`、`::pread()`、`::pwrite()`、`::fsync()` | 隔离 Linux API 的参数细节，生产语义保持不变 |

这些名字放在 `detail` 命名空间，表示它们是内部测试接点，不是承诺给普通调用者
长期使用的公共 API。

## 5. `write_all_at_with()` 为什么要这样运行

假设要把 5 字节写到文件 offset 100，而第一次真正写成了 2 字节：

```text
第一次请求：buffer + 0，长度 5，offset 100
第一次结果：成功 2 字节

第二次请求：buffer + 2，长度 3，offset 102
第二次结果：成功 3 字节

最终结果：总共写成 5 字节
```

循环中的三个关键量是：

- `written_count`：累计已经成功写出的字节数；
- `remaining`：本次还需要写多少，即 `byte_count - written_count`；
- `current_offset`：本次文件位置，即 `offset + written_count`。

buffer 指针和文件 offset 必须同步推进。只推进其中一个会造成重复数据或错误位置。
如果返回 `EINTR`，本次没有完成任何字节，所以重试参数必须完全相同。如果返回 0，
循环没有进展，代码把它转换为 `EIO`，避免永久自旋。

## 6. 文件 I/O 自动测试覆盖什么

`stage13_file_io_error_paths` 包含以下确定性案例：

1. open 返回 `EACCES`，包装层保留错误码且不产生有效 fd；
2. read 先返回 `EINTR`，再成功读出 3 字节；
3. read 返回非 `EINTR` 的 `EACCES` 时不重试、不改写错误；
4. write 的 `EINTR` 重试使用完全相同的参数；
5. 2 字节短写后，buffer、剩余长度和 offset 都正确推进；
6. 已写 2 字节后返回 `ENOSPC`，结果同时保留进度和错误码；
7. 0 字节 write 被转换成 `EIO`；
8. fsync 先返回 `EINTR` 后成功；
9. fsync 的最终 `EROFS` 被原样保留。

测试中的重要辅助数据是：

- `ScriptStep`：一次假系统调用的返回值和 `errno`；
- `WriteCall`：记录一次 write 收到的 fd、buffer、长度和 offset；
- `script_steps`：固定上限的返回脚本，不使用无界容器；
- `script_index`：当前消费到哪一步；
- `operation_call_count`：验证是否真的发生了预期次数的重试；
- `write_calls`：验证短写前后参数如何变化。

这些状态只存在于单线程测试进程，不进入生产路径。

## 7. BackendFactory 的生产流与测试流

### 7.1 生产流

```text
BackendFactory::create(config)
  -> system_backend_factory_operations()
       create_uring / create_thread_pool / create_sync
  -> create_backend_with(config, operations)
       执行显式选择或 Auto fallback 策略
  -> 真实 Backend 对象
```

### 7.2 测试流

```text
stage13_backend_factory_failure_test
  -> BackendFactoryOperations{会失败的 uring, ...}
  -> create_backend_with()
       执行同一份生产 fallback 策略
  -> 检查选中了 thread pool / sync，或异常是否向上传播
```

`BackendFactoryOperations` 保存三个 backend 构造函数指针。测试只替换构造动作，
不会复制 `switch` 或 `try/catch` 策略。因此将来有人错误修改 fallback 顺序或异常类型，
测试会直接失败。

## 8. BackendFactory 自动测试覆盖什么

`stage13_backend_factory_failures` 验证四条策略：

1. 显式请求 Uring 时，构造返回 `EPERM` 必须立即失败，不能偷偷换 backend；
2. Auto 下 Uring 抛 `std::system_error` 时，继续尝试 ThreadPool；
3. Auto 下 Uring 和 ThreadPool 都抛 `std::system_error` 时，最终使用 Sync；
4. `std::invalid_argument` 表示配置或程序使用错误，不能被当作环境不可用而隐藏。

这里只捕获 `std::system_error` 很重要：它表达资源、线程或内核能力等运行环境失败。
如果把所有异常都吞掉，错误的参数甚至代码缺陷也可能被伪装成一次“正常 fallback”，
调试会非常困难。

显式 backend 采用 fail-fast 也很重要。用户明确指定 io_uring，通常就是想验证 io_uring；
如果程序静默切到 Sync，命令虽然成功，测试或 benchmark 的结论却会被错误标注。

## 9. 为什么使用函数指针，而不是全局开关

函数指针适合这里，因为接点很小、签名固定、没有运行时多态状态：

- 不需要为四个 syscall 建立一组抽象基类；
- 不需要模板化整个生产调用链；
- 不引入第三方 mocking 框架；
- 每次调用显式传入操作表，不会让一个测试留下全局 hook 污染下一个测试；
- 生产入口明确传入真实操作表，不会意外启用测试行为。

代价是调用方必须保证内部操作表中的函数指针有效。因此这些接点放在 `detail` 中，
只由生产适配层和受控测试使用，不扩大为普通公共 API。

## 10. 本小任务没有证明什么

确定性注入测试证明的是“包装算法面对指定返回值时行为正确”，不等于证明：

- 某个具体文件系统真的返回过 `EACCES`；
- 当前内核真的拒绝过 io_uring；
- `O_DIRECT` 的地址、长度和 offset 三重对齐已经端到端可用；
- `kill -9` 时临时文件发布策略已经通过真实进程测试；
- 没有 liburing 开发包时项目可以编译。

这些环境或进程级证据不能由假操作冒充，仍分别留给 13.3 和 13.4。

后续状态（2026-08-11）：13.3 已补真实 `SIGKILL`；13.4 已补当前 ext4 的真实
`O_DIRECT` 对齐合同、无 liburing 独立构建和 50 GiB T1。上面的列表保留的是
13.2 本身没有证明什么，不能倒推成“注入测试已经证明了环境行为”。

## 11. 构建和测试

定向测试命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build \
  --target stage13_file_io_error_paths_test \
           stage13_backend_factory_failure_test -j
ctest --test-dir build -R '^stage13_' --output-on-failure
```

完整回归命令：

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Sanitizer 定向验证使用独立构建目录，避免改变普通 Debug 构建：

```bash
cmake -S . -B build-stage13-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-stage13-asan \
  --target stage13_file_io_error_paths_test \
           stage13_backend_factory_failure_test -j
ctest --test-dir build-stage13-asan -R '^stage13_' --output-on-failure
```

2026-08-11 本轮实际结果：

- 普通 Debug 完整构建通过；
- 普通 Debug CTest `57/57` 通过；
- 两项 Stage 13 测试各连续运行 100 次，全部通过；
- 严格警告 + ASan/UBSan 定向构建通过，没有编译警告；
- ASan/UBSan 下两项 Stage 13 测试 `2/2` 通过。

这只是本轮实际运行结果。TSan、真实 `O_DIRECT` 和大文件验收没有在 13.2 运行，
因此不会被写成已经通过。

## 12. 防坍缩自检

1. **有界内存是否变化？** 没有。生产 BufferPool 和固定队列未修改；测试脚本也是固定数组。
2. **背压是否变化？** 没有。reader、processor、writer 和两条 SPSCQueue 均未修改。
3. **流式处理是否变化？** 没有。文件仍按 block 流动，没有整文件载入或集中保存 block。
4. **buffer 所有权是否变化？** 没有，仍是 `BufferPool -> reader -> processor -> writer -> BufferPool`。
5. **可靠落盘是否被绕过？** 没有。`AtomicOutputFile` 和 commit 顺序未修改。
6. **是否提前实现后续任务？** 没有。此条描述 13.2 完成当时的边界；随后 13.3/13.4
   按各自小任务分别执行了 `SIGKILL`、真实 `O_DIRECT`、大文件和 Sanitizer 验收。

## 13. 面试一句话

> 我把稀有系统错误做成可注入的底层动作，但让测试继续执行生产重试和 fallback
> 策略；这样既能稳定覆盖 `EINTR`、短写和构造失败，又不会把模拟结果冒充真实环境证据。
