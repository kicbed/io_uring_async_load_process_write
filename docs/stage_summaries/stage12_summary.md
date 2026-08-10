# Stage 12 Summary: Terminal Output and Optional JSON Metrics

Date: 2026-08-10

## 1. What Was Completed

Stage 12 turned the bounded metrics already produced by the Stage 10 pipeline
into clear human and machine output without changing the data path.

- 12.1 added one small run-report model and reusable terminal/JSON formatters.
- 12.2 added interval-based live progress with separate TTY and redirected-log
  behavior.
- 12.3 added schema-versioned JSON rendering and reliable atomic publication.
- 12.4 integrated `--metrics-json=PATH` into the real demo while retaining the
  existing Stage 11 `key=value` contract.
- 12.5 added focused unit/integration tests, user documentation, design and
  interview notes, and this stage handoff.

The completed reporting flow is:

```text
real bounded reader / processor / writer
  -> update bounded Counter, Gauge, and Histogram objects
  -> live reporter periodically reads written bytes and current pressure
  -> executor joins workers and reliably commits processed output
  -> bounded output verification passes
  -> take one final MetricsRegistry::Snapshot
  -> optional reliable metrics.json
  -> human summary + stable key=value records
```

The reporter is an observer. It is not a fourth pipeline stage and never owns
or queues a data buffer.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- examples/
|   `-- preprocess_pipeline_demo.cpp
|-- include/
|   `-- reporting/
|       `-- pipeline_reporter.h
|-- src/
|   `-- reporting/
|       `-- pipeline_reporter.cpp
|-- tests/
|   |-- stage12_pipeline_reporter_test.cpp
|   `-- stage12_preprocess_pipeline_demo_test.cmake
`-- docs/
    |-- design.md
    |-- interview.md
    |-- metrics_output.md
    `-- stage_summaries/
        `-- stage12_summary.md
```

All earlier pipeline, backend, BufferPool, metrics, benchmark, and test files
remain present.

## 3. Added or Modified Files

- `include/reporting/pipeline_reporter.h`
  - Declares the bounded run/progress data, terminal reporter, human and
    machine formatters, and JSON interface.
- `src/reporting/pipeline_reporter.cpp`
  - Implements progress rendering, `std::jthread` lifetime, final summary,
    JSON escaping/schema, complete writes, fsync, rename, and RAII cleanup.
- `examples/preprocess_pipeline_demo.cpp`
  - Adds `--metrics-json`, path validation, live reporter lifetime, one final
    snapshot, readable output, and optional JSON publication.
- `tests/stage12_pipeline_reporter_test.cpp`
  - Tests human/machine formatting, missing metric detection, progress math,
    JSON escaping and fields, reliable replacement, temp cleanup, and invalid
    numeric/path input.
- `tests/stage12_preprocess_pipeline_demo_test.cmake`
  - Runs the real Sync pipeline, verifies transformed bytes and output text,
    parses JSON structurally, checks required values/temp cleanup, and rejects
    JSON/output path collision before processing.
- `CMakeLists.txt`
  - Adds `asyncdataloader_reporting` and the two Stage 12 tests.
- `docs/metrics_output.md`
  - Documents commands, terminal modes, JSON schema, reliability, ownership,
    compatibility, and boundaries.
- `README.md`, `docs/design.md`, `docs/interview.md`
  - Mark Stage 12 complete and explain its use and engineering decisions.
- `docs/stage_summaries/stage12_summary.md`
  - Provides this closure and anti-collapse audit.

## 4. Core Functions, Data Flow, and Important Concepts

### Run context and progress formatting

`PipelineRunReport` stores only the small context needed to describe one run:
paths, backend names, CPU-stage name, configuration, final byte/block totals,
elapsed time, and success flags. Metrics stay in `MetricsRegistry::Snapshot`.
This separation keeps presentation data out of `PipelineExecutor`.

`render_progress_line()` converts one `PipelineProgress` value into a progress
bar, written/input bytes, current throughput, both queue depths, and active
buffer leases. Progress is based on `pipeline.write.bytes`, so it represents
downstream completion instead of reader run-ahead.

### Live reporter

`LiveTerminalReporter` borrows the stable written-byte Counter and three
Gauges. With a positive interval it owns one `std::jthread`:

```text
wait for interval
  -> load metric atomics
  -> render one line
  -> refresh a TTY line or append one log line
  -> repeat until stop requested
```

`stop()` requests cancellation, wakes the timed wait, and joins. The destructor
calls it again safely through RAII. The reporter does not lock queues or use
metrics to coordinate work; pipeline synchronization remains in the pool,
queues, and backend.

### Final output functions

`write_terminal_header()` identifies the workload before execution.
`write_terminal_summary()` explains final throughput, high-water marks,
latencies, commit, and verification after workers stop.

`write_key_value_configuration()` and `write_key_value_result()` preserve the
machine contract used by Stage 11 sweep and profile tools. Human formatting can
therefore improve without making scripts scrape a progress bar.

`StreamStateGuard` is small RAII for `std::ostream`: it restores flags,
precision, and fill characters after a formatter returns.

### JSON and reliable publication

`render_metrics_json()` serializes schema version 1 from the same final
snapshot. It escapes JSON strings, rejects NaN/infinity, and records bounded
Counter, Gauge, and fixed Histogram arrays.

`write_metrics_json_atomic()` renders first, then `AtomicJsonFile` performs:

```text
mkstemp in target directory
  -> write until every byte is complete
  -> retry EINTR and handle short write
  -> fsync temporary file
  -> rename temporary name over target
  -> fsync parent directory
```

`ScopedFd` closes descriptors automatically. If publication fails before
rename, `AtomicJsonFile` removes its temporary name. The CLI separately rejects
empty/directory paths and input/output collisions before starting work.

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run only Stage 12 tests:

```bash
ctest --test-dir build -R '^stage12_' --output-on-failure
```

Run the real demo with both report formats:

```bash
./build/preprocess_pipeline_demo \
  <input.bin> <output.bin> \
  --backend=auto \
  --block-size=1048576 \
  --buffers=8 \
  --queue-depth=4 \
  --report-ms=250 \
  --metrics-json=<existing-directory>/metrics.json
```

Use `--report-ms=0` for benchmark automation that needs final metrics without
periodic terminal samples.

## 6. Current Test Results

Verified on 2026-08-10:

- Debug full CTest: 55/55 passed, 0 failed.
- Release focused Stage 12 CTest: 2/2 passed, 0 failed.
- `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` focused build and CTest:
  2/2 passed, 0 failed.
- ASan/UBSan focused build and CTest: 2/2 passed, 0 failed.
- Stage 11 parameter-sweep and profile-parser compatibility tests: passed.
- Manual Release run over a 64 MiB file: processed output verification passed,
  redirected live records were produced, final values stayed within configured
  queue/buffer bounds, and the 2,374-byte JSON file parsed successfully.

The manual run is a functional demonstration, not a new benchmark claim. No
throughput number from it is promoted as evidence.

## 7. Problems Encountered and How They Were Fixed

- A JSON destination normally does not exist before the run. On this standard
  library, `filesystem::is_directory(path, error)` returned `ENOENT`, which was
  initially treated as a fatal inspection error. Validation now accepts only
  `no_such_file_or_directory` at that point; all other filesystem errors remain
  fatal, and normalized path comparison still rejects JSON/output collision.
- TTY cursor-control output would make redirected logs hard to parse.
  `isatty(stdout)` now selects in-place refresh only for interactive terminals;
  redirected output receives ordinary `live ...\n` records.
- New readable output could have broken Stage 11 scripts. The original
  `key=value` fields were retained and compatibility tests were rerun.
- A formatter could leave `std::fixed` or precision changes on `std::cout`.
  `StreamStateGuard` restores the caller's stream state automatically.
- Direct JSON writes could expose a truncated official file. Publication now
  uses same-directory temp-file/fsync/rename/directory-fsync with RAII cleanup.

## 8. Remaining Issues and Acceptance Boundaries

- T1/T1b formal 50/200 GiB bounded-memory campaigns remain unrun; Stage 11's
  smaller plateau evidence is not relabeled.
- T2 controlled unbounded negative ablation remains unimplemented.
- T3 has an overlap-capable architecture and ablation, but stable speedup is
  not claimed.
- T4 ordered transformed output continues to pass.
- T5 multi-core out-of-order processing is not implemented.
- T6 automated kill/crash persistence testing remains for Stage 13.
- T7 ASan/UBSan passed for Stage 12; TSan remains blocked by this WSL runtime,
  so complete TSan safety is not claimed.
- T8 backend selection/fallback behavior remains covered by earlier tests.
- T9 uses real byte transformation, but a CPU-heavy characterization remains
  unclaimed.
- Terminal/JSON reporting is local only. No HTTP server, dashboard, database,
  remote collection, CUDA, distributed system, or domain format was added.

## 9. Next Stage

Stage 13 is Error tests, documentation, and interview preparation. It owns the
remaining acceptance/error matrix, crash-publication tests, final documentation
review, and interview packaging. Stage 12 does not pre-claim those results.

## 10. Interview Explanation

Short version:

> I added a reporting layer beside the bounded pipeline, not inside its data
> path. One RAII `jthread` samples stable metric objects for TTY-aware progress.
> After workers join and output verification passes, one bounded snapshot feeds
> a readable summary, backward-compatible key/value records, and optional
> schema-versioned JSON. JSON uses temp-file, fsync, atomic rename, and directory
> fsync. The reporter owns no data buffers, so ownership and backpressure stay
> unchanged.

The key boundary is that observability explains what happened; it does not
make coroutines faster, prove io_uring wins, or synchronize the pipeline.

## 11. Anti-Collapse Self-Check

1. **Did this stage introduce or break a hard constraint?**
   No. It added observation and formatting only. It did not replace the
   three-stage pipeline, remove reliable output, or add an unbounded main-path
   queue.
2. **Is memory still bounded?**
   Yes. Data payload remains bounded by configured block size and fixed
   BufferPool capacity. Live reporting holds fixed scalar data. The final
   snapshot/JSON is bounded by at most 64 metrics and fixed histogram buckets,
   independent of input-file size.
3. **Can the project pass the large-file bounded-memory test now?**
   The streaming architecture and Stage 11 measurement command are ready, and
   smaller scale observations plateaued. Formal T1/T1b still require the real
   50/200 GiB runs, so they are not claimed.
4. **Who owns each buffer?**
   Ownership is unchanged:
   `BufferPool -> reader BlockWorkItem -> read/process queue -> processor ->
   process/write queue -> writer -> BufferHandle destructor -> BufferPool`.
   The reporter and JSON writer own no `BufferHandle` and see no block bytes.
5. **Which acceptance tests are not ready?**
   T1/T1b large-scale runs, T2 negative ablation, stable T3 evidence, T5
   out-of-order processing, T6 kill testing, full T7 TSan on a supported host,
   and T9 CPU-heavy characterization remain unclaimed for Stage 13 or later
   explicit scope.
