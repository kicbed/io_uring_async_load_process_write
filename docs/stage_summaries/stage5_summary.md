# Stage 5 Summary: `io_uring` and C++20 Coroutine Integration

Date: 2026-07-31

## 1. What Was Completed

Stage 5 connected a real `io_uring` read completion to a C++20 coroutine.

- Added a move-only `Task<T>` that owns a coroutine frame, starts it explicitly,
  stores a returned value, preserves an unhandled exception, and destroys the
  frame through RAII.
- Added a non-copyable and non-movable `CompletionRequest` that stores one
  continuation, one CQE-style result, and enforces exactly-once completion.
- Added `UringContext`, which owns an `io_uring`, creates read awaiters, waits
  for one CQE, consumes it, and dispatches it to the associated request.
- Added `ReadAwaiter`, whose `await_suspend()` prepares and submits a read SQE,
  attaches stable request identity through `user_data`, stores the current
  coroutine handle, and returns `true` to keep the coroutine suspended.
- Added result and error propagation from `cqe->res`, through
  `ReadAwaiter::await_resume()`, into `Task<T>::promise_type`, and finally to
  `Task<T>::result()`.
- Added a real-file `async_read_demo` using `co_await context.read_at(...)`.
- Added focused tests for `Task<T>`, the completion/resumption boundary, normal
  reads, EOF, missing input, and a negative CQE result.
- Documented the `co_await read_at` control flow, ownership rules, limitations,
  and interview explanations in `docs/design.md` and `docs/interview.md`.

The functional Stage 5 read API is currently
`UringContext::read_at()`. `UringContext` is deliberately a small teaching
context rather than the final `UringBackend`; the uniform backend interface and
fallback hierarchy belong to Stage 6.

Stage 5 does not claim that coroutines themselves improve performance or that
`io_uring` is always the fastest backend.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- include/
|   `-- coroutine/
|       |-- completion_request.h
|       |-- task.h
|       `-- uring_read_awaiter.h
|-- src/
|   `-- coroutine/
|       `-- uring_read_awaiter.cpp
|-- examples/
|   `-- io_uring/
|       `-- async_read_demo.cpp
|-- tests/
|   |-- stage5_async_read_demo_test.cmake
|   |-- stage5_completion_request_test.cpp
|   `-- stage5_task_test.cpp
`-- docs/
    |-- design.md
    |-- interview.md
    `-- stage_summaries/
        `-- stage5_summary.md
```

## 3. Added or Modified Files

- `CMakeLists.txt`
  - Builds the Stage 5 unit tests and async-read demo and registers their CTest
    cases.
- `include/coroutine/task.h`
  - Defines the coroutine return type, promise, frame ownership, start state,
    result retrieval, and exception propagation.
- `include/coroutine/completion_request.h`
  - Defines the stable request/completion state that connects CQE identity to a
    suspended coroutine.
- `include/coroutine/uring_read_awaiter.h`
  - Declares `UringContext`, `ReadAwaiter`, their public operations, and the
    per-read state that must survive suspension.
- `src/coroutine/uring_read_awaiter.cpp`
  - Initializes and releases the ring, submits a read SQE, consumes one CQE,
    resumes the associated coroutine, and decodes the completion result.
- `examples/io_uring/async_read_demo.cpp`
  - Demonstrates one fixed-size real-file read through
    `co_return co_await context.read_at(...)`.
- `tests/stage5_task_test.cpp`
  - Verifies lazy start, value return, exception propagation, move-only
    ownership, invalid operations, and single result consumption.
- `tests/stage5_completion_request_test.cpp`
  - Verifies continuation registration, suspension, result flow, exactly-once
    resume, duplicate-completion rejection, negative result preservation, and
    no request access after resumption.
- `tests/stage5_async_read_demo_test.cmake`
  - Verifies a normal short read and payload, empty-file EOF, missing input, and
    propagation of a negative CQE result.
- `docs/design.md`
  - Records the complete Stage 5 control flow, ownership graph, error flow, and
    single-threaded learning boundary.
- `docs/interview.md`
  - Adds interview-ready explanations of the awaiter protocol, CQE bridge,
    ownership rules, and engineering limitations.
- `docs/stage_summaries/stage5_summary.md`
  - Records the Stage 5 handoff and anti-collapse audit.

## 4. Purpose and Data Flow

### `Task<T>` lifecycle

```text
call coroutine
  -> allocate coroutine frame and construct promise
  -> get_return_object() returns Task<T>
  -> initial_suspend()
  -> Task::start() resumes the body
  -> final_suspend() retains the frame and result
  -> Task::result() reads the promise
  -> Task destructor destroys the frame
```

### `co_await read_at` lifecycle

```text
async_read()
  -> UringContext::read_at() returns ReadAwaiter
  -> await_ready() returns false
  -> await_suspend(current coroutine handle)
       -> get and prepare read SQE
       -> SQE.user_data = &request_
       -> submit
       -> request_.set_continuation(handle)
       -> return true: remain suspended
  -> UringContext::wait_one()
       -> wait CQE
       -> recover CompletionRequest from user_data
       -> copy cqe->res and mark CQE seen
       -> request->complete(result)
  -> CompletionRequest resumes once
  -> await_resume() returns bytes or throws
  -> co_return stores the result in the promise
```

The kernel never calls `coroutine_handle::resume()`. It reports completion
through a CQE; the user-space completion path performs the resume.

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run only Stage 5 tests:

```bash
ctest --test-dir build -R '^stage5_' --output-on-failure
```

Run the demo manually:

```bash
printf 'abcXYZ' > /tmp/asyncdataloader-stage5-input.bin
./build/stage5_async_read_demo /tmp/asyncdataloader-stage5-input.bin
```

Expected output:

```text
bytes_read=6
payload=abcXYZ
```

Reproduce the focused warning and ASan/UBSan build:

```bash
cmake -S . -B build-stage5-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-stage5-sanitized \
  --target stage5_task_test stage5_completion_request_test stage5_async_read_demo \
  -j
ctest --test-dir build-stage5-sanitized \
  -R '^stage5_' \
  --output-on-failure
```

## 6. Current Test Results

Verified on 2026-07-31:

- Debug CMake configure: passed.
- Debug full build: passed.
- Debug full CTest: 19/19 passed, 0 failed.
- Focused Stage 5 CTest: 3/3 passed, 0 failed.
- Warning-enabled ASan/UBSan Stage 5 build: passed.
- Warning-enabled ASan/UBSan Stage 5 CTest: 3/3 passed, 0 failed.
- TSan: not run; no TSan-safety claim is made.
- Release benchmark: not run for this closeout; no performance claim is made.

## 7. Issues Encountered and How They Were Addressed

- **Resumption can change object lifetimes.**
  - `CompletionRequest::complete()` moves the continuation into a local handle,
    clears the member, and does not access members after `resume()`.
  - A focused test destroys the request during resumed coroutine execution.
- **A CQE and request pointer must not be accessed after resumption.**
  - `UringContext::wait_one()` copies `cqe->res` and calls
    `io_uring_cqe_seen()` before it resumes the coroutine.
- **The address stored in `user_data` must remain stable.**
  - `ReadAwaiter` and `CompletionRequest` are non-copyable and non-movable.
- **A completion must not resume a coroutine twice.**
  - `CompletionRequest` rejects a second continuation or completion and clears
    the saved handle before the first resume.
- **Immediate submit/setup failures have no future CQE to resume the operation.**
  - `ReadAwaiter` stores an immediate negative result and returns `false` from
    `await_suspend()`, so `await_resume()` handles the error immediately.
- **Negative CQE results must not be decoded through global `errno`.**
  - `await_resume()` converts `-cqe->res` into `std::system_error`; tests cover a
    negative completion.

## 8. Remaining Issues and Explicit Boundaries

- `UringContext::wait_one()` is a one-completion, calling-thread event loop. It
  is not a concurrent production scheduler.
- The teaching context has no concurrent CQ poller. Its continuation is armed
  after submission but before `main()` begins waiting. A concurrent event loop
  must handle completion/registration races explicitly.
- `UringContext` is not yet the final `UringBackend`.
- liburing is still a required build dependency; unavailable-kernel and
  unavailable-library fallback belongs to Stage 6.
- There is no uniform `IOBackend`, `ThreadPoolBackend`, `SyncBackend`, backend
  factory, or fallback policy yet.
- Coroutine-integrated asynchronous write is not implemented.
- There is no processing-stage registration, end-to-end read/process/write
  overlap, BufferPool, bounded inter-stage queue, backpressure, metrics, or
  crash-safe final output yet.
- The current demo has one fixed 4 KiB buffer and one in-flight read. It proves
  the bridge, not throughput or large-file pipeline behavior.

## 9. Next Stage

Stage 6 is **IOBackend abstraction and three-level fallback**.

The next stage should design a uniform I/O contract and then implement or adapt:

```text
BackendFactory
  -> UringBackend
  -> ThreadPoolBackend
  -> SyncBackend
```

It must define how backend availability and operation errors differ, how
fallback is selected, and how all backends preserve the same result and buffer
lifetime contract. Stage 6 must not introduce Stage registration, BufferPool,
metrics, or the final pipeline early.

## 10. Interview Explanation

Short version:

> I implemented a move-only `Task<T>` and a custom `ReadAwaiter` that connects
> `io_uring` completions to C++20 coroutines. `await_suspend()` submits the SQE,
> stores stable request identity in `user_data`, records the current coroutine
> handle, and returns `true` to remain suspended. The user-space CQE path
> recovers the request, consumes the CQE, stores `cqe->res`, and resumes exactly
> once. `await_resume()` returns the byte count or throws, and the Task promise
> carries that result or exception to the caller. The kernel borrows the request
> and buffer addresses, while RAII owners keep them alive until completion.

Useful boundaries:

- `io_uring_submit()` does not suspend a coroutine; the awaiter protocol does.
- The kernel produces CQEs but never calls C++ `resume()`.
- Coroutines organize asynchronous control flow; they do not guarantee a speed
  improvement.
- The current demo proves correctness and lifetime discipline, not a complete
  pipeline or a universal `io_uring` performance advantage.

## 11. Anti-Collapse Self-Check

### Did this stage introduce or break any hard constraint?

No final-path constraint was removed or bypassed. Stage 5 is explicitly a
bounded learning component, not the final pipeline. It adds no whole-file
buffer, unbounded queue, invented benchmark number, or claim that a serial
single-request demo is the final architecture.

### Is memory still bounded?

Yes for the Stage 5 demo:

- one fixed 4096-byte vector;
- one coroutine frame;
- one embedded request;
- one ring with fixed queue depth 8;
- fixed metadata and runtime overhead.

Its memory does not grow with input file size because it reads only one block.

### Can the project pass the 256 MiB / 50 GiB bounded-memory test now?

No. A fixed one-block demo is bounded but is not an end-to-end large-file
pipeline. Stage 8 will introduce BufferPool/configured backpressure, Stage 10
will integrate the streaming pipeline, and Stages 11/13 will measure and verify
large-file behavior.

### Who owns each buffer and request while the read is in flight?

- `main()` owns the vector; the kernel borrows its data pointer.
- `FdGuard` owns the file descriptor; the submitted operation borrows the raw
  integer descriptor.
- `UringContext` owns the ring; `ReadAwaiter` borrows it.
- `Task<T>` owns the coroutine frame.
- The coroutine frame keeps the active `ReadAwaiter` alive.
- `ReadAwaiter` owns its embedded `CompletionRequest`.
- SQE/CQE `user_data` borrows the `CompletionRequest` address.
- `CompletionRequest` borrows the coroutine handle and never destroys the
  coroutine frame.

The owners outlive `wait_one()` and the CQE dispatch in the demo. The
non-movable request state prevents the stored address from changing.

### Which acceptance tests are not ready, and when will they be addressed?

- T1/T1b bounded large-file memory: Stage 8 mechanism, Stage 10 integration,
  Stages 11/13 validation.
- T2 backpressure comparison: Stage 8 implementation and Stage 11 measurement.
- T3 read/process/write overlap: Stage 10 implementation and Stage 11 evidence.
- T4/T5 ordered output correctness: Stage 10 implementation and Stage 13 error
  and correctness tests.
- T6 crash-safe output: final writer integration and Stage 13.
- T7 full ASan/TSan pipeline validation: Stage 13. Stage 5 has only focused
  ASan/UBSan coverage.
- T8 backend fallback: Stage 6.
- T9 real CPU processing stage: Stage 7 registration and Stage 10 integration.
