# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

Current status: Stage 6 is complete. The project now has a common read-side
`IOBackend`, concrete io_uring/thread-pool/synchronous implementations, and an
automatic fallback factory. Stage 7, processing-stage registration and the
pipeline framework, is next.

The current backend demos are bounded learning components, not the final
read-process-write pipeline. BufferPool-backed backpressure, stage metrics,
three-stage overlap, and reliable output integration remain planned work.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

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
