# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

Current status: Stage 10 is complete. `preprocess_pipeline_demo` streams a file
through one reader, one CPU processor, and one writer with two fixed-capacity
queues and a fixed-size aligned BufferPool. A move-only `BlockWorkItem` carries
one RAII buffer lease and its offset through the whole path. The built-in
`ByteIncrementStage` changes every byte modulo 256, and the demo verifies the
published output in a second bounded streaming pass.

The demo accepts Auto, io_uring, thread-pool, or synchronous read backends.
Auto mode preserves the Stage 6 construction-time fallback policy. Runtime
output exposes progress, measured throughput, read/process/write latency,
per-Stage latency, queue depths, and current/peak in-flight buffers. Final data
is written to a same-directory temporary file, file-synced, atomically renamed,
and followed by a parent-directory fsync. Stage 11 still owns controlled
performance comparisons and large-file RSS acceptance; these Stage 10 metrics
are observations, not benchmark claims.

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
