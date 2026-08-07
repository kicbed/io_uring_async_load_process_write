# Stage 10 Summary: End-to-End Preprocessing Pipeline Demo

Date: 2026-08-07

## 1. What Was Completed

Stage 10 connected the existing backend, Stage, BufferPool, backpressure, and
metrics components into one runnable offline preprocessing pipeline.

- Added move-only `BlockWorkItem` metadata around one `BufferHandle` lease.
- Extended the fixed-capacity `SPSCQueue` with normal close, failure broadcast,
  first-exception preservation, queued-item RAII cleanup, and optional queue
  depth instrumentation.
- Added a single-use `PipelineExecutor` with one reader, one processor, one
  writer, two bounded queues, and one fixed aligned buffer pool.
- Reader uses the selected `IOBackend`; processor invokes the registered
  `Pipeline`; writer completes positional short writes through `write_all_at()`.
- Added read/process/write block and byte Counters, queue and in-flight Gauges,
  total stage latency Histograms, and reused per-Stage automatic timing.
- Added `ByteIncrementStage`, a deterministic real byte transformation used by
  the final demo and output oracle.
- Added `run_file()` publication through same-directory temporary file,
  temporary-file fsync, atomic rename, and parent-directory fsync.
- Added `preprocess_pipeline_demo` with runtime backend selection, Auto fallback
  controls, bounded-memory configuration, periodic terminal status, final
  metric output, and bounded streaming output verification.
- Added six focused Stage 10 tests, including sync and thread-pool execution,
  forced Auto-to-Sync fallback, full and tail blocks, EOF, worker errors,
  queue lifecycle, metrics, exact output, and reliable publication.
- Updated README, design notes, and interview notes without introducing Stage
  11 benchmark claims or Stage 12 JSON/polished terminal presentation.

The resulting primary flow is:

```text
CLI configuration
  -> BackendFactory selects IOBackend
  -> reader acquires one fixed pool buffer and read_at(offset)
  -> bounded read-to-process queue
  -> processor runs ByteIncrementStage over valid bytes
  -> bounded process-to-write queue
  -> writer pwrite()s at the original offset
  -> BufferHandle destructor returns the lease
  -> fsync temporary file -> rename -> fsync parent directory
  -> bounded second-pass output verification
```

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- examples/
|   |-- preprocess_pipeline_demo.cpp
|   `-- stage8_backpressure_demo.cpp
|-- include/
|   `-- pipeline/
|       |-- block_work_item.h
|       |-- builtin_stages.h
|       |-- pipeline.h
|       |-- pipeline_executor.h
|       |-- spsc_queue.h
|       `-- stage.h
|-- src/
|   `-- pipeline/
|       |-- block_work_item.cpp
|       |-- builtin_stages.cpp
|       |-- pipeline.cpp
|       `-- pipeline_executor.cpp
|-- tests/
|   |-- stage10_block_work_item_test.cpp
|   |-- stage10_pipeline_executor_test.cpp
|   |-- stage10_pipeline_metrics_test.cpp
|   |-- stage10_preprocess_pipeline_demo_test.cmake
|   |-- stage10_queue_lifecycle_test.cpp
|   |-- stage10_reliable_output_test.cpp
|   `-- stage8_spsc_queue_test.cpp
`-- docs/
    |-- design.md
    |-- interview.md
    `-- stage_summaries/
        `-- stage10_summary.md
```

All Stage 0-9 sources, examples, tests, and summaries remain present. The tree
above shows only the Stage 10 surface and older files whose queue call sites
had to follow the new close-aware API.

## 3. Added or Modified Files

- `CMakeLists.txt`
  - Adds Stage 10 sources to the pipeline library, builds the final demo and
    five C++ test executables, and registers six CTest cases.
- `include/pipeline/block_work_item.h`,
  `src/pipeline/block_work_item.cpp`
  - Define the move-only block envelope, validate lease and valid-byte bounds,
    and expose only the valid data prefix.
- `include/pipeline/spsc_queue.h`
  - Adds close/fail states, `optional` EOF, exception propagation, queued-value
    destruction, and current/peak depth Gauge updates while retaining a fixed
    ring capacity.
- `include/pipeline/pipeline_executor.h`,
  `src/pipeline/pipeline_executor.cpp`
  - Define runtime metric names, register metric objects, coordinate all three
    worker loops, broadcast errors, stream descriptors, and reliably publish
    a final path.
- `include/pipeline/builtin_stages.h`,
  `src/pipeline/builtin_stages.cpp`
  - Add `ByteIncrementStage`, which changes every byte modulo 256.
- `examples/preprocess_pipeline_demo.cpp`
  - Parses bounded config and backend options, builds the instrumented stage
    chain, reports live/final metrics, runs reliable publication, and performs
    a second bounded verification pass.
- `examples/stage8_backpressure_demo.cpp`
  - Adapts the earlier demo to checked `push()` results and optional `pop()`.
- `tests/stage8_spsc_queue_test.cpp`
  - Adapts the original queue contract checks to close-aware return types.
- `tests/stage10_block_work_item_test.cpp`
  - Verifies validation, move-only type traits, metadata, valid prefix, move
    state, replaced-lease return, and final pool restoration.
- `tests/stage10_queue_lifecycle_test.cpp`
  - Verifies FIFO drain, close wakeups, blocked producer rejection, first
    exception propagation, failure-time RAII return, depth metrics, and
    backpressure.
- `tests/stage10_pipeline_executor_test.cpp`
  - Verifies sync and bounded-thread-pool read paths, three worker stages,
    full/tail block sizes, exact `+1` output, empty input, and processor/writer
    error propagation.
- `tests/stage10_reliable_output_test.cpp`
  - Verifies successful replacement, preservation on processing failure,
    temporary cleanup on rename failure, same-file rejection, missing input,
    and empty-output commit.
- `tests/stage10_pipeline_metrics_test.cpp`
  - Verifies exact event totals, latency sample counts, queue bounds, in-flight
    bounds, final zero current values, output, and single-use executor behavior.
- `tests/stage10_preprocess_pipeline_demo_test.cmake`
  - Runs the actual CLI with Sync and forced Auto fallback, compares exact
    output bytes, and checks publication, verification, backend, and metric
    fields.
- `README.md`
  - Marks Stage 10 complete and documents final demo commands and boundaries.
- `docs/design.md`
  - Documents topology, ownership, shutdown, metrics, publication, verification,
    and honest later-stage boundaries.
- `docs/interview.md`
  - Adds function-flow, ownership, backpressure, fallback, durability, and
    metric explanations plus an interview-ready summary.
- `docs/stage_summaries/stage10_summary.md`
  - Provides this complete handoff and anti-collapse audit.

## 4. Core Classes, Functions, and Ownership

### `BlockWorkItem`

```text
block_index  = logical identity
file_offset  = exact positional output destination
valid_bytes  = useful prefix in a fixed-capacity buffer
BufferHandle = unique right to use one pool slot
```

The metadata is a shipping label, not four copies of the block. Moving the work
item transfers its lease; copying is forbidden. `valid_data()` prevents stale
bytes beyond a short read from being processed or written.

### `SPSCQueue::push/pop/close/fail`

- `push(T value)` owns a waiting item by value and returns `false` only when
  normal close wins before enqueue; a failed queue rethrows its exception.
- `pop()` returns one moved item, or `nullopt` after normal close and drain.
- `close()` marks graceful EOF and preserves queued work.
- `fail(exception_ptr)` stops delivery, keeps the first failure, destroys
  queued work, resets current queue depth, and wakes both directions.

The queue mutex transfers readiness and ownership. Its Gauge only observes
depth; metric atomics are not a synchronization protocol.

### `PipelineExecutor::run()`

`run()` validates descriptors and the stage chain, enforces one run per
executor, creates fixed resources, starts writer then processor then reader,
joins all workers, and rethrows the first exception after quiescence.

Worker flow:

```text
reader_loop
  -> pool.acquire()
  -> complete_read(): read_at -> Task::start -> wait_one while suspended
  -> create BlockWorkItem
  -> read_to_process.push(move(item))

processor_loop
  -> read_to_process.pop()
  -> Pipeline::process(item.valid_data())
  -> process_to_write.push(move(item))

writer_loop
  -> process_to_write.pop()
  -> write_all_at(output_fd, valid_data, file_offset)
  -> item scope ends -> BufferHandle returns pool slot
```

The reader may therefore work on block `i+1`, processor on `i`, and writer on
`i-1`. FIFO handoff plus explicit offsets preserve ordered output in this
single-processor topology.

### `PipelineExecutor::run_file()` and `AtomicOutputFile::commit()`

`run_file()` owns path opening and final publication. It rejects the same
input/output inode, creates a temp file in the destination directory, delegates
streaming to `run()`, and commits only after all workers succeed.

`commit()` performs:

```text
fsync(temp fd) -> rename(temp, final) -> fsync(parent directory fd)
```

Before rename, exceptions leave an existing final path untouched and RAII
unlinks the temp file. If directory fsync fails after rename, publication may
already be visible, so the function reports that durability could not be
confirmed rather than pretending the old final file is still installed.

### `preprocess_pipeline_demo`

The demo's `main()` creates the selected backend, one instrumented Pipeline,
one `ByteIncrementStage`, and one instrumented executor. `LiveReporter` borrows
stable metric references and samples them from a `jthread`; stop-token wakeup
ensures prompt shutdown. `verify_incremented_output()` uses two reusable blocks
after publication and checks every byte without loading whole files.

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run only Stage 10:

```bash
ctest --test-dir build -R '^stage10_' --output-on-failure
```

Run the final demo with automatic backend selection:

```bash
./build/preprocess_pipeline_demo \
  /path/to/input.bin \
  /path/to/output.bin \
  --backend=auto
```

Force the Auto fallback path away from io_uring:

```bash
./build/preprocess_pipeline_demo \
  /path/to/input.bin \
  /path/to/output.bin \
  --backend=auto \
  --disable-uring
```

Inspect all CLI options:

```bash
./build/preprocess_pipeline_demo --help
```

Repeat every Stage 10 test 100 times:

```bash
ctest --test-dir build \
  -R '^stage10_' \
  --repeat until-fail:100 \
  --output-on-failure
```

The warning-enabled ASan/UBSan configuration used for this stage is:

```bash
cmake -S . -B build-stage10-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-stage10-sanitized \
  --target stage10_block_work_item_test \
           stage10_queue_lifecycle_test \
           stage10_pipeline_executor_test \
           stage10_reliable_output_test \
           stage10_pipeline_metrics_test \
           preprocess_pipeline_demo \
  -j
ctest --test-dir build-stage10-sanitized \
  -R '^stage10_' \
  --output-on-failure
```

## 6. Current Test Results

Verified on 2026-08-07:

- Debug configure and complete build: passed.
- Debug full CTest: 47/47 passed, 0 failed.
- Focused Stage 10 CTest: 6/6 passed, 0 failed.
- Release Stage 10 build and CTest: 6/6 passed, 0 failed.
- Every Stage 10 test repeated 100 times: passed without failure.
- Warning-enabled ASan/UBSan Stage 10 build: passed with no compiler warnings.
- Warning-enabled ASan/UBSan Stage 10 CTest: 6/6 passed, 0 failed.
- Manual 32 MiB streaming demo with periodic 10 ms reporting: completed,
  published output, verified every transformed byte, and ended with zero
  current queue depth and zero active buffer leases.
- TSan-instrumented Stage 10 targets compiled, but all executions aborted in
  this WSL runtime before project code with
  `ThreadSanitizer: unexpected memory mapping`. No TSan-safety claim is made.

The 32 MiB run was a functionality/observability check, not a controlled
benchmark. Its measured throughput is intentionally not recorded as a project
performance result.

## 7. Bugs and Risks Encountered

### The original teaching queue had no EOF or error protocol

The Stage 8 queue returned a value unconditionally. A real consumer would wait
forever after the reader reached EOF, and one worker failure could leave the
other workers blocked. Stage 10 changed `pop()` to `optional`, added `close()`
and `fail()`, and updated Stage 8 call sites to handle the richer contract.

### Error cleanup had to release values already inside queues

Merely waking waiters is insufficient: queued `BlockWorkItem`s still own pool
leases. `fail()` now destroys every occupied optional slot under the queue
lock. Each `BufferHandle` destructor returns its lease, so a blocked reader can
wake and then observe the same canonical failure.

### A tail block must not expose the whole buffer capacity

The final read can be shorter than `block_size`. `BlockWorkItem` rejects zero or
oversized valid lengths and exposes only `first(valid_bytes)`. Tests cover two
full blocks followed by a three-byte tail.

### Queue metrics could become stale on failure

Increment/decrement only on push/pop would leave current depth nonzero when
failure discarded queued items. Queue failure and destruction explicitly set
current depth to zero while preserving the lifetime high watermark.

### Direct output could damage the previous final file

Writing to the advertised output path was rejected for `run_file()`. A
same-directory `mkstemp` path plus fsync/rename/directory-fsync now separates
work-in-progress from published output. Tests confirm processor and rename
failures clean temporary files and preserve the prior final object when rename
has not occurred.

### TSan cannot execute in this environment

Compilation succeeded, but the runtime failed on its own memory mapping before
any test logic. This is recorded as an environment limitation, not converted
into a passing safety claim.

## 8. Remaining Issues and Honest Boundaries

- T1/T1b large-file RSS acceptance has not been run. Stage 10 supplies the
  bounded streaming topology; Stage 11 must measure 1/50/200 GB behavior under
  a configured memory limit.
- T3 controlled overlap evidence and backend comparisons belong to Stage 11.
  Three distinct workers exist now, but no speedup is claimed.
- T5 multi-core out-of-order processing is not implemented. The current single
  processor preserves FIFO order and still writes explicit offsets.
- T6 automated `kill -9` crash testing remains Stage 13. The required
  temp/fsync/rename mechanism now exists.
- A runnable TSan environment is still required before satisfying the T7 data
  race acceptance claim.
- Full `O_DIRECT` integration and its alignment-error/fallback matrix remain
  later work; aligned pool allocation alone is not presented as direct I/O.
- Stage 12 owns polished terminal presentation and optional JSON. Stage 10 has
  plain periodic text output only.
- The common backend abstraction remains read-side. The writer uses robust
  synchronous positional I/O; no claim is made that writes use io_uring.

## 9. Next Stage

Stage 11: complete benchmark and performance analysis.

The smallest next runnable step should define a controlled benchmark harness
that compares the existing serial baselines and the Stage 10 pipeline using
identical input, transform, cache policy, configuration, and correctness
checks. It must record real results rather than trying to prove io_uring wins.

Stage 11 must not weaken BufferPool bounds, bypass processing, remove reliable
publication for the demo path, or turn one manual timing into a benchmark table.

## 10. Interview Explanation

Short version:

> I connected a runtime-selected read backend to a bounded three-stage
> read-process-write topology. One fixed BufferPool owns all block allocations,
> and move-only work items carry an RAII lease plus offset metadata through two
> bounded queues, so downstream slowness applies backpressure. The CPU stage
> actually transforms every byte, positional writes preserve layout, and the
> final file is published through temp-file/fsync/rename/directory-fsync.
> Counters, queue depths, in-flight buffers, and per-stage latencies come from
> the real path, while performance conclusions remain deferred to controlled
> benchmarks.

Useful follow-up points:

- Coroutines unify backend completion flow; they do not guarantee speed.
- Auto fallback is visible and explicit backend requests are fail-fast.
- `valid_bytes` is essential for tail-block correctness.
- Queue close means drain; queue failure means stop and release ownership.
- Metrics observe atomically but never synchronize buffer handoff.
- The output verification pass is itself bounded and byte-exact.

## 11. Anti-Collapse Self-Check

### 1. Did this stage introduce or break any hard constraint?

No intentional hard constraint was removed or bypassed. Stage 10 introduced
the missing three-stage streaming path, real modifying CPU work, bounded
handoffs, error propagation, reliable publication, fallback selection, and
runtime observability. It did not load the whole file, accumulate all blocks,
use an unbounded main queue, delete the BufferPool, or claim benchmark wins.

### 2. Is memory still bounded?

Yes by construction. Pipeline payload memory is:

```text
block_size * max_inflight_buffers
```

The two queues have fixed slot counts and hold handles rather than block
copies. Metrics have fixed registry/histogram limits. The verification pass
runs after the pipeline pool is destroyed and holds two fixed-size reusable
blocks, so its memory also does not grow with file size.

### 3. Can the project currently pass T1?

The implementation now has the architectural properties needed for a
large-file bounded-memory run: positional streaming, fixed pool, fixed queues,
backpressure, and no per-block accumulation. However, the required 50 GB file,
256 MB limit, and peak-RSS measurement were not executed in Stage 10. T1 is
therefore not claimed as passed; Stage 11 must run and record it.

### 4. Who owns each buffer at each step?

```text
AlignedBufferPool owns every allocation permanently
  -> reader's BufferHandle owns one lease
  -> read queue's BlockWorkItem owns the moved lease
  -> processor's local optional owns it
  -> write queue's BlockWorkItem owns it
  -> writer's local optional owns it
  -> local destruction returns the lease to AlignedBufferPool
```

On failure, queued optionals are destroyed and local stack objects unwind, so
all paths return leases before the pool is destroyed.

### 5. Which acceptance tests are not ready, and when?

- T1/T1b bounded large-file RSS: Stage 11.
- T3 measured overlap and performance comparison: Stage 11.
- T5 multi-core out-of-order process/write correctness: not implemented;
  schedule explicitly before claiming it.
- T6 automated crash/kill-9 safety: Stage 13.
- T7 TSan execution: Stage 13 or another runnable Linux environment.
- Complete T8 error/fallback matrix: core forced fallback passes now; expand in
  Stage 13.
- T9 CPU-heavy workload characterization: the modifying byte transform proves
  a real Stage now; controlled heavy-stage analysis belongs to Stage 11.
