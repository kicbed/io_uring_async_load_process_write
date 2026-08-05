# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

Current status: Stage 8 is complete. The project now has validated pipeline
capacity settings, fixed-count aligned buffers, move-only RAII buffer leases,
a blocking thread-safe BufferPool, and a fixed-capacity SPSC handoff queue. A
backpressure demo shows that a producer waits when its downstream queue is full
and that every buffer returns to the pool after consumption.

These components establish bounded ownership and handoff; they are not yet the
final read-process-write pipeline. Stage 9 will add metrics data structures and
instrumentation. Three-stage overlap, ordered reliable output, and large-file
acceptance tests remain planned work.

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
