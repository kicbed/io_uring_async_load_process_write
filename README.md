# AsyncDataLoader

AsyncDataLoader is a C++20/Linux systems programming project for offline large-file preprocessing.

The final project is a bounded-memory, observable read-process-write pipeline:

```text
read raw block -> CPU preprocessing stage -> write processed block
```

This repository is currently at Stage 0: environment and project skeleton.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Current Executable

```bash
./build/asyncdataloader_stage0
```
