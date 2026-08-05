# Stage 8 Summary: BufferPool, PipelineConfig, and Backpressure

Date: 2026-08-05

## 1. What Was Completed

Stage 8 established fixed-capacity buffer ownership and inter-stage handoff.

- Added `PipelineConfig` with validated `block_size`,
  `max_inflight_buffers`, `queue_depth`, and `buffer_alignment`.
- Added an overflow-checked `buffer_pool_bytes()` calculation.
- Added move-only `AlignedBuffer` storage allocated by `posix_memalign()` and
  freed by RAII.
- Added a fixed-count, thread-safe `AlignedBufferPool`.
- Added move-only `BufferHandle` leases that automatically return their pool
  index on destruction.
- Added nonblocking `try_acquire()` and blocking `acquire()` pool operations.
- Added a fixed-capacity, mutex-based `SPSCQueue<T>` ring that supports
  move-only values and blocks on full or empty state.
- Added a deterministic backpressure demo: a one-slot queue blocks the second
  producer push until the consumer removes the first handle.
- Added tests for configuration errors, alignment, move ownership, automatic
  return, pool exhaustion, queue FIFO order, ring wraparound, blocking, and
  integrated handle handoff.
- Documented the exact Stage 8 `O_DIRECT` alignment guarantee and its runtime
  filesystem boundary.
- Updated the README, design notes, and interview notes to describe the Stage
  8 architecture without claiming a complete pipeline or performance result.

Stage 8 does not yet connect real file reads, CPU processing, or writes. The
demo uses two synthetic byte markers only to make ownership and backpressure
observable.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- include/
|   |-- buffer/
|   |   |-- aligned_buffer.h
|   |   |-- aligned_buffer_pool.h
|   |   `-- buffer_handle.h
|   |-- config/
|   |   `-- pipeline_config.h
|   `-- pipeline/
|       `-- spsc_queue.h
|-- src/
|   |-- buffer/
|   |   |-- aligned_buffer.cpp
|   |   |-- aligned_buffer_pool.cpp
|   |   `-- buffer_handle.cpp
|   `-- config/
|       `-- pipeline_config.cpp
|-- examples/
|   `-- stage8_backpressure_demo.cpp
|-- tests/
|   |-- stage8_pipeline_config_test.cpp
|   |-- stage8_aligned_buffer_test.cpp
|   |-- stage8_buffer_handle_test.cpp
|   |-- stage8_buffer_pool_backpressure_test.cpp
|   |-- stage8_spsc_queue_test.cpp
|   `-- stage8_backpressure_demo_test.cmake
`-- docs/
    |-- design.md
    |-- interview.md
    |-- stage8_odirect_alignment.md
    `-- stage_summaries/
        `-- stage8_summary.md
```

Stage 0-7 sources, demos, tests, and summaries remain present and continue to
build. The tree above highlights only the Stage 8 surface.

## 3. Added or Modified Files

- `CMakeLists.txt`
  - Builds the Stage 8 libraries, five C++ tests, backpressure demo, and demo
    output test.
- `README.md`
  - Marks Stage 8 complete, shows how to run the demo, and preserves the
    current end-to-end boundary.
- `include/config/pipeline_config.h`
  - Declares the four capacity/alignment settings and validation API.
- `src/config/pipeline_config.cpp`
  - Rejects zero, invalid alignment, incompatible block size, and size
    multiplication overflow.
- `include/buffer/aligned_buffer.h`
  - Declares one move-only aligned allocation and byte-span accessors.
- `src/buffer/aligned_buffer.cpp`
  - Implements `posix_memalign()`, `free()`, and move transfer.
- `include/buffer/aligned_buffer_pool.h`
  - Declares fixed buffer ownership, free-index tracking, blocking acquisition,
    and the pool/handle lifetime contract.
- `src/buffer/aligned_buffer_pool.cpp`
  - Allocates all buffers at construction, synchronizes acquisition/return,
    waits on exhaustion, and wakes one waiter after return.
- `include/buffer/buffer_handle.h`
  - Declares the move-only RAII lease and borrowed access to pool-owned bytes.
- `src/buffer/buffer_handle.cpp`
  - Transfers lease state with `std::exchange()` and returns the index once.
- `include/pipeline/spsc_queue.h`
  - Implements a header-only fixed-capacity ring for one producer and one
    consumer using optional slots, a mutex, and two condition variables.
- `examples/stage8_backpressure_demo.cpp`
  - Integrates config, pool, handles, and queue in a visible blocking handoff.
- `tests/stage8_pipeline_config_test.cpp`
  - Verifies defaults, valid settings, invalid fields, and overflow handling.
- `tests/stage8_aligned_buffer_test.cpp`
  - Verifies address alignment, byte access, and move semantics.
- `tests/stage8_buffer_handle_test.cpp`
  - Verifies unique lease transfer, moved-from state, and RAII return.
- `tests/stage8_buffer_pool_backpressure_test.cpp`
  - Verifies pool exhaustion waits, return wakes a waiter, and concurrent
    leases never exceed capacity.
- `tests/stage8_spsc_queue_test.cpp`
  - Verifies zero-capacity rejection, FIFO/wraparound, empty/full waits, and
    `BufferHandle` transfer through queue slots.
- `tests/stage8_backpressure_demo_test.cmake`
  - Verifies the demo reports one-slot blocking, FIFO markers, and complete
    pool return.
- `docs/design.md`
  - Records component roles, ownership flow, the two bounded layers, and
    lifecycle limitations.
- `docs/interview.md`
  - Adds concise explanations of move-only leases, ring queues, condition
    variables, memory bounds, and `O_DIRECT` limits.
- `docs/stage8_odirect_alignment.md`
  - Distinguishes buffer-address, request-length, and file-offset alignment;
    explains `STATX_DIOALIGN`, partial-block handling, and fallback boundaries.
- `docs/stage_summaries/stage8_summary.md`
  - Provides this tested handoff and anti-collapse audit.

## 4. Purpose and Data Flow

### Configuration and allocation

```text
PipelineConfig::validate()
  -> calculate fixed payload bytes
  -> AlignedBufferPool constructs N aligned buffers once
  -> no payload allocation per input block
```

### Lease ownership

```text
pool owns physical AlignedBuffer
  -> acquire(): producer owns BufferHandle lease
  -> push(move): queue slot owns lease
  -> pop(): consumer owns lease
  -> handle destruction: pool free list receives the index
```

The handle contains a pool pointer and slot index. Moving the handle does not
copy the underlying block. A moved-from handle is empty and cannot perform a
second return.

### Backpressure demo

```text
pool capacity = 2, queue capacity = 1

producer acquires buffer 0 -> writes marker 1 -> push succeeds
producer acquires buffer 1 -> writes marker 2 -> push waits (queue full)
consumer pops marker 1     -> queue has space  -> producer wakes
producer pushes marker 2   -> consumer pops it
both consumer handles leave scope -> pool available = 2
```

Pool capacity bounds physical bytes. Queue capacity bounds adjacent-stage
backlog. They solve different problems and are both required.

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the Stage 8 demo and focused tests:

```bash
./build/stage8_backpressure_demo
ctest --test-dir build -R '^stage8_' --output-on-failure
```

Configure, build, and run all Release tests:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

Build Stage 8 with warnings and ASan/UBSan:

```bash
cmake -S . -B build-stage8-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-stage8-sanitized \
  --target stage8_pipeline_config_test \
           stage8_aligned_buffer_test \
           stage8_buffer_handle_test \
           stage8_buffer_pool_backpressure_test \
           stage8_spsc_queue_test \
           stage8_backpressure_demo \
  -j
ctest --test-dir build-stage8-sanitized \
  -R '^stage8_' \
  --output-on-failure
```

## 6. Current Test Results

Verified on 2026-08-05:

- Debug configure and full build: passed.
- Debug full CTest: 34/34 passed, 0 failed.
- Backpressure demo output: reported pool capacity 2, queue capacity 1,
  producer blocking, FIFO markers `1,2`, and two returned buffers.
- Backpressure demo repeated run: 100/100 passed.
- Earlier focused repeated runs in this stage:
  - BufferPool backpressure: 100/100 passed.
  - SPSC queue: 100/100 passed.
- Release configure, full build, and full CTest: 34/34 passed, 0 failed.
- Warning-enabled ASan/UBSan Stage 8 build: passed without compiler warnings.
- Warning-enabled ASan/UBSan focused Stage 8 CTest: 6/6 passed, 0 failed.
- TSan-instrumented BufferPool, queue, and demo targets compiled successfully.
- TSan execution could not start project logic in this environment; the
  runtime exited with `FATAL: ThreadSanitizer: unexpected memory mapping`.
  No TSan-safety claim is made.
- No performance benchmark was run and no Stage 8 performance number is
  claimed.

## 7. Bugs and Risks Encountered

- **Pool-byte multiplication could overflow before allocation.**
  - `PipelineConfig` checks division bounds before multiplying and throws
    `std::overflow_error`.
- **A copied lease could return one pool slot twice.**
  - `BufferHandle` deletes copy construction/assignment and clears the source
    during moves.
- **Waiting without a predicate could mishandle spurious wakeups.**
  - Pool and queue waits use condition-variable predicate overloads and modify
    shared state under their mutexes.
- **A timing check could be misreported as a benchmark.**
  - The demo uses a short timeout only as a blocking-state assertion and
    publishes no latency or throughput result.
- **TSan is unusable in the current container runtime.**
  - Instrumented targets were built, but the runtime memory-mapping failure
    occurs before project code. The limitation is recorded rather than hidden
    or converted into a passing claim.
- **`docs/project_manual.md` remains absent.**
  - The repository contains `docs/project_manual.docx`; Stage 8 followed
    `AGENTS.md`, `docs/anti_collapse_checklist.md`, and
    `docs/staged_prompts.txt`.

## 8. Remaining Issues and Explicit Boundaries

- `SPSCQueue` has no close, end-of-stream, cancellation, or error propagation
  operation. Waiting threads must finish and be joined before destruction.
- `AlignedBufferPool` must outlive every handle and every thread using it.
- The demo contains one producer/consumer handoff, not the final pair of
  read-to-process and process-to-write queues.
- The queue transfers only a handle. Future block metadata still needs a
  logical offset and valid-byte count for EOF and ordered output.
- No backend is connected to the pool yet, and no file is opened with
  `O_DIRECT`.
- Runtime direct-I/O capability/alignment probing and partial-tail policy are
  not implemented.
- Metrics, queue-depth gauges, wait-time measurements, and memory high-water
  marks are Stage 9 work.
- Real read/process/write overlap, ordered output, temporary-file persistence,
  `fsync`, and atomic rename remain Stage 10 and later work.
- The large-file bounded-memory acceptance test has not run.

## 9. Next Stage

Stage 9 is **Metrics Data Structures and Instrumentation**.

Its smallest runnable delivery should define explicit counter, gauge, and
latency/histogram ownership, then instrument one bounded component without
building the complete Stage 10 pipeline early. Queue depth, in-flight buffers,
memory high watermark, and wait behavior must become observable without
removing or bypassing backpressure.

## 10. How to Explain This Stage in Interviews

Short version:

> I bounded payload memory with a fixed aligned BufferPool and represented
> each active slot as a move-only RAII lease. One producer and one consumer
> transfer those leases through a fixed-capacity ring. Pool exhaustion and
> queue exhaustion wait on condition variables, so downstream slowdown
> propagates upstream without copying blocks or allowing memory to grow with
> input size. A deterministic demo verifies blocking, FIFO handoff, and
> automatic return; the complete read-process-write pipeline remains a later
> integration stage.

Important distinctions:

- The pool owns bytes; a handle owns a temporary lease; a queue slot may
  temporarily own that handle.
- `max_inflight_buffers` bounds physical payload memory; `queue_depth` bounds
  stage backlog.
- SPSC is the usage contract. The first queue implementation is mutex-based,
  not a lock-free performance claim.
- Aligned allocation prepares for direct I/O but does not prove that
  `O_DIRECT` is supported, correctly aligned for a particular file, or faster.
- Condition variables provide coordination; they do not create asynchronous
  I/O or three-stage overlap by themselves.

## 11. Anti-Collapse Self-Check

### Did this stage introduce or break any hard constraint?

No. Stage 8 strengthens H1, H3, and H5 by adding fixed payload allocation,
bounded handoff, upstream waiting, move-only lease ownership, and RAII return.
It adds no whole-file read, all-block vector, unbounded main-path queue,
invented benchmark number, or claim that this two-thread demo is the final
pipeline.

H2, H4, H6, H7, and H8 have not been discarded. Stage 7 already supplies a
real normalization transform; later stages still need to integrate it with
three-stage overlap, reliable output, error handling, and metrics.

### Is memory still bounded?

Yes for the Stage 8 components. Pool payload memory is exactly:

```text
block_size * max_inflight_buffers
```

Queue storage is fixed at construction and contains handles rather than block
copies. Free-index, in-use, mutex, condition-variable, thread-stack, and
allocator bookkeeping are fixed overhead for a fixed configuration. No Stage
8 allocation grows with input-file size.

### Can the project currently pass T1, the bounded 50 GiB test?

Not yet. Stage 8 supplies the required bounded allocation and backpressure
mechanisms, but there is no end-to-end streaming file path to exercise them.
Stage 10 must integrate read/process/write; Stage 11 must measure RSS and scale
invariants; Stage 13 must retain the case as a final acceptance regression.

### Who owns each buffer at each step?

- `AlignedBufferPool` always owns every physical allocation.
- An available slot is represented by its index in the pool free list.
- `acquire()` removes one index and returns a `BufferHandle`; that handle owns
  the exclusive lease.
- `push(std::move(handle))` transfers the lease to one queue slot.
- `pop()` transfers it to the consumer handle.
- The final handle destructor returns the index to the pool and wakes one
  waiter.
- A moved-from handle is empty and cannot double-return the slot.
- The pool must outlive every handle; violating that lifetime contract can
  create a dangling pool pointer and remains forbidden.

### Which acceptance tests are not ready, and when will they be addressed?

- T1/T1b large-file bounded RSS: Stage 10 integration, Stage 11 measurement,
  Stage 13 regression.
- T2 backpressure comparison: Stage 8 proves bounded blocking; any controlled
  unbounded negative comparison belongs only in Stage 11 and must never become
  the main path.
- T3 overlap evidence: Stage 9 metrics, Stage 10 execution, Stage 11 analysis.
- T4/T5 correct and ordered pipeline output: Stage 10 implementation and Stage
  13 error/correctness tests.
- T6 crash-safe output: Stage 10 writer integration and Stage 13 kill/recovery
  validation.
- T7 full memory/thread safety: focused ASan/UBSan passes now; TSan execution
  is blocked by the current environment and full-pipeline sanitizer validation
  remains Stage 13 work.
- T8 end-to-end fallback: backend fallback exists from Stage 6; Stage 10 must
  connect it and Stage 13 must validate the full path.
- T9 real CPU processing under overlap: normalization exists from Stage 7;
  Stage 10 must integrate it and Stage 11 must measure it honestly.
