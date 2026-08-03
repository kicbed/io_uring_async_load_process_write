# Stage 6 Summary: `IOBackend` Abstraction and Three-Level Fallback

Date: 2026-08-03

## 1. What Was Completed

Stage 6 introduced a common read-side backend boundary and three selectable
implementations.

- Added the non-copyable `IOBackend` interface with `name()`, positional
  `read_at()`, and caller-driven `wait_one()` operations.
- Added `SyncBackend`, which performs blocking `pread()` when its lazy Task is
  started and completes without an asynchronous completion event.
- Added `UringBackend`, which adapts the Stage 5 `UringContext` and awaiter to
  the common backend interface.
- Added `ThreadPoolBackend`, which offloads blocking `pread()` calls to a fixed
  number of `std::jthread` workers.
- Bounded the thread-pool work/completion queues with `max_inflight` and
  reports excess submissions as `EAGAIN`.
- Connected thread-pool completions to coroutines through an embedded
  `CompletionRequest`; workers publish results and caller-side `wait_one()`
  resumes the corresponding coroutine.
- Added `BackendConfig`, `BackendKind`, and `BackendFactory`.
- Defined fail-fast explicit selection and Auto fallback:

  ```text
  UringBackend -> ThreadPoolBackend -> SyncBackend
  ```

- Restricted fallback to backend construction-time `std::system_error`;
  operation errors and invalid configuration remain visible.
- Added `backend_fallback_demo`, which performs the same fixed-size real-file
  read through any selected backend and reports requested versus selected
  backend names.
- Added focused tests for the interface contract, every concrete backend,
  bounded thread-pool submission, factory policy, fallback selection, EOF,
  short reads, read errors, and invalid configuration.
- Updated README, design notes, and interview notes with Stage 6 behavior and
  explicit limitations.

The common interface currently covers reads only. Stage 6 does not claim that
coroutines improve performance or that io_uring is always fastest.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- include/
|   |-- backend/
|   |   |-- backend_factory.h
|   |   |-- io_backend.h
|   |   |-- sync_backend.h
|   |   |-- thread_pool_backend.h
|   |   `-- uring_backend.h
|   `-- coroutine/
|       |-- completion_request.h
|       |-- task.h
|       `-- uring_read_awaiter.h
|-- src/
|   |-- backend/
|   |   |-- backend_factory.cpp
|   |   |-- sync_backend.cpp
|   |   |-- thread_pool_backend.cpp
|   |   `-- uring_backend.cpp
|   `-- coroutine/
|       `-- uring_read_awaiter.cpp
|-- examples/
|   `-- backend_fallback_demo.cpp
|-- tests/
|   |-- stage6_backend_factory_test.cpp
|   |-- stage6_backend_fallback_demo_test.cmake
|   |-- stage6_io_backend_contract_test.cpp
|   |-- stage6_sync_backend_test.cpp
|   |-- stage6_thread_pool_backend_test.cpp
|   `-- stage6_uring_backend_test.cpp
`-- docs/
    |-- design.md
    |-- interview.md
    `-- stage_summaries/
        `-- stage6_summary.md
```

## 3. Added or Modified Files

- `CMakeLists.txt`
  - Builds and registers the Stage 6 backend tests and fallback demo.
- `README.md`
  - Updates the current project stage and demonstrates backend selection.
- `include/backend/io_backend.h`
  - Declares the common lazy read and completion contract.
- `include/backend/sync_backend.h`
  - Declares the synchronous concrete strategy.
- `include/backend/uring_backend.h`
  - Declares the io_uring adapter strategy.
- `include/backend/thread_pool_backend.h`
  - Declares the bounded worker-pool strategy and its synchronization state.
- `include/backend/backend_factory.h`
  - Declares backend selection configuration, enum values, and factory API.
- `src/backend/sync_backend.cpp`
  - Implements blocking positional reads with common Task/error semantics.
- `src/backend/uring_backend.cpp`
  - Delegates common reads and completion waits to `UringContext`.
- `src/backend/thread_pool_backend.cpp`
  - Implements request submission, fixed workers, blocking reads, completion
    publication, caller-thread coroutine resumption, and bounded inflight state.
- `src/backend/backend_factory.cpp`
  - Implements explicit construction and Auto fallback policy.
- `examples/backend_fallback_demo.cpp`
  - Parses demo-only backend flags and runs one fixed 4 KiB read through the
    common interface.
- `tests/stage6_io_backend_contract_test.cpp`
  - Verifies abstract interface properties, virtual dispatch, lazy Task
    behavior, and argument preservation.
- `tests/stage6_sync_backend_test.cpp`
  - Verifies synchronous reads, short reads, EOF, errors, and invalid waits.
- `tests/stage6_uring_backend_test.cpp`
  - Verifies the common contract over real SQE/CQE completions.
- `tests/stage6_thread_pool_backend_test.cpp`
  - Verifies worker reads, suspension/resumption, errors, offsets, EOF, and
    `max_inflight` rejection.
- `tests/stage6_backend_factory_test.cpp`
  - Verifies explicit construction, deterministic Auto selection, and invalid
    configuration behavior.
- `tests/stage6_backend_fallback_demo_test.cmake`
  - Verifies selected backend reporting, content, configured fallback, EOF,
    missing input, and invalid CLI options.
- `docs/design.md`
  - Documents the interface, strategies, thread-pool flow, fallback policy,
    ownership, boundedness, and current engineering boundary.
- `docs/interview.md`
  - Adds interview-ready Stage 6 explanations.
- `docs/stage_summaries/stage6_summary.md`
  - Records this handoff and anti-collapse audit.

## 4. Purpose and Data Flow

### Factory selection

```text
BackendConfig
  -> BackendFactory::create()
       -> explicit Uring / ThreadPool / Sync
       -> or Auto: Uring -> ThreadPool -> Sync
  -> std::unique_ptr<IOBackend>
```

### Common read path

```text
IOBackend::read_at(fd, span, offset)
  -> lazy Task<std::size_t>
  -> Task::start()
       -> Sync: blocking pread and immediate completion
       -> Uring: submit SQE and suspend
       -> ThreadPool: bounded enqueue and suspend
  -> if not done: IOBackend::wait_one()
  -> Task::result()
```

### Thread-pool coroutine bridge

```text
awaiter owns ReadRequest
  -> work queue borrows ReadRequest*
  -> worker executes pread()
  -> completion queue borrows ReadRequest*
  -> wait_one() consumes result
  -> CompletionRequest resumes saved coroutine
```

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run only Stage 6 tests:

```bash
ctest --test-dir build -R '^stage6_' --output-on-failure
```

Run the fallback demo:

```bash
./build/stage6_backend_fallback_demo \
  /path/to/input \
  --backend=auto

./build/stage6_backend_fallback_demo \
  /path/to/input \
  --backend=auto \
  --disable-uring

./build/stage6_backend_fallback_demo \
  /path/to/input \
  --backend=auto \
  --disable-uring \
  --disable-thread-pool
```

Build and test Stage 6 with warnings and ASan/UBSan:

```bash
cmake -S . -B build-stage6-final-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-stage6-final-sanitized \
  --target stage6_io_backend_contract_test \
           stage6_sync_backend_test \
           stage6_uring_backend_test \
           stage6_thread_pool_backend_test \
           stage6_backend_factory_test \
           stage6_backend_fallback_demo \
  -j
ctest --test-dir build-stage6-final-sanitized \
  -R '^stage6_' \
  --output-on-failure
```

## 6. Current Test Results

Verified on 2026-08-03:

- Debug configure and full build: passed.
- Debug full CTest: 25/25 passed, 0 failed.
- Warning-enabled ASan/UBSan Stage 6 CTest: 6/6 passed, 0 failed.
- Debug `stage6_thread_pool_backend` repeated 100 times: passed every run.
- TSan binary construction was attempted during Stage 6 development, but this
  WSL environment terminated before project code with
  `ThreadSanitizer: unexpected memory mapping`; no TSan-safety claim is made.
- Release benchmark was not run; no Stage 6 performance claim is made.

## 7. Issues Encountered and How They Were Addressed

- **A common interface initially had no concrete overrides.**
  - Each strategy now implements the same `name()`, `read_at()`, and
    `wait_one()` contract.
- **Synchronous and asynchronous completion have different timing.**
  - Callers start the lazy Task, check `done()`, and call `wait_one()` only for
    a suspended operation.
- **Thread-pool requests need stable identity while queued and executing.**
  - `ReadRequest` is embedded in the coroutine awaiter; backend queues borrow
    its stable pointer while the caller-owned Task keeps the frame alive.
- **Worker threads must not run arbitrary continuation code.**
  - Workers publish completions; caller-side `wait_one()` performs resumption.
- **A worker queue must not become an unbounded substitute for backpressure.**
  - `max_inflight` covers every accepted state and excess submissions return
    `EAGAIN`.
- **Resuming a coroutine can change request lifetime immediately.**
  - Completion paths copy the result before `resume()` and never touch request
    state afterward.
- **Fallback must not hide configuration or operation errors.**
  - Explicit selection is fail-fast, Auto catches construction-time
    `std::system_error`, and read errors remain attached to their original Task.
- **Fallback tests must not depend on kernel availability.**
  - Demo-only disable flags deterministically remove Auto candidates.
- **TSan could not start in the current WSL runtime.**
  - The limitation is recorded rather than reported as a passing safety test.

## 8. Remaining Issues and Explicit Boundaries

- `IOBackend` currently abstracts positional reads only; common write support is
  not implemented.
- CMake still requires the liburing development package. Auto mode handles
  runtime ring-initialization failure, but a compile-time no-liburing build is
  not yet supported.
- `wait_one()` is caller-driven and processes one completion; there is no
  general concurrent event loop.
- The caller must keep backend, Task, fd, and buffer alive until completion;
  cancellation of abandoned in-flight Tasks is not implemented.
- Auto fallback reports the selected backend but does not retain structured
  diagnostics for failed candidates.
- There is no Stage registry, processing pipeline, BufferPool, inter-stage
  backpressure, metrics, asynchronous writer, ordered-output coordinator, or
  crash-safe final output yet.
- The demo reads one block and proves interface/fallback correctness, not
  pipeline throughput.

## 9. Next Stage

Stage 7 is **Stage Registration and Pipeline Framework**.

It should introduce the Stage processing boundary, `Pipeline::add_stage`, and
small built-in processing stages without implementing Stage 8 BufferPool or
metrics early. The first runnable step should process one caller-owned block
through registered stages while preserving explicit ownership.

## 10. Interview Explanation

Short version:

> I separated read semantics from I/O mechanism with an `IOBackend` strategy
> interface and a factory. `UringBackend` uses kernel io_uring completion,
> `ThreadPoolBackend` uses a fixed worker set around blocking `pread`, and
> `SyncBackend` is the final fallback. All return the same lazy coroutine Task.
> Auto mode falls back only on backend initialization failure; explicit
> selection and operation errors remain visible. The thread pool bounds all
> accepted requests with `max_inflight`, and caller-side completion resumes the
> corresponding coroutine without running continuations on I/O workers.

Important boundaries:

- Thread-pool `pread()` is blocking I/O offloaded from the caller, not kernel
  asynchronous I/O.
- There is one coroutine frame per operation, not one thread per coroutine.
- Coroutines unify control flow; they are not the source of performance.
- Stage 6 proves selection, fallback, error flow, and lifetime discipline, not
  the final read/process/write pipeline.

## 11. Anti-Collapse Self-Check

### Did this stage introduce or break any hard constraint?

No hard constraint was removed or intentionally bypassed. Stage 6 adds no
whole-file buffer, all-block vector, unbounded backend queue, invented
benchmark value, or claim that the single-block demo is the final design.

### Is memory still bounded?

The Stage 6 demo owns one fixed 4 KiB buffer and one Task. The thread pool has a
fixed worker count and accepts at most `max_inflight` requests across its work,
active, and completion states. Backend-owned queue memory is therefore bounded
by configuration.

This is not yet the final file-level memory proof: callers could create other
objects outside the backend. Stage 8 will make buffer count and inter-stage
queues configuration-bounded for the full path.

### Can the project pass the 256 MiB / 50 GiB bounded-memory test now?

No. Stage 6 has bounded components but no end-to-end large-file pipeline.
Stage 8 will add BufferPool and backpressure, Stage 10 will integrate streaming
read/process/write, and Stages 11/13 will measure and verify the acceptance
case.

### Who owns each buffer at each step?

- The demo owns its fixed `std::array<std::byte, 4096>`.
- `IOBackend::read_at()` receives a non-owning `std::span`.
- Sync `pread()`, the io_uring operation, or a worker thread borrows the data
  address until completion.
- The caller keeps the buffer, fd, backend, and Task alive through
  `wait_one()` and `Task::result()`.
- There is no BufferPool transfer in Stage 6; the final
  `BufferPool -> reader -> processor -> writer -> BufferPool` lifecycle belongs
  to Stages 8-10.

### Which acceptance tests are not ready, and when will they be addressed?

- T1/T1b bounded large-file memory: Stage 8 mechanism, Stage 10 integration,
  Stages 11/13 verification.
- T2 pipeline backpressure comparison: Stage 8 implementation and Stage 11
  measurement.
- T3 three-stage overlap: Stage 10 implementation and Stage 11 evidence.
- T4/T5 ordered pipeline output: Stage 10 implementation and Stage 13 tests.
- T6 crash-safe final output: writer integration and Stage 13 validation.
- T7 full ASan/TSan pipeline run: Stage 13; Stage 6 has focused ASan/UBSan,
  while TSan could not start in the current environment.
- T8 configured io_uring disable fallback: covered in Stage 6. Compile-time
  absence of liburing remains unsupported and must be addressed before final
  error-test closure.
- T9 real CPU processing: Stage 7 registration and Stage 10 integration.
