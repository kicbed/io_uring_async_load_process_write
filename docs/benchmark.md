# AsyncDataLoader Benchmark Methodology

## Current Status

Stage 3 provides reusable latency statistics, a shared CSV schema, and three
benchmark executables. Stage 11.1 adds a controlled overlap experiment:
a serial oracle plus the same three-worker `SyncBackend` pipeline with either
one buffer (no cross-block overlap) or at least three buffers (overlap
permitted). Stage 11.2 adds a read-backend matrix for Sync, ThreadPool,
io_uring, and Auto's recorded selection. Stage 11.3 adds a separate-process
parameter sweep that records pipeline metrics and GNU `time` peak RSS. Stage
11.4 validates and aggregates those raw rows, creates SVG charts without a
third-party plotting dependency, and generates an evidence-bounded report.
Stage 11.5 captures reproducible `strace -f -c` or `perf stat` evidence for one
exact command while keeping profiler-distorted timing outside benchmark data.

No official performance result has been collected yet. Tiny CTest inputs
verify behavior only; their timings are not benchmark data.

Official results still require recorded Release runs on declared environments.

## Workload Families

| Executable | CSV name | Timed work per iteration | Writes output | Valid comparison |
|---|---|---|---|---|
| `stage3_bench_sync` | `sync_baseline` | open, block allocation, `pread`, CPU case transform, `pwrite`, `fsync`, close | yes | future end-to-end pipelines with the same transform and durability |
| `stage3_bench_pread` | `pread_scan` | sequential `pread` into one bounded buffer plus byte checksum | no | `mmap_scan` under the same file/cache conditions |
| `stage3_bench_mmap` | `mmap_scan` | `mmap`, sequential byte scan plus checksum, `munmap` | no | `pread_scan` under the same file/cache conditions |
| `stage11_bench_end_to_end` | `serial_sync_byte_increment` | one aligned block, one thread: `pread` -> byte increment -> `pwrite`, then `fsync` | yes | correctness oracle and whole-architecture reference |
| `stage11_bench_end_to_end` | `pipeline_sync_no_overlap_byte_increment` | three workers and two bounded queues, but one pool buffer forces each block to finish writing before the next read | yes | overlap row from the same command |
| `stage11_bench_end_to_end` | `pipeline_sync_overlap_byte_increment` | same workers, queues, SyncBackend, and transform, with at least three pool buffers | yes | no-overlap row from the same command |
| `stage11_bench_backends` | `pipeline_sync_byte_increment` | bounded three-worker pipeline; reader thread performs blocking `pread` | yes | other backend rows from the same command |
| `stage11_bench_backends` | `pipeline_thread_pool_byte_increment` | same pipeline; read coroutine suspends while a fixed worker pool performs `pread` | yes | other backend rows from the same command |
| `stage11_bench_backends` | `pipeline_io_uring_byte_increment` | same pipeline; read coroutine suspends after SQE submission and resumes from CQE completion | yes | other backend rows from the same command |
| `stage11_bench_backends` | `pipeline_auto_selected_<actual>_byte_increment` | same pipeline using the backend selected by Auto | yes | selection/fallback evidence, not a fourth I/O mechanism |

`sync_baseline` and `mmap_scan` are not directly comparable. The former is a
complete read-process-write workload with durable output; the latter is a
read-only scan. A shared CSV format does not make different workloads equal.

## Measurement Boundaries

- `sync_baseline` measures the full baseline function, including file open,
  output truncation, block allocation, transform, write, `fsync`, and RAII
  close. Each iteration rewrites the output from offset zero.
- `pread_scan` opens the input and allocates its bounded block before the
  iteration timers. Each sample covers all `pread` calls and checksum work.
- `mmap_scan` opens and stats the input before the iteration timers. Each sample
  covers mapping, sequential access, and unmapping.
- Stage 11.1 and 11.2 open/truncate descriptors and construct processing
  objects before each timer. Each sample covers processing, all reads/writes,
  and output `fsync`. Descriptor close and bounded output verification are
  outside the timer.
- Stage 11.1 rotates the serial, no-overlap, and overlap order. Stage 11.2
  rotates the available backend order. Neither command drops or warms the
  Linux page cache, so the caller must record the resulting cache policy.
- Stage 11.2 constructs a fresh backend before each timer. Backend construction
  and destruction are excluded, so the rows measure data-path execution rather
  than thread/ring setup.
- Both commands enable existing pipeline metrics. Detailed overlap and
  bottleneck conclusions still require real recorded runs; Stage 11.5 only
  standardizes collection of the supporting system evidence.
- The current reader completes one block before submitting the next read.
  Therefore the io_uring row exercises SQE/CQE coroutine suspension but does
  not claim multiple simultaneous read requests or queue-depth scaling.
- In Stage 11.3, `queue_depth` means the capacity of each pipeline handoff
  queue. The current demo sets io_uring ring depth from the buffer count, but
  with one outstanding read it still does not measure ring-depth scaling.
- Stage 11.3 starts a fresh `preprocess_pipeline_demo` process for every sample.
  This matters because a process-wide maximum RSS cannot be reset between
  configurations. The demo's `elapsed_ms` covers pipeline execution and
  reliable publication; GNU `time` peak RSS covers the whole C++ process,
  including the bounded verification pass.
- Stage 11.3 reuses one uniquely named scratch output and removes it after the
  sweep. It holds only bounded result rows in the Python launcher and writes
  the final CSV through a temporary file, `fsync`, and rename.
- Stage 11.4 recomputes aggregate throughput as total group bytes divided by
  total group time. It never averages unlike block, buffer, queue, backend,
  worker, input, or environment configurations into one result.
- Auto rows remain fallback-policy observations. The analyzer can show their
  selected backend and same-config Sync ratio, but excludes Auto from rankings
  of distinct I/O mechanisms.
- A Stage 11.5 invocation runs the exact child command once under one profiler.
  `strace -f -c` aggregates syscall counts across threads. `perf stat` records
  the counters supported and permitted by the current kernel. These tools add
  overhead, so their elapsed times are diagnostic rather than benchmark rows.
- Profiling `preprocess_pipeline_demo` covers process startup, backend setup,
  reliable publication, and the bounded verification pass as well as the core
  pipeline. Its summary is whole-command evidence, not isolated Stage timing.
- Throughput always means input bytes scanned or processed divided by measured
  time. Written bytes are reported separately and are not added to throughput.
- Average, P50, P95, and P99 are calculated from per-iteration elapsed times.
  Percentiles use the nearest-rank definition.

## CLI

Build benchmark binaries in Release mode:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

Run the end-to-end synchronous baseline:

```bash
./build-release/stage3_bench_sync \
  <input> <output> <block_size_bytes> [iterations]
```

Run the comparable scan microbenchmarks:

```bash
./build-release/stage3_bench_pread \
  <input> <block_size_bytes> [iterations]

./build-release/stage3_bench_mmap \
  <input> [iterations]
```

Run the Stage 11.1 same-workload end-to-end comparison:

```bash
./build-release/stage11_bench_end_to_end \
  <input> \
  <serial_output> \
  <no_overlap_output> \
  <overlap_output> \
  <block_size_bytes> \
  <overlap_max_inflight_buffers> \
  <queue_depth> \
  [iterations]
```

Stage 11.1 uses the production default alignment of 4096 bytes, so block size
must be a multiple of 4096, and the overlap configuration requires at least
three buffers. The no-overlap control always uses one buffer while keeping the
same queue capacity and thread topology.

Run the Stage 11.2 read-backend matrix:

```bash
mkdir -p <output_directory>
./build-release/stage11_bench_backends \
  <input> \
  <output_directory> \
  <block_size_bytes> \
  <max_inflight_buffers> \
  <queue_depth> \
  <thread_workers> \
  [iterations] \
  [--disable-uring] \
  [--disable-threadpool]
```

The disable switches affect Auto only; explicit ThreadPool and io_uring rows
remain explicit. If an explicit backend cannot be constructed, stderr records
`skipped_backend=<name>` and no row is falsely labeled as that backend. Auto's
CSV name contains the backend actually selected. The output directory must
already exist.

Both commands write standard CSV to stdout. Processing, timing, cache,
execution-order, backend-selection, and verification metadata go to stderr so
they do not corrupt the CSV stream.

Run the Stage 11.3 parameter and peak-RSS sweep:

```bash
mkdir -p <output_directory>
python3 benchmark/stage11_parameter_sweep.py \
  --executable ./build-release/preprocess_pipeline_demo \
  --input <input> \
  --output-directory <output_directory> \
  --csv <results.csv> \
  --environment-id <recorded_environment_id> \
  --block-sizes 1048576,4194304 \
  --buffers 3,8 \
  --queue-depths 1,4 \
  --backends sync,threadpool,uring \
  --thread-workers 2 \
  --iterations 3 \
  [--rss-limit-mib 300]
```

The three comma-separated lists form a Cartesian product. Backend order is
rotated across configurations/samples, and `--max-runs` defaults to 1000 to
catch accidental giant matrices. Explicit unavailable backends fail the sweep
instead of being mislabeled; choose `auto` only when fallback selection is the
thing being measured.

Analyze one or more Stage 11.3 CSV files recorded under the same environment
ID:

```bash
mkdir -p <analysis_directory>
python3 benchmark/stage11_analyze_results.py \
  --input-csv <raw-results.csv> \
  [--input-csv <another-raw-results.csv>] \
  --output-directory <analysis_directory> \
  --minimum-samples 5 \
  [--title "recorded experiment title"]
```

The output directory must already exist. The script uses only the Python
standard library and publishes these files:

- `summary.csv`: one row per exact configuration;
- `throughput.svg`: observed aggregate throughput bars;
- `peak_rss.svg`: whole-process peak RSS beside configured BufferPool payload;
- `analysis.md`: result table, adequately sampled comparisons, candidate
  counterintuitive findings, and interpretation guardrails.

The analyzer rejects missing columns, failed output verification, mislabeled
explicit backends, conflicting environment IDs, and queue/in-flight peaks that
exceed configuration. A group below `--minimum-samples` remains visible in the
table but is excluded from generated comparative findings.

Capture `strace` evidence for one exact command:

```bash
mkdir -p <profile_root>
python3 benchmark/stage11_capture_profile.py \
  --tool strace \
  --output-directory <profile_root> \
  --label <unique_label> \
  --environment-id <recorded_environment_id> \
  -- \
  <exact pipeline or benchmark command and arguments>
```

Capture `perf stat` evidence with a different label and scratch output:

```bash
python3 benchmark/stage11_capture_profile.py \
  --tool perf \
  --output-directory <profile_root> \
  --label <another_unique_label> \
  --environment-id <recorded_environment_id> \
  -- \
  <exact pipeline or benchmark command and arguments>
```

One invocation creates `<profile_root>/<label>/` containing:

- `manifest.txt`: environment ID, working directory, exact command, profiler
  command, exit code, and evidence status;
- `strace-summary.txt` or `perf-stat.csv`: untouched profiler summary;
- `command.stdout.txt` and `command.stderr.txt`: the profiled program's output.

The label is deliberately unique and an existing evidence directory is never
overwritten. A failed profiler or child command still publishes its stdout,
stderr, manifest, and any profiler output, then exits non-zero. This preserves
the failure evidence instead of silently presenting it as a successful run.

For the two C++ benchmark executables, `iterations` defaults to 1 and is
limited to 1,000,000; `block_size_bytes` is limited to 1 GiB. The Stage 11.3
sweep defaults to three samples per configuration and separately caps the
complete matrix with `--max-runs`.

## CSV Schema

```text
name,bytes_per_iteration,bytes_written_per_iteration,sample_count,total_elapsed_ms,average_ms,p50_ms,p95_ms,p99_ms,throughput_mib_s
```

| Field | Meaning |
|---|---|
| `name` | Stable workload identifier |
| `bytes_per_iteration` | Input bytes read or scanned in one iteration |
| `bytes_written_per_iteration` | Output bytes written in one iteration |
| `sample_count` | Number of completed timed iterations |
| `total_elapsed_ms` | Sum of all per-iteration elapsed times |
| `average_ms` | Arithmetic mean iteration latency |
| `p50_ms` | Nearest-rank 50th percentile latency |
| `p95_ms` | Nearest-rank 95th percentile latency |
| `p99_ms` | Nearest-rank 99th percentile latency |
| `throughput_mib_s` | Total input MiB divided by total measured seconds |

## Stage 11.3 Raw Sample CSV

The sweep writes one row per child-process sample. Its columns are grouped as:

- identity: environment ID, input path, input bytes, sample index;
- configuration: requested/selected backend, block size, buffer count, queue
  depth, worker count, and configured BufferPool bytes;
- correctness and progress: blocks/bytes written and verification status;
- pipeline observations: in-flight/queue peaks and read/process/write/Stage
  average latency;
- outcome: elapsed time, throughput, process peak RSS, and optional RSS-limit
  result.

`peak_rss_kib` comes from GNU `time %M` on Linux. It is the entire C++ process
high watermark, not just BufferPool payload memory. `rss_within_limit` is blank
when no limit was supplied. When a limit is exceeded, the complete CSV is
preserved and the script exits with status 3.

## Stage 11.4 Summary Rules

Rows are grouped only when environment, input, requested and selected backend,
block size, BufferPool capacity, queue capacity, worker count, and configured
BufferPool bytes all match. For each group:

- average, median, and nearest-rank P95 come from the sample elapsed times;
- aggregate throughput is total processed bytes divided by total elapsed time;
- peak RSS and bounded-queue/in-flight observations use group maxima;
- a Sync ratio is emitted only when the exact same configuration has an
  explicit Sync group;
- automatically worded findings require the configured minimum sample count.

The generated report calls a result an observation, not a cause. Its automatic
counterintuitive candidates use a 3% difference only as a profiling trigger;
that threshold is not a confidence interval or proof of statistical
significance.

## Stage 11.5 Profiling Rules

Use profiling only after a repeatable 11.3/11.4 observation identifies a
question. For example:

- `strace -f -c` can show whether a path used `pread64`, io_uring syscalls, or
  unexpectedly many metadata calls;
- `perf stat` can provide CPU, scheduling, and page-fault counters when the
  local kernel and permissions expose them;
- child stdout retains backend selection, correctness, and pipeline metrics so
  the profile can be matched to the intended workload.

Syscall count is not automatically a bottleneck, and a CPU counter is not an
automatic cause. Compare the same command/configuration, record cache and
environment policy, and treat profiler results as supporting evidence. Do not
compare `strace` wall time against normal or `perf` wall time as though they
were equivalent benchmark samples. Also inspect syscall names before treating
the `errors` column as a pipeline failure: dynamic-loader and feature-probing
calls can fail normally while the child command still exits successfully.

## Fair Test Protocol

1. Use the same Release build, machine, kernel, compiler, filesystem, storage
   device, and input file for all rows in one comparison.
2. Use a real allocated input file. Record whether it is sparse, compressed by
   the filesystem, or generated from another source.
3. Record exact input size and a checksum so every method receives identical
   bytes.
4. For `pread_scan` versus `mmap_scan`, use the same iteration count and run
   them close together. Record the `pread` block size.
5. State the cache policy. Repeated iterations usually mix the first page-fault
   pass with later page-cache hits. Do not label a run "cold cache" unless the
   cache-reset procedure is explicitly recorded.
6. Do not automate global Linux cache dropping in this repository. It requires
   privileges, affects other workloads, and can make results less reproducible.
7. For P95/P99 interpretation, use enough samples. With only a few samples,
   nearest-rank P99 is effectively the maximum and is statistically weak.
8. Keep background load, CPU power policy, and thermal conditions stable. Run
   methods in alternating order when practical to reduce time-order bias.
9. Place sync output on the recorded target filesystem and ensure enough free
   space. Its timing includes `fsync`, so storage durability behavior matters.
10. Save raw CSV before writing conclusions. Report regressions and cases where
    mmap or a fallback wins; do not force an io_uring-faster narrative.
11. Accept a Stage 11.1/11.2 sample only after its output passes the bounded
    byte-for-byte verifier against the serial oracle. A mismatch invalidates
    the complete command rather than producing partial CSV.
12. Keep every scratch output distinct from the input and from the other
    outputs. Benchmark files are truncated each run; production publication
    still uses the Stage 10 temporary-file/fsync/rename path.
13. Interpret Stage 11.1 mainly through no-overlap versus overlap. Serial versus
    overlap also changes thread/queue architecture, so it is a broader system
    comparison rather than isolated proof that overlap alone caused a change.
14. Interpret Auto as policy validation. If Auto selects io_uring, its row is
    another io_uring execution, not evidence for an additional backend.
15. For Stage 11.3, treat each CSV row as one raw sample. Do not average rows
    from different block/buffer/queue configurations into one result.
16. A successful tiny sweep proves orchestration and correctness, not T1/T1b.
    Those claims require the declared 1/50/200 GB inputs and recorded RSS.
17. Compare Stage 11.3 rows with other Stage 11.3 rows. Its reliable
    `run_file()` timing boundary differs from the scratch-output boundary used
    by the Stage 11.1/11.2 executables, so their absolute times must not be
    merged into one table as equivalent samples.
18. Keep raw CSV and generated summaries together. Never replace raw evidence
    with only a chart or hand-copied table.
19. Use `strace`/`perf` on the exact measured configuration before attributing
    a difference to syscall count, scheduling, page faults, or CPU work. The
    CSV alone cannot prove those causes.
20. Use distinct scratch outputs and evidence labels for profiler runs. Both
    profilers execute the child command; neither is a read-only inspection of
    an already completed process.
21. If `perf` is unavailable, lacks kernel support, or is denied by
    `perf_event_paranoid`, record the failure rather than replacing it with a
    guessed counter or a different-machine conclusion.

## Bounded-Memory Acceptance Command

For the T1 configuration from the anti-collapse checklist, first prepare a
real 50 GB input and record its allocation/checksum. Then run one bounded
sample such as:

```bash
python3 benchmark/stage11_parameter_sweep.py \
  --executable ./build-release/preprocess_pipeline_demo \
  --input <real-50gb-input> \
  --output-directory <same-filesystem-scratch-directory> \
  --csv <t1-50gb.csv> \
  --environment-id <environment_id> \
  --block-sizes 8388608 \
  --buffers 24 \
  --queue-depths 8 \
  --backends auto \
  --iterations 1 \
  --rss-limit-mib 300
```

This configures 192 MiB of BufferPool payload. Peak RSS will also include two
8 MiB verification blocks, code, stacks, metrics, and allocator overhead. T1b
requires repeating the same configuration for 1/50/200 GB inputs and comparing
the recorded peak RSS values. These commands are acceptance procedures, not
claims that those large runs have already happened.

## Environment Record Template

```text
date:
git_commit:
build_type: Release
compiler_and_version:
cmake_version:
kernel:
cpu:
memory:
storage_device:
filesystem:
mount_options:
cpu_governor:
input_path:
input_size_bytes:
input_checksum:
input_sparse_or_compressed:
output_path_and_filesystem:
cache_policy:
background_load_notes:
iterations:
pread_block_size_bytes:
raw_csv_path:
```

## Result Record Templates

Comparable read-only scans:

| Environment ID | Input size | Cache policy | Iterations | Method | Block size | Average ms | P95 ms | P99 ms | MiB/s |
|---|---:|---|---:|---|---:|---:|---:|---:|---:|
| pending | pending | pending | pending | `pread_scan` | pending | pending | pending | pending | pending |
| pending | pending | pending | pending | `mmap_scan` | n/a | pending | pending | pending | pending |

End-to-end preprocessing:

| Environment ID | Input size | Iterations | Method | Block size | Bytes written | Average ms | P95 ms | MiB/s | Output verified |
|---|---:|---:|---|---:|---:|---:|---:|---:|---|
| pending | pending | pending | `sync_baseline` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | `serial_sync_byte_increment` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | `pipeline_sync_no_overlap_byte_increment` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | `pipeline_sync_overlap_byte_increment` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | `pipeline_sync_byte_increment` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | `pipeline_thread_pool_byte_increment` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | `pipeline_io_uring_byte_increment` | pending | pending | pending | pending | pending | pending |
| pending | pending | pending | `pipeline_auto_selected_<actual>_byte_increment` | pending | pending | pending | pending | pending | pending |

Parameter/RSS raw samples:

| Environment ID | Input size | Requested/selected backend | Block size | Buffers | Queue depth | Sample | BufferPool bytes | Peak RSS KiB | Elapsed ms | MiB/s | RSS limit passed | Verified |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|
| pending | pending | pending | pending | pending | pending | pending | pending | pending | pending | pending | pending | pending |

## Interpretation Guardrails

- Coroutines organize asynchronous state; they are not themselves a performance
  source.
- `io_uring` is not assumed to be fastest for every file size, queue depth,
  filesystem, or cache state.
- ThreadPool has extra worker threads, while Sync blocks the reader thread and
  io_uring uses kernel SQE/CQE completion. Stage 11.2 compares those complete
  read mechanisms; it does not pretend their thread counts are identical.
- mmap uses demand paging and the page cache. Mapping a whole file is not the
  final bounded-buffer pipeline design.
- The serial baseline is retained as a correctness oracle and end-to-end
  reference. It is not the final pipeline architecture.
- Queue and in-flight high watermarks staying below configuration are useful
  consistency checks, but T1/T1b additionally require real large-file peak-RSS
  measurements across different input sizes.
- Stage 3 establishes measurement mechanics. Stage 11 records full backend and
  pipeline results with `strace`/`perf` evidence where useful.
