# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

Current status: Stage 9 is complete. The project now has bounded Counter,
Gauge, and Histogram primitives; a name-based MetricsRegistry with a unified
snapshot; RAII timing for registered CPU stages; and BufferPool instrumentation
for current and peak in-flight leases. Metrics may be updated concurrently
without becoming synchronization for the pipeline itself.

These components establish the observability foundation; they are not yet the
final read-process-write pipeline. Real read/process/write queue-depth and I/O
latency instrumentation require the Stage 10 topology. Terminal presentation,
optional JSON, controlled benchmarks, three-stage overlap, ordered reliable
output, and large-file acceptance tests remain planned work.

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
