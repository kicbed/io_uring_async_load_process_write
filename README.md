# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

Current status: Stage 11 is complete and Stage 12 is next.
`preprocess_pipeline_demo` streams a file through one reader, one CPU
processor, and one writer with two fixed-capacity queues and a fixed-size
aligned BufferPool. A move-only `BlockWorkItem` carries one RAII buffer lease
and its offset through the whole path. The built-in `ByteIncrementStage`
changes every byte modulo 256, and the demo verifies the published output in a
second bounded streaming pass.

The demo accepts Auto, io_uring, thread-pool, or synchronous read backends.
Auto mode preserves the Stage 6 construction-time fallback policy. Runtime
output exposes progress, measured throughput, read/process/write latency,
per-Stage latency, queue depths, and current/peak in-flight buffers. Final data
is written to a same-directory temporary file, file-synced, atomically renamed,
and followed by a parent-directory fsync. Stage 11.1 now provides a controlled
no-overlap-versus-overlap experiment, while Stage 11.2 runs the same bounded
pipeline through Sync, ThreadPool, io_uring, and the recorded Auto selection.
Stage 11.3 automates block-size, buffer-count, queue-depth, and backend sweeps
while recording each child process's peak RSS. Stage 11.4 validates and groups
those raw samples, then produces a summary CSV, two dependency-free SVG charts,
and an evidence-bounded Markdown report. Stage 11.5 captures an exact command's
`strace -f -c` or `perf stat` output without mixing profiler-distorted timing
into benchmark CSV. Stage 11.6 archives one real WSL2 reference campaign with
raw CSV, charts, environment, commands, bounded-RSS observations, and honest
profiler limitations. Formal 50/200 GiB acceptance is still unclaimed;
functional-test timings are not benchmark claims.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Stage 8 Backpressure Demo

```bash
./build/stage8_backpressure_demo
```

The demo uses two aligned pool buffers and a queue with one slot. The first
handle fills the queue, the producer's second `push()` waits, and a consumer
`pop()` makes space and wakes it. The output also verifies FIFO markers and
that both RAII leases returned their buffers to the pool.

See [`docs/stage8_odirect_alignment.md`](docs/stage8_odirect_alignment.md) for
the boundary between Stage 8's aligned allocation and a future real
`O_DIRECT` I/O path.

## Stage 9 Metrics Tests

```bash
ctest --test-dir build -R '^stage9_' --output-on-failure
```

The Stage 9 tests cover concurrent metric updates, fixed histogram buckets,
bounded registry behavior, automatic stage timing, BufferHandle-aware in-flight
tracking, and independent registry snapshots. A snapshot is reporting data for
later terminal or JSON formatting; Stage 9 does not calculate benchmark
throughput or claim performance improvements.

## Stage 10 End-to-End Demo

```bash
./build/preprocess_pipeline_demo \
  /path/to/input.bin \
  /path/to/output.bin \
  --backend=auto

./build/preprocess_pipeline_demo \
  /path/to/input.bin \
  /path/to/output.bin \
  --backend=auto \
  --disable-uring
```

Useful boundedness controls are `--block-size`, `--buffers`, and
`--queue-depth`. Run `./build/preprocess_pipeline_demo --help` for the complete
interface. Explicit backend selection is fail-fast; only Auto mode falls back.

## Stage 10 Tests

```bash
ctest --test-dir build -R '^stage10_' --output-on-failure
```

These tests cover work-item ownership, normal/error queue shutdown, RAII lease
return, the three-thread executor, runtime metrics, exact transformed output,
forced Auto fallback, and reliable temporary-file publication.

## Stage 11.1 Fair End-to-End Baseline

```bash
./build-release/stage11_bench_end_to_end \
  /path/to/input.bin \
  /path/to/serial-output.bin \
  /path/to/no-overlap-output.bin \
  /path/to/overlap-output.bin \
  1048576 \
  8 \
  4 \
  20
```

The two pipeline rows use the same three worker threads, queues,
`SyncBackend`, and `ByteIncrementStage`. The control owns one buffer, which
prevents cross-block overlap; the treatment owns at least three, which permits
read/process/write overlap. The serial row is the correctness oracle. CSV is
emitted only after bounded byte-for-byte verification succeeds.

## Stage 11.2 Read Backend Matrix

```bash
mkdir -p /tmp/asyncdataloader-backends
./build-release/stage11_bench_backends \
  /path/to/input.bin \
  /tmp/asyncdataloader-backends \
  1048576 \
  8 \
  4 \
  2 \
  20
```

This command keeps the processing and writing paths fixed and changes only the
read backend. Explicit unavailable backends are reported as skipped rather
than mislabeled. Auto is reported as a selection policy together with the
backend it actually chose. See `docs/benchmark.md` for exact measurement
boundaries and limitations.

## Stage 11.3 Parameter and RSS Sweep

```bash
mkdir -p /tmp/asyncdataloader-sweep
python3 benchmark/stage11_parameter_sweep.py \
  --executable ./build-release/preprocess_pipeline_demo \
  --input /path/to/input.bin \
  --output-directory /tmp/asyncdataloader-sweep \
  --csv /tmp/asyncdataloader-sweep/results.csv \
  --environment-id local-release \
  --block-sizes 1048576,4194304 \
  --buffers 3,8 \
  --queue-depths 1,4 \
  --backends sync,threadpool,uring \
  --thread-workers 2 \
  --iterations 3
```

Every sample launches a fresh C++ process, so GNU `time` can report that
sample's independent peak RSS. The script reuses and removes one uniquely
named scratch output, requires the C++ correctness check to pass, checks that
queue/buffer metrics remain within their configured bounds, and atomically
publishes the CSV only after the complete matrix succeeds.

## Stage 11.4 Result Analysis

```bash
mkdir -p /tmp/asyncdataloader-analysis
python3 benchmark/stage11_analyze_results.py \
  --input-csv /tmp/asyncdataloader-sweep/results.csv \
  --output-directory /tmp/asyncdataloader-analysis \
  --minimum-samples 5
```

The analyzer keeps unlike configurations separate and refuses to combine
different environment IDs. It writes `summary.csv`, `throughput.svg`,
`peak_rss.svg`, and `analysis.md`. Generated findings describe only the
recorded observations. They do not claim that io_uring is always fastest or
guess a performance cause without separate `strace`/`perf` evidence.

## Stage 11.5 System Evidence Capture

Capture a syscall summary for one exact pipeline command:

```bash
mkdir -p /tmp/asyncdataloader-profiles
python3 benchmark/stage11_capture_profile.py \
  --tool strace \
  --output-directory /tmp/asyncdataloader-profiles \
  --label sync-strace \
  --environment-id local-release \
  -- \
  ./build-release/preprocess_pipeline_demo \
  /path/to/input.bin \
  /tmp/sync-strace-output.bin \
  --backend=sync \
  --block-size=1048576 \
  --buffers=8 \
  --queue-depth=4 \
  --thread-workers=2 \
  --report-ms=0
```

Use `--tool perf` and a new label/output file to capture `perf stat` instead.
Each invocation really runs the pipeline once. The new evidence directory
contains the exact command, profiler output, child stdout/stderr, and status.
Existing evidence is never overwritten. Profiled wall time is diagnostic and
must not be copied into the Stage 11.3 benchmark CSV.

## Stage 11.6 Recorded Reference Result

The archived [WSL2 reference campaign](docs/benchmark_results/2026-08-10-wsl2-reference/README.md)
contains the exact environment, reproduction commands, raw samples, generated
CSV/SVG/Markdown analysis, strace evidence, and the retained perf-unavailable
failure. Its 24 exact parameter groups had no universal backend winner. With a
192 MiB configured BufferPool payload, observed whole-process RSS plateaued at
about 155.7 MiB for 1-4 GiB inputs and every row passed output and configured
bound checks. This is bounded-memory evidence, not a substitute for the still
unrun 50/200 GiB T1/T1b acceptance campaign.

## Custom Stage Demo

```bash
./build/stage7_custom_stage_demo
```

The demo registers a caller-defined affine transformation and changes the
fixed block `[1,2,3]` into `[3,5,7]` with `output = input * 2 + 1`.

## Backend Fallback Demo

```bash
./build/stage6_backend_fallback_demo \
  /path/to/input \
  --backend=auto

./build/stage6_backend_fallback_demo \
  /path/to/input \
  --backend=auto \
  --disable-uring
```

The demo prints both the requested and selected backend, the byte count, and
the first fixed-size block. This build currently requires the liburing
development package even when the runtime factory falls back to another
backend.
