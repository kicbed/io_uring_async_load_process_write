# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

Current status: Stage 1 coroutine learning demos are complete. Stage 2 is next:
Linux file I/O and the synchronous read-process-write baseline.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Current Executable

```bash
./build/asyncdataloader_stage0
./build/stage1_simple_task_demo
./build/stage1_manual_resume_demo
./build/stage1_delay_awaiter_demo
```
