# Stage 9 Summary: Metrics Data Structures and Instrumentation

Date: 2026-08-06

## 1. What Was Completed

Stage 9 established a bounded observability layer and connected it to two
existing runtime boundaries.

- 9.1 added a thread-safe monotonic `Counter`.
- 9.2 added a signed `Gauge` with current value and lifetime high watermark.
- 9.3 added a fixed-capacity `Histogram` with at most 32 finite inclusive
  upper bounds plus one overflow bucket.
- 9.4 added a `MetricsRegistry` that owns at most 64 globally unique named
  metrics and provides stable metric references.
- 9.5 added an RAII `ScopedTimer` and automatic per-stage latency Histograms
  for an instrumented `Pipeline`.
- 9.6 added a unified registry snapshot and instrumented
  `AlignedBufferPool` lease transitions with a current/peak in-flight Gauge.
- Added focused tests for concurrent updates, boundaries, invalid
  registration, exception timing, lease moves/returns, and snapshot copying.
- Updated the README, design notes, and interview notes with exact ownership,
  concurrency, boundedness, and stage-boundary claims.

The stage does not yet produce real read/write latency, final queue depth, or
throughput. Those events require the Stage 10 end-to-end topology. It also does
not add terminal refreshing, JSON output, a dashboard, or benchmark numbers.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- include/
|   |-- buffer/
|   |   `-- aligned_buffer_pool.h
|   |-- metrics/
|   |   |-- counter.h
|   |   |-- gauge.h
|   |   |-- histogram.h
|   |   |-- metrics_registry.h
|   |   `-- scoped_timer.h
|   `-- pipeline/
|       `-- pipeline.h
|-- src/
|   |-- buffer/
|   |   `-- aligned_buffer_pool.cpp
|   |-- metrics/
|   |   |-- counter.cpp
|   |   |-- gauge.cpp
|   |   |-- histogram.cpp
|   |   |-- metrics_registry.cpp
|   |   `-- scoped_timer.cpp
|   `-- pipeline/
|       `-- pipeline.cpp
|-- tests/
|   |-- stage9_counter_test.cpp
|   |-- stage9_gauge_test.cpp
|   |-- stage9_histogram_test.cpp
|   |-- stage9_metrics_registry_test.cpp
|   |-- stage9_scoped_timer_test.cpp
|   |-- stage9_stage_timing_test.cpp
|   `-- stage9_metrics_snapshot_test.cpp
`-- docs/
    |-- design.md
    |-- interview.md
    `-- stage_summaries/
        `-- stage9_summary.md
```

Stage 0-8 sources, demos, tests, and summaries remain present and continue to
build. This tree shows only the Stage 9 surface and the two instrumented Stage
7/8 components.

## 3. Added or Modified Files

- `CMakeLists.txt`
  - Builds the metrics library and seven Stage 9 test executables.
  - Links Pipeline and BufferPool to the metrics library where instrumentation
    is used.
- `include/metrics/counter.h`, `src/metrics/counter.cpp`
  - Define and implement an atomic monotonically increasing event total.
- `include/metrics/gauge.h`, `src/metrics/gauge.cpp`
  - Define and implement atomic current-value updates and CAS-based high-water
    tracking.
- `include/metrics/histogram.h`, `src/metrics/histogram.cpp`
  - Define and implement fixed bucket storage, sample observation, and bounded
    snapshots.
- `include/metrics/metrics_registry.h`,
  `src/metrics/metrics_registry.cpp`
  - Own named metrics, validate global uniqueness and limits, perform typed
    lookup, and copy all metric types into one reporting snapshot.
- `include/metrics/scoped_timer.h`, `src/metrics/scoped_timer.cpp`
  - Measure one monotonic-clock scope and observe its nanoseconds when the
    timer leaves scope.
- `include/pipeline/pipeline.h`, `src/pipeline/pipeline.cpp`
  - Add optional MetricsRegistry injection and one automatically named latency
    Histogram around each registered Stage call.
- `include/buffer/aligned_buffer_pool.h`,
  `src/buffer/aligned_buffer_pool.cpp`
  - Add optional injection of a dedicated zero-initialized in-flight Gauge;
    successful lease acquisition increments it and RAII return decrements it.
- `tests/stage9_counter_test.cpp`
  - Verifies default, weighted, concurrent, and monotonic Counter behavior.
- `tests/stage9_gauge_test.cpp`
  - Verifies set/increment/decrement semantics and concurrent high-watermark
    updates.
- `tests/stage9_histogram_test.cpp`
  - Verifies constructor validation, exact boundary assignment, overflow,
    aggregate totals, and concurrent observations.
- `tests/stage9_metrics_registry_test.cpp`
  - Verifies add/find behavior, global name uniqueness, invalid names, metric
    count limits, and reference stability.
- `tests/stage9_scoped_timer_test.cpp`
  - Verifies one timer produces one positive histogram observation and that
    RAII destruction also records during exception unwinding.
- `tests/stage9_stage_timing_test.cpp`
  - Verifies automatic metric naming, registration order, per-stage samples,
    exception behavior, and the original uninstrumented Pipeline path.
- `tests/stage9_metrics_snapshot_test.cpp`
  - Verifies the complete registry-to-pool-to-RAII-to-snapshot flow, unchanged
    Gauge state on handle moves and failed acquisition, high watermark, copied
    histogram configuration/data, old-snapshot independence, and rejection of
    a reused dirty Gauge.
- `README.md`
  - Marks Stage 9 complete and gives the focused test command without making
    end-to-end or performance claims.
- `docs/design.md`
  - Documents component roles, metric and buffer flows, ownership, atomic
    semantics, fixed limits, and current integration boundaries.
- `docs/interview.md`
  - Adds concise answers about metric choice, dependency injection, entry
    vectors, histogram buckets, RAII timing, snapshots, and boundedness.
- `docs/stage_summaries/stage9_summary.md`
  - Provides this tested handoff and anti-collapse audit.

## 4. Purpose, Ownership, and Data Flow

### Metric registration and snapshot

```text
application constructs MetricsRegistry
  -> add Counter / Gauge / Histogram before workers start
  -> registry uniquely owns each metric
  -> Pipeline and BufferPool borrow stable metric pointers
  -> runtime events update atomic numeric fields
  -> registry.snapshot() copies one bounded reporting view
  -> future terminal/JSON code consumes the copy
```

The registry is not a singleton. Its application-owned lifetime and injected
dependencies are explicit, and tests can create isolated registries.

### One complete 9.6 example

```text
Gauge starts: current=0, high=0

pool.acquire()
  -> one slot changes free to leased
  -> current=1, high=1

move BufferHandle into another variable
  -> same lease, only its C++ owner changes
  -> current=1, high=1

BufferHandle leaves scope
  -> RAII returns the slot
  -> current=0, high=1

registry.snapshot()
  -> copies {name, current=0, high=1}
```

The Gauge observes ownership transitions; it does not own or return a buffer.
The physical allocation remains owned by `AlignedBufferPool`, and exactly one
move-only `BufferHandle` owns each active lease.

### Automatic Stage timing

```text
Pipeline::process(block)
  -> create ScopedTimer for Stage 0 histogram
  -> Stage 0::process(block)
  -> timer destructor records elapsed nanoseconds
  -> repeat for Stage 1
```

If a Stage throws, its timer is destroyed during stack unwinding and records
the failed call's elapsed time. The exception still propagates, and later
stages are not invoked.

### Atomic observation boundary

Counter, Gauge, and Histogram use atomics to avoid metric-level data races.
They deliberately use relaxed ordering because metrics do not publish work or
transfer buffer ownership. Queue and pool synchronization remain responsible
for those semantics.

A live snapshot is non-transactional: an update may occur between two atomic
loads. After workers quiesce it is exact. A copied snapshot remains unchanged
by all later updates.

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the focused Stage 9 tests:

```bash
ctest --test-dir build -R '^stage9_' --output-on-failure
```

Configure the warning-enabled ASan/UBSan build used for Stage 9:

```bash
cmake -S . -B build-stage9-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
```

Build and run the Stage 8/9 sanitizer targets:

```bash
cmake --build build-stage9-sanitized \
  --target stage8_pipeline_config_test \
           stage8_aligned_buffer_test \
           stage8_buffer_handle_test \
           stage8_buffer_pool_backpressure_test \
           stage8_spsc_queue_test \
           stage8_backpressure_demo \
           stage9_counter_test \
           stage9_gauge_test \
           stage9_histogram_test \
           stage9_metrics_registry_test \
           stage9_scoped_timer_test \
           stage9_stage_timing_test \
           stage9_metrics_snapshot_test \
  -j
ctest --test-dir build-stage9-sanitized \
  -R '^(stage8_.*|stage9_.*)$' \
  --output-on-failure
```

## 6. Current Test Results

Verified on 2026-08-06:

- Debug configure and full build: passed.
- Debug full CTest: 41/41 passed, 0 failed.
- Focused Stage 8 plus 9.6 integration CTest: 7/7 passed, 0 failed.
- Warning-enabled ASan/UBSan Stage 8/9 build: passed.
- Warning-enabled ASan/UBSan Stage 8/9 CTest: 13/13 passed, 0 failed.
- Earlier 9.5 repetition checks:
  - ScopedTimer test: 100/100 passed.
  - Automatic Stage timing test: 100/100 passed.
- Release tests were not run for this Stage 9 handoff.
- TSan was not run for Stage 9; no Stage 9 TSan-safety claim is made.
- No performance benchmark was run and no performance number is claimed.

## 7. Bugs, Risks, and Fixes

- **A Gauge high watermark can race between observers.**
  - The implementation uses a compare-exchange loop. A failed CAS refreshes
    the expected value, and the loop retries only while the candidate is still
    larger.
- **Histogram boundary and overflow semantics can be off by one.**
  - Finite upper bounds are inclusive. `lower_bound()` chooses the first bound
    not below the sample; `end()` has index `finite_bucket_count`, deliberately
    selecting the following overflow bucket.
- **Storing metrics directly in a vector could invalidate borrowed
  references.**
  - Registry entries own metrics with `unique_ptr`. Entry-vector reallocation
    moves pointers, not metric objects, so the returned references stay stable.
- **A Stage registration failure could leave an uninstrumented Stage behind.**
  - The Pipeline removes the newly inserted Stage if histogram registration
    throws, then propagates the original error.
- **Manual start/stop timing can miss exception paths.**
  - `ScopedTimer` records from its destructor, including during stack
    unwinding.
- **Counting BufferHandle objects would overcount moves.**
  - The Gauge changes only when a pool slot changes free/leased state. Moving
    a handle performs no metric update, while release through destruction or
    move assignment decrements exactly once for the returned lease.
- **Reusing a nonzero Gauge for a new pool could make its value meaningless.**
  - The instrumented constructor requires a dedicated Gauge whose current
    value and high watermark both start at zero.
- **One focused CTest regex initially selected only the new test.**
  - The selection mistake was noticed from the reported test count and rerun
    with an explicitly grouped expression; all intended Stage 8 plus 9.6 tests
    then passed 7/7.

## 8. Remaining Issues and Explicit Boundaries

- Actual read latency, write latency, processed-block/byte counters, final
  queue-depth gauges, and throughput rates are not connected because the
  end-to-end Stage 10 scheduler does not yet exist.
- The Stage 8 SPSC queue has no close, end-of-stream, cancellation, or
  cross-thread error propagation protocol. Stage 10 must design these before
  starting worker threads.
- The snapshot is non-transactional during concurrent updates; it is not a
  global pause-the-world state capture.
- Terminal refresh formatting and optional JSON output are not implemented.
- There is no final async write interface or complete backend-to-writer path.
- Real read/process/write overlap, logical block metadata, offset-correct
  output, short-I/O loops, temporary output, `fsync`, and rename are not yet
  integrated.
- Large-file RSS, scale invariance, overlap, correctness, crash-safety, and
  throughput acceptance tests have not run.
- `docs/project_manual.md` remains absent; the repository contains and uses
  `docs/project_manual.docx` as confirmed by the user.

## 9. Next Stage

Stage 10 is **End-to-End Preprocessing Pipeline Demo**.

Its smallest first delivery should define the per-block work item and lifecycle
metadata needed to carry one BufferHandle through the real path:

```text
logical block index / file offset / valid byte count / BufferHandle
```

It must then design explicit end-of-stream and error shutdown before connecting
the two bounded handoff queues. Later Stage 10 subtasks can connect reader,
registered CPU stages, writer, ordered offsets, reliable temporary-file output,
and the Stage 9 metrics. Stage 10 must not replace bounded queues with an
all-block vector or present a serial loop as the final pipeline.

## 10. How to Explain This Stage in Interviews

Short version:

> I implemented bounded atomic Counter, Gauge, and fixed-bucket Histogram
> primitives and placed them in an injected registry capped by metric count and
> name length. A unified snapshot decouples live metric ownership from future
> reporting. RAII timers automatically record each CPU stage, including
> exception paths, while BufferPool free-to-leased transitions track current
> and peak in-flight buffers without changing move-only ownership or
> backpressure. The metrics are observable state, not synchronization, and I
> defer throughput claims until the end-to-end pipeline is measured.

Important distinctions:

- Counter is cumulative; Gauge is current plus peak; Histogram is a bounded
  distribution.
- MetricsRegistry owns metrics; Pipeline and BufferPool borrow them.
- The registry is injected, not global, so lifetime and test isolation are
  explicit.
- Metric atomics prevent numeric data races but never publish buffers.
- A live snapshot is useful but not transactionally consistent.
- BufferHandle moves do not create new in-flight buffers.
- Stage 9 creates observability infrastructure; it does not prove speed or
  three-stage overlap.

## 11. Anti-Collapse Self-Check

### Did this stage introduce or break any hard constraint?

No. Stage 9 strengthens H8 by making stage latency and in-flight buffers
observable. It preserves the fixed Stage 8 pool and queue, adds no whole-file
read, all-block vector, unbounded main-path queue, serial-final-pipeline claim,
or invented benchmark number.

The stage does not falsely claim that metrics satisfy H2, H6, or the complete
H8 by themselves. Read/write timing, final queue depth, throughput, overlap,
and reliable output still require real Stage 10 events.

### Is memory still bounded?

Yes for all existing components.

```text
payload memory = block_size * max_inflight_buffers
```

The existing fixed queue still stores only move-only handles. Metric overhead
is capped by 64 names of at most 128 bytes, fixed-size primitive objects, at
most 33 active Histogram buckets each, and a snapshot of only that registered
set. No per-block sample is retained, so metric memory does not grow with file
size.

### Can the project currently pass T1, the bounded 50 GiB test?

Not yet. The BufferPool and metrics needed to enforce and observe the bound now
exist, but no end-to-end streaming file path exercises them. Stage 10 must
integrate the bounded read/process/write flow; Stage 11 must measure RSS and
scale invariance; Stage 13 must retain the final acceptance regression.

### Who owns each buffer and metric at each step?

- `AlignedBufferPool` always owns every physical allocation.
- A successful acquire removes one free index; one `BufferHandle` then owns the
  exclusive lease and the in-flight Gauge increments.
- Moving that handle to a variable or future queue slot transfers only the
  lease; the physical buffer stays in the pool and the Gauge is unchanged.
- The future reader, processor, and writer will successively own the moved
  handle; Stage 9 does not invent those threads early.
- Destruction or replacement of the final handle returns the index through
  RAII and decrements the Gauge.
- The application owns `MetricsRegistry`; it uniquely owns all metric objects.
  Pipeline and BufferPool borrow metric pointers and must not outlive it.
- `MetricsRegistry::Snapshot` owns its copied names and numeric values and can
  outlive subsequent updates, but not usefully represent one frozen instant
  during active updates.

No new buffer pointer is retained by a metric, so Stage 9 introduces no new
buffer use-after-free path.

### Which acceptance tests are not ready, and when will they be addressed?

- T1/T1b: Stage 10 supplies the streaming path; Stage 11 measures large-file
  RSS and scale invariance; Stage 13 keeps the final regression.
- T2: bounded behavior already exists structurally; the controlled ablation
  comparison belongs to Stage 11 and must never become the default main path.
- T3: Stage 10 creates overlap; Stage 11 measures its timeline and timing.
- T4: Stage 10 compares pipeline output with the retained serial baseline;
  Stage 13 expands error/correctness coverage.
- T5: offset-correct parallel completion belongs to Stage 10 and its final
  regression to Stage 13.
- T6: temporary-file, `fsync`, and rename integration belongs to Stage 10;
  crash-safety testing belongs to Stage 13.
- T7: focused Stage 8/9 ASan/UBSan passed 13/13. A runnable TSan environment
  and full-load end-to-end sanitizer tests remain Stage 13 work.
- T8: Stage 6 has backend fallback tests; end-to-end pipeline fallback belongs
  to Stage 10/13.
- T9: Stage 7 supplies real normalization; Stage 10 integrates real CPU work
  into the overlapping path and Stage 11 measures the controlled workload.

## 12. What the User Personally Practiced

The user explicitly authorized Codex to implement Stage 9.1-9.6, so no Stage 9
source code is falsely attributed to the user. During review, the user
personally reasoned through:

- finite Histogram interval and overflow-bucket meaning;
- why an observation updates a bucket in addition to total count/sum;
- why dependency-injected metrics are preferable to a Histogram singleton;
- why the bounded registry uses small entry vectors rather than a hash map;
- how one RAII timer flows through a Stage call.

## 13. Recommended Hands-On Practice

Before Stage 10, useful short reimplementation exercises are:

1. Rewrite `Gauge::update_high_watermark()` from its invariant and explain the
   compare-exchange retry condition.
2. Given bounds `[20, 50]`, manually derive bucket indices for `0`, `20`, `21`,
   `50`, `51`, and `100` before reading the Histogram code.
3. Recreate a small `MetricsRegistry::snapshot()` loop and explain why the copy
   is independent but non-transactional.
4. Trace a BufferHandle through acquire, move construction, move assignment,
   and destruction while writing the expected Gauge current/high values.
5. Add a tiny throwing Stage in a scratch test and predict which latency
   Histograms receive samples before running it.
