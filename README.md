# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

Current status: Stage 7 is complete. The project now has a common read-side
`IOBackend`, concrete io_uring/thread-pool/synchronous
implementations, an automatic fallback factory, and the first processing
boundary: a caller-owned block can pass through registered stages in order.
Built-in no-op, per-block min-max normalization, and FNV-1a checksum stages are
available for composing and testing processing chains. A custom affine-stage
demo shows how callers can extend the processing interface outside the library.

The current backend demos are bounded learning components, not the final
read-process-write pipeline. Stage 8 will add BufferPool-backed ownership and
backpressure; stage metrics, three-stage overlap, and reliable output
integration remain planned work.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

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
