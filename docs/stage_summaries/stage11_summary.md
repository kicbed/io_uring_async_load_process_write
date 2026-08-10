# Stage 11 Summary: Complete Benchmark and Performance Analysis

Date: 2026-08-10

## 1. What Was Completed

Stage 11 turned the Stage 10 pipeline into a measurable system without changing
its bounded streaming architecture.

- Added a bounded serial byte-increment oracle and a fair same-topology
  one-buffer-versus-eight-buffer overlap experiment.
- Added a read-backend matrix that keeps processing and writing fixed while
  comparing Sync, ThreadPool, io_uring, and Auto's actual selection.
- Added a separate-process parameter sweep across block size, pool capacity,
  queue capacity, and backend, including whole-process peak RSS.
- Added strict CSV validation, exact-configuration grouping, aggregate
  summaries, dependency-free SVG charts, and evidence-bounded Markdown
  findings.
- Added reproducible `strace -f -c` and `perf stat` capture with exact command,
  stdout/stderr, status, and atomic evidence publication.
- Added six Stage 11 tests for the C++ baseline, both benchmark executables,
  parameter sweep, result analyzer, and profile capture.
- Recorded and archived one real WSL2 reference campaign from the exact Release
  commit `3ba60cdc4c4d3f64c8b5fda6ff417a56209717e3`.
- Recorded 120 verified parameter-matrix samples, a 64 MiB-to-4 GiB bounded-RSS
  scale check, read scans, end-to-end comparisons, backend comparisons, three
  successful strace profiles, and one honest perf-unavailable failure record.
- Updated benchmark documentation and created this Stage 11 handoff.

The Stage 11 measurement flow is:

```text
same Release binary + same input + declared cache/environment policy
  -> run one bounded pipeline configuration in a fresh process
  -> verify transformed output and queue/buffer bounds
  -> record raw timing, metrics, selected backend, and peak RSS
  -> group only identical configurations
  -> render CSV + SVG + Markdown observations
  -> use strace/perf evidence for selected questions
```

No result was changed to make io_uring appear faster. In the 24-group matrix,
Sync, ThreadPool, and io_uring each led at least one exact configuration.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- benchmark/
|   |-- stage11_analyze_results.py
|   |-- stage11_bench_backends.cpp
|   |-- stage11_bench_end_to_end.cpp
|   |-- stage11_capture_profile.py
|   `-- stage11_parameter_sweep.py
|-- include/benchmark/
|   `-- end_to_end_baseline.h
|-- src/benchmark/
|   `-- end_to_end_baseline.cpp
|-- tests/
|   |-- fixtures/stage11_analysis_fixture.csv
|   |-- stage11_analyze_results_test.cmake
|   |-- stage11_bench_backends_test.cmake
|   |-- stage11_bench_end_to_end_test.cmake
|   |-- stage11_capture_profile_test.cmake
|   |-- stage11_end_to_end_baseline_test.cpp
|   `-- stage11_parameter_sweep_test.cmake
`-- docs/
    |-- benchmark.md
    |-- benchmark_results/
    |   `-- 2026-08-10-wsl2-reference/
    |       |-- README.md
    |       |-- commands.md
    |       |-- environment.md
    |       |-- matrix/
    |       |-- profiles/
    |       |-- raw/
    |       `-- rss-scale/
    `-- stage_summaries/
        `-- stage11_summary.md
```

All Stage 0-10 sources, tests, demos, and summaries remain present. No
production pipeline component was removed or replaced by benchmark-only code.

## 3. Added or Modified Files

### Benchmark implementation

- `include/benchmark/end_to_end_baseline.h`,
  `src/benchmark/end_to_end_baseline.cpp`
  - Provide the bounded serial oracle, timed serial and pipeline helpers, and
    bounded byte-for-byte file verification.
- `benchmark/stage11_bench_end_to_end.cpp`
  - Runs serial, one-buffer pipeline, and overlap-capable pipeline samples in a
    rotating order and emits CSV only after all outputs match the oracle.
- `benchmark/stage11_bench_backends.cpp`
  - Runs the same three-stage workload with each available read backend,
    records Auto's actual selection, and reports unavailable explicit backends
    as skipped rather than mislabeled.
- `benchmark/stage11_parameter_sweep.py`
  - Builds the Cartesian run plan, launches one fresh C++ process per sample,
    parses metrics, checks correctness/bounds, records GNU `time` peak RSS, and
    atomically publishes raw CSV.
- `benchmark/stage11_analyze_results.py`
  - Rejects invalid/mixed evidence, groups exact configurations, computes
    descriptive statistics, and atomically publishes summary CSV, SVG charts,
    and a guarded report.
- `benchmark/stage11_capture_profile.py`
  - Runs one exact command under strace or perf and preserves successful or
    failed evidence in a unique, atomically published directory.

### Tests and build

- `CMakeLists.txt`
  - Builds the Stage 11 library/executables and registers six focused tests.
- `tests/stage11_end_to_end_baseline_test.cpp`
  - Tests bounded serial execution, pipeline timing helper, tail/empty files,
    exact verification, invalid paths, and processing/I/O errors.
- `tests/stage11_bench_end_to_end_test.cmake`
  - Runs the real overlap benchmark CLI and validates CSV, metadata, output,
    and argument failures.
- `tests/stage11_bench_backends_test.cmake`
  - Runs the real backend matrix, checks selected-backend labels, output, Auto
    behavior, unavailable backend handling, and invalid arguments.
- `tests/stage11_parameter_sweep_test.cmake`
  - Exercises real subprocess orchestration, peak RSS, correctness/bound
    parsing, and failure cleanup.
- `tests/fixtures/stage11_analysis_fixture.csv`,
  `tests/stage11_analyze_results_test.cmake`
  - Supply deterministic grouped samples and test validation, statistics,
    charts, reports, and atomic publication failures.
- `tests/stage11_capture_profile_test.cmake`
  - Uses a deterministic fake profiler to test success/failure preservation,
    command boundaries, unique labels, and output-directory safety.

### Documentation and recorded evidence

- `README.md`
  - Marks Stage 11 complete and links the reference campaign.
- `docs/benchmark.md`
  - Defines workload families, timing boundaries, cache/environment rules,
    commands, schemas, analysis rules, profiling rules, acceptance commands,
    and interpretation guardrails.
- `docs/benchmark_results/2026-08-10-wsl2-reference/`
  - Archives environment, commands, checksums, raw CSV, generated summaries,
    SVG charts, strace evidence, and the failed perf attempt.
- `docs/stage_summaries/stage11_summary.md`
  - Provides this stage closure and anti-collapse audit.

## 4. Core Functions, Data Flow, and Important Concepts

### Bounded serial oracle

`run_serial_pipeline_reference()` performs one bounded loop:

```text
acquire/reuse one fixed block
  -> pread current offset
  -> Pipeline::process(valid bytes)
  -> complete pwrite at the same offset
  -> repeat until EOF
```

It exists as a correctness oracle and broad performance reference. It is not
the final architecture. `run_timed_serial_byte_increment()` constructs the
same `ByteIncrementStage` used by the Stage 11 pipeline comparison and adds
output fsync inside the timer.

`run_timed_pipeline_byte_increment()` runs the existing Stage 10
`PipelineExecutor` with a caller-selected read backend and the same processing
and durability boundary. This avoids accidentally comparing different work.

`verify_files_equal_bounded()` compares two files with two fixed-size buffers.
It never loads either complete file, so correctness checking remains streaming.

### End-to-end overlap ablation

`stage11_bench_end_to_end` compares:

```text
serial:       one thread, pread -> process -> pwrite
no-overlap:   reader + processor + writer, but one leased pool buffer
overlap:      reader + processor + writer, eight leased pool buffers available
```

The one-buffer pipeline is important: it keeps the same three threads and two
queues as the overlap row, while buffer ownership prevents the next read until
the previous block returns from the writer. Therefore no-overlap versus
overlap is the focused comparison; serial versus pipeline is a broader whole
architecture comparison.

### Backend matrix

`stage11_bench_backends` keeps the pipeline constant and changes only how the
reader completes `read_at()`:

- Sync blocks the reader thread in pread.
- ThreadPool suspends the reader coroutine while a fixed worker performs pread.
- io_uring suspends after SQE submission and resumes after CQE completion.
- Auto is only a selection policy and records the backend it actually chose.

The current reader submits one block and waits before submitting the next.
Consequently this stage measures backend mechanism, not a batch of simultaneous
io_uring reads.

### Parameter sweep and peak RSS

`build_run_plan()` creates the bounded Cartesian product. `run_sample()` starts
one `preprocess_pipeline_demo` process under GNU `time`, parses the demo's
key/value metrics, and rejects failed verification or exceeded bounds.
`write_csv_atomic()` publishes the complete result only after the sweep is
ready, preventing a partial CSV from looking complete.

A fresh process is required because Linux maximum RSS is a process lifetime
high watermark and cannot be reset between configurations.

### Analysis and evidence capture

`summarize()` groups only identical environment/input/backend/block/buffer/
queue/worker configurations. It recomputes throughput from total bytes and
total time instead of averaging incompatible rates. `render_report()` phrases
results as observations and keeps Auto out of distinct-mechanism rankings.

`capture()` creates a temporary evidence directory, executes one exact command
under the selected profiler, writes the manifest and outputs, fsyncs them, and
renames the directory into place. A profiler failure is still valuable
evidence, so it is retained with `status=failed` and a non-zero exit status.

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Build and run focused Release Stage 11 tests:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release -R '^stage11_' --output-on-failure
```

Run the three central experiment tools:

```bash
./build-release/stage11_bench_end_to_end \
  <input> <serial-output> <no-overlap-output> <overlap-output> \
  1048576 8 4 5

./build-release/stage11_bench_backends \
  <input> <existing-output-directory> 1048576 8 4 2 5

python3 benchmark/stage11_parameter_sweep.py \
  --executable ./build-release/preprocess_pipeline_demo \
  --input <input> \
  --output-directory <scratch-directory> \
  --csv <raw.csv> \
  --environment-id <recorded-id> \
  --block-sizes 1048576,4194304 \
  --buffers 3,8 \
  --queue-depths 1,4 \
  --backends sync,threadpool,uring \
  --thread-workers 2 \
  --iterations 5
```

The exact reference commands are archived in
`docs/benchmark_results/2026-08-10-wsl2-reference/commands.md`.

## 6. Current Test and Experiment Results

Verified on 2026-08-10:

- Debug full CTest: 53/53 passed, 0 failed.
- Release focused Stage 11 CTest: 6/6 passed, 0 failed.
- Warning-enabled focused Stage 11 build and CTest: 6/6 passed, 0 failed.
- ASan/UBSan focused Stage 11 build and CTest: 6/6 passed, 0 failed.
- Profile-capture test repeated 30 times: passed without failure.
- Real explicit io_uring smoke: backend selected, output committed, and output
  verification passed.
- Real parameter matrix: 120/120 child runs verified output and stayed within
  configured queue, in-flight, and 300 MiB RSS bounds.
- Real bounded-RSS scale check: 64 MiB through 4 GiB all verified and remained
  below 300 MiB; RSS plateaued at about 155.7 MiB for 1-4 GiB.
- Real strace capture: Sync, ThreadPool, and io_uring commands all completed.
- Real perf capture: unavailable for this WSL2 kernel; failure evidence was
  retained and no counter result is claimed.

The reference campaign's important performance observation is variability,
not a winner. The 24 exact parameter groups produced fastest counts of Sync 3,
ThreadPool 2, and io_uring 3. Several configurations had one multi-second tail,
so more controlled repetitions are needed before strong tail-latency claims.

## 7. Problems Encountered and How They Were Handled

- A serial-versus-three-thread comparison alone could not isolate overlap.
  A one-buffer three-stage control was added so topology stays constant while
  buffer availability disables cross-block overlap.
- Auto could have been mislabeled as a fourth backend. CSV names and metadata
  now include its actual selection, and the analyzer excludes Auto from
  distinct-mechanism rankings.
- Reusing one process would make later configurations inherit earlier maximum
  RSS. The sweep launches a fresh process for every sample.
- A failed/mixed sweep could leave believable partial output. Raw CSV and
  generated reports are written to temporary paths, fsynced, and renamed only
  when complete; failed samples preserve diagnostics.
- Aggregating unlike block/buffer/queue settings could create meaningless
  averages. The analyzer rejects environment conflicts and groups by every
  material configuration field.
- Python's default CSV line ending was CRLF, which Git reported as trailing
  whitespace on Linux. The sweep now requests LF explicitly; archived values
  were unchanged while their line endings were normalized.
- WSL2 samples showed large, non-repeatable tails. Raw rows, P50/P95, and
  limitations are retained; no speedup or causal explanation is invented.
- The local perf wrapper lacked tools for kernel
  `6.18.33.2-microsoft-standard-WSL2`. The failed capture was archived instead
  of installing packages or fabricating counters.
- TSan binaries still abort in this WSL environment before project code with
  `ThreadSanitizer: unexpected memory mapping`; Stage 11 makes no TSan-safety
  claim.

## 8. Remaining Issues and Acceptance Boundaries

- T1/T1b are not formally passed. The 64 MiB-to-4 GiB scale check is strong
  small-scale evidence, but the checklist still requires a real 50 GiB run
  and 1/50/200 GiB scale comparison under the same 256 MiB policy.
- T2's controlled unbounded ablation is not implemented. The production path
  remains bounded; if this negative experiment is added, Stage 13 must isolate
  it from the main path and cap its test size safely.
- T3 now has a runnable same-topology overlap ablation and overlapping
  architecture, but this five-sample WSL2 campaign did not show a stable
  speedup over the serial oracle. T3 is therefore not claimed as passed.
- T4 exact ordered output passed in every accepted Stage 11 pipeline sample.
- T5 multi-core out-of-order processing is not implemented; the current single
  processor and FIFO queues preserve order with explicit offsets.
- T6 crash-safety kill testing remains for Stage 13. Reliable
  temp-file/fsync/rename publication is already the production mechanism.
- T7 ASan/UBSan passed for Stage 11, but TSan remains blocked by the runtime
  environment; full T7 is not claimed.
- T8 Auto selection/fallback correctness is covered by existing tests.
- T9 uses a real modifying `ByteIncrementStage`, not NoOp/checksum-only work,
  but its CPU-heavy characterization and overlap-under-heavy-processing test
  are not yet established.
- `perf stat` needs matching kernel tools and permissions on a suitable Linux
  host before hardware-counter conclusions can be added.

## 9. Next Stage

Stage 12 is Terminal output and optional JSON metrics. It should improve the
presentation of existing metrics without changing queue bounds, BufferPool
ownership, backend semantics, processing order, benchmark data, or reliable
publication. Dashboard, database, distributed execution, CUDA, and domain file
formats remain out of scope.

Stage 13 remains responsible for final error/acceptance testing, final
documentation, and interview packaging, including unresolved large-file and
environment-dependent checks.

## 10. Interview Explanation

Short version:

> I built a benchmark layer around the same bounded read-process-write
> pipeline rather than a separate toy loop. It compares a serial oracle, a
> one-buffer no-overlap control, an overlap-capable pipeline, and three read
> backend mechanisms under identical processing and writing. Every sample
> verifies output and configured bounds, and a fresh process records peak RSS.
> On my WSL2 reference run there was no universal backend winner, while RSS
> plateaued near 156 MiB from 1 to 4 GiB; I report that as bounded-memory
> evidence, not as the still-unrun 50/200 GiB acceptance claim.

The important engineering boundary is that coroutines express suspension;
they do not guarantee speed. io_uring is one backend and may win or lose based
on workload, filesystem, cache, and configuration. Raw data and profiler
evidence are kept so conclusions remain falsifiable.

## 11. Anti-Collapse Self-Check

1. **Did this stage introduce or break a hard constraint?**
   No. It added bounded baselines, measurement orchestration, analysis, and
   evidence capture. It did not replace the production streaming pipeline,
   remove metrics, or introduce an unbounded main-path queue.
2. **Is memory still bounded?**
   Yes. Production data memory remains governed by block size, fixed BufferPool
   capacity, and two fixed-capacity queues. Benchmark verification uses fixed
   reusable blocks. The Python sweep holds only bounded result metadata and is
   not the data path.
3. **Can the project pass the large-file bounded-memory test now?**
   The command and 300 MiB guard exist, and 1-4 GiB observations plateaued.
   Formal T1/T1b still require physically allocated 50/200 GiB inputs and a
   recorded suitable environment, so they are not claimed as passed.
4. **Who owns each buffer?**
   Production ownership is unchanged:
   `BufferPool -> reader BlockWorkItem -> read/process queue -> processor ->
   process/write queue -> writer -> BufferHandle destructor -> BufferPool`.
   The serial oracle owns one aligned buffer locally and reuses it per block.
   File verification owns two local fixed buffers and never leaks them into the
   pipeline.
5. **Which acceptance tests are not ready?**
   T1/T1b large-scale runs, T2 controlled negative ablation, T3 stable speedup
   evidence, T5 out-of-order processing, T6 kill testing, complete T7 TSan on a
   supported host, and T9 CPU-heavy behavior remain unclaimed. Stage 13 owns
   final acceptance and documentation; no future result is invented here.
