# Stage 3 Summary: Benchmark Framework and mmap Baseline

Date: 2026-07-13

## 1. What Was Completed

Stage 3 established reusable benchmark mechanics instead of recording
unverifiable performance claims.

- Added strict bounded parsing for benchmark block sizes and iteration counts.
- Added reusable average/P50/P95/P99 latency statistics using nearest-rank
  percentiles.
- Added a shared `BenchmarkReport` and one CSV schema for all benchmark paths.
- Added repeated end-to-end `sync_baseline` measurement.
- Added a bounded-buffer `pread_scan` microbenchmark.
- Added an `mmap_scan` microbenchmark with empty-file and error handling.
- Separated comparable scan workloads from the end-to-end preprocessing
  workload.
- Added normal, empty-input, missing-file, malformed-config, and output
  correctness tests.
- Replaced the placeholder benchmark document with a reproducible methodology,
  environment template, result templates, and interpretation guardrails.
- Added deeper stage/task teaching requirements to the project coaching prompt.
- Initialized CodeGraph for local code navigation and ignored its machine-local
  database directory in Git.

No official benchmark numbers were collected. CTest timings are functional test
data and must not be presented as performance results.

## 2. Current Relevant Directory Structure

```text
.
|-- AGENTS.md
|-- CMakeLists.txt
|-- benchmark/
|   |-- stage3_bench_mmap.cpp
|   |-- stage3_bench_pread.cpp
|   `-- stage3_bench_sync.cpp
|-- include/
|   `-- benchmark/
|       |-- benchmark_cli.h
|       |-- benchmark_report.h
|       `-- benchmark_stats.h
|-- src/
|   |-- baseline/
|   |   `-- sync_preprocess_baseline.cpp
|   `-- benchmark/
|       |-- benchmark_report.cpp
|       `-- benchmark_stats.cpp
|-- tests/
|   |-- stage3_bench_mmap_test.cmake
|   |-- stage3_bench_pread_test.cmake
|   |-- stage3_bench_sync_test.cmake
|   `-- stage3_benchmark_stats_test.cpp
`-- docs/
    |-- benchmark.md
    `-- stage_summaries/
        `-- stage3_summary.md
```

## 3. Added or Modified Files

### Build and local tooling

- `CMakeLists.txt`
  - Builds the reusable `asyncdataloader_benchmark` library.
  - Builds sync, pread, and mmap benchmark executables.
  - Registers all Stage 3 tests.
- `.gitignore`
  - Ignores machine-local `.codegraph/` state.
- `.codegraph/`
  - Local symbol/call graph index; not product code and not committed.

### Benchmark support

- `include/benchmark/benchmark_cli.h`
  - Parses complete positive integer arguments without exceptions.
  - Limits iterations to 1,000,000 and block size to 1 GiB.
- `include/benchmark/benchmark_stats.h`
  - Defines `BenchmarkSummary` and the latency summary interface.
- `src/benchmark/benchmark_stats.cpp`
  - Validates samples and calculates average/P50/P95/P99.
- `include/benchmark/benchmark_report.h`
  - Defines the common benchmark report and CSV interface.
- `src/benchmark/benchmark_report.cpp`
  - Calculates total elapsed time and aggregate input throughput.
  - Writes the shared CSV header and row.

### Benchmark executables

- `benchmark/stage3_bench_sync.cpp`
  - Repeats the serial read-transform-write-`fsync` baseline.
  - Verifies byte counts remain stable between iterations.
- `benchmark/stage3_bench_pread.cpp`
  - Sequentially scans with `pread` into one bounded reusable block.
  - Performs a checksum so Release builds cannot remove the scan.
- `benchmark/stage3_bench_mmap.cpp`
  - Maps, sequentially scans, and unmaps the input each iteration.
  - Handles zero-length inputs without calling invalid zero-length `mmap`.
- `src/baseline/sync_preprocess_baseline.cpp`
  - Added explicit parentheses to the existing letter-range condition, removing
    a warning without changing behavior.

### Tests and documentation

- `tests/stage3_benchmark_stats_test.cpp`
  - Tests strict parsing, invalid samples, nearest-rank statistics, throughput,
    and CSV rendering.
- `tests/stage3_bench_sync_test.cmake`
  - Tests repeated/default runs, transformed output, missing input, and invalid
    block/iteration arguments.
- `tests/stage3_bench_pread_test.cmake`
  - Tests repeated scans, empty input, shared CSV, and invalid arguments.
- `tests/stage3_bench_mmap_test.cmake`
  - Tests repeated/default scans, empty input, missing input, and invalid
    iterations.
- `docs/benchmark.md`
  - Defines workloads, timing boundaries, CLI, CSV fields, fair-test protocol,
    environment capture, result templates, and interpretation limits.
- `AGENTS.md` and `.agents/skills/asyncdataloader-cpp-coach/SKILL.md`
  - Require detailed stage/task explanations in Driver/Navigator mode.

## 4. Purpose and Data Flow

### End-to-end baseline

```text
input -> pread blocks -> CPU case transform -> pwrite -> fsync -> output
      -> elapsed sample -> latency summary -> CSV
```

### Comparable scan microbenchmarks

```text
input -> bounded block + pread -> checksum -> elapsed samples --+
                                                               +-> report -> CSV
input -> mmap -> page-backed byte scan -> checksum -> samples --+
```

Only `pread_scan` and `mmap_scan` are direct scan comparisons.
`sync_baseline` includes CPU processing, writing, and durability and must be
compared only with future end-to-end pipelines using the same semantics.

## 5. Commands That Work

Debug build and tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Release build and tests:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

Benchmark CLI:

```bash
./build-release/stage3_bench_sync \
  <input> <output> <block_size_bytes> [iterations]

./build-release/stage3_bench_pread \
  <input> <block_size_bytes> [iterations]

./build-release/stage3_bench_mmap \
  <input> [iterations]
```

All executables emit:

```text
name,bytes_per_iteration,bytes_written_per_iteration,sample_count,total_elapsed_ms,average_ms,p50_ms,p95_ms,p99_ms,throughput_mib_s
```

## 6. Current Test Results

Verified on 2026-07-13:

- Debug full build: passed.
- Debug CTest: 13/13 passed, 0 failed.
- Release full build with GNU C++ 11.4.0: passed.
- Release CTest: 13/13 passed, 0 failed.
- Stage 3 warning build with `-Wall -Wextra -Wpedantic -Wconversion
  -Wshadow`: passed with no remaining warnings in the built Stage 3 targets.
- ASan/TSan: not run in this stage; no sanitizer claim is made.
- Official performance benchmark: not run; no performance claim is made.

## 7. Bugs Encountered and Fixes

- `stage3_bench_mmap` initially contained only TODO code.
  - Implemented open/stat/map/scan/unmap and explicit syscall errors.
- Zero-length `mmap` is invalid.
  - Empty files bypass mapping and produce zero-byte samples.
- A scan loop can be optimized away in Release mode.
  - Both scan baselines accumulate into a volatile checksum.
- Single-run output could not provide tail latency.
  - Added bounded repetitions and reusable nearest-rank statistics.
- Prefix-only numeric parsing can accept values such as `3junk`.
  - Added `std::from_chars` parsing with full-consumption checks.
- Sync and mmap originally emitted different CSV fields.
  - Added one shared report and CSV writer.
- Sync preprocessing and mmap scanning were at risk of being compared as equal
  workloads.
  - Added `pread_scan` and documented two separate workload families.
- A reused Stage 2 letter-range condition triggered `-Wparentheses`.
  - Added explicit grouping without changing the transform.

## 8. Remaining Issues

- Stage 3 does not implement asynchronous I/O, overlap, fallback, BufferPool,
  backpressure, stage metrics, or crash-safe temporary-file rename.
- `mmap_scan` maps the whole file's virtual address range and depends on kernel
  paging behavior. It is a comparison baseline, not the final main path.
- Concurrent truncation of an mmap input can cause `SIGBUS`; the benchmark
  assumes an immutable input during a run.
- P95/P99 from small sample counts are statistically weak even though the math
  is correct.
- Cold-cache versus warm-cache experiments are not automated. Every official
  run must record its cache policy.
- Full sync/threadpool/io_uring/pipeline results, raw CSV, `strace`, and `perf`
  analysis remain for Stage 11.

## 9. Next Stage

Stage 4: native liburing basics, without coroutines.

The smallest first task should submit one read request and explain:

```text
get SQE -> prepare read -> submit -> wait CQE -> inspect cqe->res -> seen CQE
```

Stage 4 must not introduce `co_await`; coroutine integration belongs to Stage 5.

## 10. Interview Explanation

Short version:

> I first built a reproducible benchmark layer rather than assuming io_uring
> would win. It validates benchmark configuration, records repeated latency
> samples, calculates nearest-rank P50/P95/P99, and emits one CSV schema. I keep
> read-only `pread` and mmap scans separate from the durable sync
> read-process-write baseline, so comparisons preserve workload semantics.

Important follow-up points:

- Throughput is total input bytes divided by total measured time.
- Percentiles need enough samples; P99 from a tiny run is not strong evidence.
- Cache state and filesystem behavior can dominate scan results.
- Coroutines are not a performance source.
- mmap is useful in some scenarios but does not provide the explicit buffer and
  prefetch control required by the final pipeline.

## 11. Anti-Collapse Self-Check

### Did this stage introduce or break a hard constraint?

No final-path hard constraint was removed or weakened. All Stage 3 programs are
explicit baselines. The serial baseline is retained as the correctness oracle,
not presented as the final pipeline. No unbounded queue or all-block vector was
introduced, and no benchmark number was invented.

### Is memory currently bounded, and can T1 run now?

- `sync_baseline` owns one block at a time, limited by the CLI block-size bound.
- `pread_scan` owns one reusable block at a time.
- Benchmark latency storage is limited by the iteration bound.
- `mmap_scan` maps the whole file's virtual address space and therefore is not
  evidence of the final configurable memory bound.

The project cannot yet pass T1 (`256MB` processing `50GB` with the final
pipeline), because BufferPool, bounded queues, backpressure, and the end-to-end
pipeline do not exist yet. Stage 8 implements the bounds, Stage 10 integrates
the pipeline, and Stage 11 measures the large-file behavior.

### Who owns each buffer or mapping?

- Sync baseline: the function-local `std::vector<char>` owns the current block;
  `FdGuard` owns each file descriptor.
- Pread scan: `main` owns one `std::vector<char>` for the whole benchmark;
  `OpenFileResult::fd` owns the input descriptor.
- mmap scan: the current iteration owns the mapping; `bytes` is a borrowed
  read-only pointer valid only until `munmap`. The descriptor is closed after
  all iterations.
- Benchmark support: `samples_ms` owns only elapsed-time values. The report owns
  its workload name and copied summary values.

No raw pointer is retained beyond its resource lifetime.

### Which acceptance tests are not ready, and when are they addressed?

| Acceptance | Status and planned stage |
|---|---|
| T1/T1b bounded-memory large file | Stage 8 design, Stage 10 integration, Stage 11 measurement |
| T2 backpressure comparison | Stage 8 implementation, Stage 11 experiment |
| T3 three-stage overlap | Stage 10 implementation, Stage 11 metrics evidence |
| T4 pipeline output equals serial oracle | Stage 10 |
| T5 out-of-order processing writes correct offsets | Stage 10/13 |
| T6 temporary file + fsync + rename crash safety | Stage 10/13 |
| T7 ASan/TSan buffer lifetime | Stage 8 onward, final verification in Stage 13 |
| T8 backend fallback | Stage 6 implementation, Stage 13 error test |
| T9 real CPU stage under overlap | Stage 7/10 implementation, Stage 11 benchmark |

Stage 3 itself verifies benchmark arithmetic, CSV contracts, scan behavior,
serial output correctness, and selected invalid-input paths.
