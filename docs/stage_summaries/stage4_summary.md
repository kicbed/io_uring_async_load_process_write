# Stage 4 Summary: Native liburing Basics

Date: 2026-07-14

## 1. What Was Completed

Stage 4 established the native liburing request/completion model without adding
C++20 coroutine integration.

- Added CMake discovery and linking for liburing through pkg-config.
- Added a one-request read demo covering SQE preparation, submission, CQE
  waiting, result decoding, request association, and CQE consumption.
- Added a four-request batch-read demo with fixed buffers and explicit offsets.
- Associated completions with stable `ReadRequest` objects through `user_data`
  instead of relying on completion order.
- Drained all expected batch CQEs and reported results in request order.
- Added a one-request write demo with explicit output offset and exact byte-count
  validation.
- Added normal-input, EOF/empty-input, missing-input, output-content, and
  output-open-failure tests.
- Retried `io_uring_wait_cqe()` after `-EINTR` in all three demos.
- Expanded interview notes with native io_uring semantics and engineering
  boundaries.

Stage 4 does not claim an asynchronous pipeline or an io_uring performance win.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- examples/
|   `-- io_uring/
|       |-- uring_read_once.cpp
|       |-- uring_batch_read.cpp
|       `-- uring_write_demo.cpp
|-- include/
|   `-- util/
|       |-- fd_guard.h
|       `-- file_io.h
|-- src/
|   `-- util/
|       |-- fd_guard.cpp
|       `-- file_io.cpp
|-- tests/
|   |-- stage4_uring_read_once_test.cmake
|   |-- stage4_uring_batch_read_test.cmake
|   `-- stage4_uring_write_demo_test.cmake
`-- docs/
    |-- interview.md
    `-- stage_summaries/
        `-- stage4_summary.md
```

## 3. Added or Modified Files

- `CMakeLists.txt`
  - Discovers liburing, builds the three Stage 4 executables, and registers
    their CTest cases.
- `examples/io_uring/uring_read_once.cpp`
  - Submits and completes one 4096-byte read request.
- `examples/io_uring/uring_batch_read.cpp`
  - Submits four 4096-byte reads and safely associates possibly out-of-order
    completions with stable request records.
- `examples/io_uring/uring_write_demo.cpp`
  - Writes one fixed 36-byte payload at offset zero and rejects incomplete
    completion.
- `tests/stage4_uring_read_once_test.cmake`
  - Covers short input, empty input, and missing input.
- `tests/stage4_uring_batch_read_test.cmake`
  - Covers a boundary-crossing 8193-byte input, empty input, and missing input.
- `tests/stage4_uring_write_demo_test.cmake`
  - Checks the exit code, completion report, exact file contents, and an output
    path whose parent directory is missing.
- `docs/interview.md`
  - Records Stage 4 concepts and interview-ready boundaries.
- `docs/stage_summaries/stage4_summary.md`
  - Records the completed stage, verification, remaining work, and
    anti-collapse audit.

## 4. Purpose of Each Demo and Data Flow

### One read

```text
input fd + 4 KiB vector + ReadRequest
  -> prepare one read SQE -> submit -> wait one CQE
  -> recover ReadRequest through user_data -> decode cqe->res -> seen
```

This proves one complete native request lifecycle and the distinction between a
submission result and an operation completion result.

### Batch read

```text
4 stable ReadRequest objects, each owning a 4 KiB buffer
  -> prepare 4 offset reads -> one submit
  -> collect 4 CQEs in completion order
  -> store each result in its request -> report in request order
```

This proves that request identity and output order must be explicit. It is not
yet read/process/write overlap.

### One write

```text
output fd + 36-byte string + WriteRequest
  -> prepare write SQE at offset 0 -> submit -> wait CQE
  -> verify identity and exact byte count -> seen -> close through RAII
```

This proves basic asynchronous write submission. It is not yet the final
temporary-file, `fsync`, and rename persistence protocol.

## 5. Commands That Currently Work

Configure, build, and test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the Stage 4 demos:

```bash
./build/stage4_uring_read_once <input>
./build/stage4_uring_batch_read <input>
./build/stage4_uring_write_demo <output>
```

The read demos print transferred bytes per request. The write demo writes:

```text
AsyncDataLoader io_uring write demo
```

## 6. Current Test Results

Verified on 2026-07-14 in WSL2 kernel `6.18.33.2-microsoft-standard-WSL2`
with liburing `2.0`:

- Debug CMake configure: passed.
- Debug full build: passed.
- Stage 4.3 focused CTest: 1/1 passed.
- Debug full CTest: 16/16 passed, 0 failed.
- Native io_uring runtime initialization and I/O: passed in this environment.
- Release build: not run during this Stage 4 closeout.
- ASan/TSan: not run; no sanitizer-safety claim is made.
- Official benchmark: not run; no performance claim is made.

## 7. Bugs Encountered and How They Were Fixed

- The first read passed the RAII wrapper where liburing required a raw file
  descriptor.
  - Passed `FdGuard::get()` at the syscall boundary while retaining RAII
    ownership.
- Early batch-read code associated a completion using pointer arithmetic across
  request objects.
  - Replaced it with equality matching against the stable request addresses,
    avoiding undefined pointer-subtraction assumptions.
- Returning on the first bad batch completion could leave other CQEs undrained.
  - Recorded internal/request errors, consumed all expected CQEs, and returned
    failure only after draining them.
- The write operation completed successfully, but an old TODO tail still printed
  an error and returned exit code 2.
  - Removed the stale failure path and returned zero after exact completion
    validation.
- Single-read completion waiting did not retry signal interruption.
  - Added the same `-EINTR` retry loop used by the batch and write demos.

## 8. Remaining Issues

- The examples are learning programs, not a reusable `IOBackend`.
- No coroutine owns or awaits an I/O request yet; that belongs to Stage 5.
- liburing is currently a required build dependency. Runtime/build fallback is a
  Stage 6 responsibility.
- The write demo treats a short write as failure. A general backend must resume
  writing the remaining suffix while preserving offset and ownership.
- There is no CPU processing stage, read/process/write overlap, BufferPool,
  bounded inter-stage queue, backpressure, or metrics yet.
- The write demo does not use temporary output, `fsync`, and atomic rename. That
  reliability mechanism remains required for the final pipeline.
- No benchmark conclusion can be drawn from functional CTest timings.

## 9. Next Stage

Stage 5 integrates native io_uring completions with C++20 coroutines.

The smallest next task should define and test the request-lifetime boundary
before building a full backend:

```text
coroutine calls async read
  -> awaiter prepares SQE and suspends coroutine
  -> event loop receives CQE
  -> request stores cqe->res
  -> event loop resumes the saved coroutine handle
```

Stage 5 must not introduce the Stage 6 backend fallback, Stage 7 pipeline
registration, or Stage 8 BufferPool early.

## 10. Interview Explanation

Short version:

> I first implemented native liburing reads and writes without coroutines. Each
> SQE carries a pointer to stable request state, the kernel borrows buffers until
> completion, and each CQE is decoded and consumed exactly once. The batch demo
> does not assume completion order, so request identity and reporting order stay
> explicit. This stage proves API correctness only, not that io_uring is always
> faster or that the final pipeline already exists.

Useful follow-up points:

- SQEs describe submitted work; CQEs report completed work.
- Submission success does not mean the I/O operation succeeded.
- Negative `cqe->res` is `-errno`; zero read bytes means EOF.
- `user_data` associates a completion with application-owned request state.
- Request metadata and buffers must remain alive until the CQE is consumed.
- Batching enables multiple in-flight operations, but does not by itself provide
  pipeline backpressure or ordered output.

## 11. Anti-Collapse Self-Check

### Did this stage introduce or break any hard constraint?

No final-path constraint was removed or bypassed. The three programs are
explicit native-API learning demos, not presented as the final pipeline. No
whole-file vector, unbounded queue, invented benchmark number, or serial-final
claim was introduced.

### Is memory still bounded?

Yes for every Stage 4 demo:

- one-read owns one 4096-byte vector;
- batch-read owns exactly four 4096-byte buffers plus fixed request metadata;
- one-write owns one fixed 36-byte payload.

Their memory use does not grow with input-file size.

### Can the project pass the large-file bounded-memory acceptance test now?

Not yet. These demos are bounded, but the final large-file pipeline does not
exist. Stage 8 will implement BufferPool and backpressure, Stage 10 will
integrate the end-to-end pipeline, and Stages 11/13 will benchmark and validate
the relevant acceptance/error cases.

### Who owns each buffer at each step?

- One-read: `main()` owns the vector and `ReadRequest`; liburing/kernel borrows
  their addresses from submission until CQE completion.
- Batch-read: `RequestBatch` owns all request records and their inline buffers;
  each SQE borrows the address of one stable element until its CQE is consumed.
- One-write: `main()` owns the payload string and `WriteRequest`; the write SQE
  borrows both addresses until completion.
- File descriptors are owned by `FdGuard`; raw integers are borrowed only at the
  liburing boundary.
- The ring is initialized before submission and released only after all expected
  completions have been consumed.

Stage 4 does not yet transfer buffers between reader, processor, writer, and
BufferPool. That ownership lifecycle belongs to Stages 7-10.

### Which acceptance tests are not ready, and when will they be addressed?

- Large-file bounded memory and backpressure: Stage 8 mechanism, Stage 10
  integration, Stages 11/13 validation.
- Three-stage overlap and ordered processed output: Stage 10.
- Backend fallback: Stage 6.
- Queue depth, inflight, latency, and memory high-water metrics: Stage 9, then
  integrated in Stage 10.
- Crash-safe temporary-file/`fsync`/rename output: final pipeline integration and
  Stage 13 error testing.
- Comparative performance conclusions: Stage 11 only, using measured results.
