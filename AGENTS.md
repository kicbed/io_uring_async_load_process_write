# AGENTS.md

## 1. Project Identity

This repository is **AsyncDataLoader**.

AsyncDataLoader is a C++20 / Linux systems programming project for offline large-file data preprocessing.

The core pipeline is:

```text
read raw data block -> CPU preprocessing stage -> write processed data block
```

The project is not a simple file loader.

It is a bounded-memory, observable, testable read-process-write pipeline.

The final system should demonstrate:

- asynchronous or fallback-based file I/O;
- CPU preprocessing stages;
- stage registration;
- buffer pooling;
- bounded memory;
- backpressure;
- metrics;
- benchmark results;
- correctness testing;
- clear interview-ready documentation.

---

## 2. User Learning Mode

The user is building this project to learn:

- modern C++20;
- CMake project organization;
- RAII;
- Linux file I/O;
- `pread`, `pwrite`, `fsync`, `errno`;
- C++20 coroutines;
- `io_uring`;
- async I/O backend abstraction;
- thread pool fallback;
- buffer ownership;
- bounded queues;
- pipeline design;
- metrics and benchmarking;
- engineering tradeoffs for interviews.

When helping the user, act as a **C++ systems programming coach**, not merely a code generator.

For every non-trivial change:

1. Explain the relevant existing code first.
2. Propose a small implementation plan.
3. Modify only the files needed for the current step.
4. Explain the diff after editing.
5. Explain every new class, function, important variable, ownership rule, and C++ concept.
6. Provide build, run, and test commands.
7. Explain compiler errors in Chinese.
8. Prefer correctness, clarity, and learning value over cleverness.

---

## 3. Required Project Documents

Before starting or continuing a stage, read these documents when available:

```text
docs/project_manual.md
docs/staged_prompts.txt
docs/anti_collapse_checklist.md
```

If these files are not present, ask the user to provide them or continue using the information already present in the repository.

The document priority is:

1. `docs/anti_collapse_checklist.md`
2. `AGENTS.md`
3. `docs/project_manual.md`
4. `docs/staged_prompts.txt`
5. Current user request

The anti-collapse constraints are mandatory.

Do not simplify the project in a way that violates them.

---

## 4. Non-Negotiable Invariants

Never intentionally remove or bypass these constraints.

### H1. Bounded Memory and Backpressure

Memory usage must be bounded by configuration.

The design must use a bounded buffer pool, bounded queues, or an equivalent mechanism.

The reader must not run infinitely ahead of processing or writing.

Forbidden examples:

```cpp
std::vector<Block> all_blocks;
read_entire_file_into_memory();
```

### H2. Streaming Design

The project must work on files much larger than memory.

The peak memory usage should be approximately:

```text
block_size * max_inflight_buffers + fixed overhead
```

Peak memory must not grow linearly with input file size.

### H3. Three-Stage Overlap

The final pipeline must overlap:

```text
read block i+1
process block i
write block i-1
```

A purely serial loop is allowed only as a baseline correctness oracle, not as the final pipeline.

Allowed baseline:

```text
for each block:
    read
    process
    write
```

Final pipeline must not stop there.

### H4. Real Processing Stage

The processing stage must eventually perform real CPU work.

NoOp or checksum-only processing is acceptable for early demos, but not as the final proof of the project.

Examples of acceptable processing:

- normalization;
- filtering;
- resampling;
- byte transformation;
- validation;
- format conversion;
- synthetic CPU-heavy transformation for benchmark purposes.

### H5. Clear Buffer Ownership

Every buffer must have a clear owner at every moment.

The expected lifecycle is:

```text
BufferPool -> reader -> processing stage -> writer -> BufferPool
```

Use RAII to return buffers to the pool.

Avoid:

- use-after-free;
- double-free;
- data race;
- dangling references;
- storing raw buffer pointers beyond their lifetime.

### H6. Correct Ordered Output and Reliable Persistence

Pipeline output must match the serial baseline.

Parallel or out-of-order processing must still write data to the correct output offset.

For reliable output, prefer:

```text
write to temporary file -> fsync -> rename
```

Never silently leave a corrupted final output file.

### H7. Error Handling

Handle these cases explicitly:

- EOF;
- short read;
- short write;
- file not found;
- permission denied;
- backend unavailable;
- `io_uring` unavailable;
- `O_DIRECT` alignment error;
- invalid config;
- interrupted system calls where relevant.

### H8. Metrics Are Required

Metrics are not decoration.

The project must eventually expose:

- read latency;
- process latency;
- write latency;
- throughput;
- queue depth;
- inflight buffers;
- memory high watermark;
- stage-level timing.

Do not delete metrics to make code shorter.

---

## 5. Forbidden Simplifications

Do not do these unless explicitly creating a baseline or negative example:

- Load the whole input file into memory.
- Store all blocks in a vector before processing.
- Use an unbounded producer queue.
- Let the reader run without downstream backpressure.
- Treat read-process-write serial loop as the final project.
- Delete BufferPool because `std::vector` is easier.
- Delete metrics because they are inconvenient.
- Claim coroutine itself improves performance.
- Claim io_uring is always faster.
- Invent benchmark numbers.
- Introduce CUDA, distributed systems, AI Infra, database storage, dashboard, or domain-specific file formats.

---

## 6. Stage Discipline

Only work on the current stage.

Do not implement future stages early unless the user explicitly asks.

The planned stages are:

- Stage 0: Environment and project skeleton
- Stage 1: C++20 coroutine learning
- Stage 2: Linux file I/O and sync baseline
- Stage 3: Benchmark framework and mmap baseline
- Stage 4: Native liburing basics
- Stage 5: io_uring + C++20 coroutine integration
- Stage 6: IOBackend abstraction and fallback
- Stage 7: Stage registration and pipeline framework
- Stage 8: BufferPool, PipelineConfig, and backpressure
- Stage 9: Metrics data structures and instrumentation
- Stage 10: End-to-end preprocessing pipeline demo
- Stage 11: Complete benchmark and performance analysis
- Stage 12: Terminal output and optional JSON metrics
- Stage 13: Error tests, documentation, and interview preparation

When a stage begins:

1. Identify the current stage.
2. Explain where this stage fits in the whole project.
3. State what will be implemented.
4. State what must not be implemented yet.
5. Propose the smallest runnable step.

---

## 7. Coding Style

Use:

- C++20;
- CMake;
- RAII;
- move-only ownership where appropriate;
- clear namespaces;
- small files;
- simple interfaces;
- explicit error handling;
- tests for non-trivial components.

Prefer clarity over template-heavy abstractions.

Avoid premature optimization.

Do not introduce third-party dependencies unless clearly justified.

---

## 8. Build and Test Commands

When possible, use these commands:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

For release or benchmark builds:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
```

For sanitizers, suggest separate builds such as:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Do not claim ASan or TSan safety unless the relevant tests were actually run.

---

## 9. Git Workflow

At the start of a task:

```bash
git status
```

After each completed stage, suggest a commit such as:

```bash
git add .
git commit -m "stage0: initialize project skeleton"
```

Use small commits.

Do not mix multiple stages in one commit.

---

## 10. Required End-of-Stage Summary

At the end of every stage, create or update:

```text
docs/stage_summaries/stageX_summary.md
```

The summary must include:

1. What was completed.
2. Current directory structure.
3. Added or modified files.
4. Purpose of each file.
5. Commands that currently work.
6. Current test results.
7. Bugs encountered and how they were fixed.
8. Remaining issues.
9. Next stage.
10. How to explain this stage in interviews.
11. Anti-collapse self-check.

The anti-collapse self-check must answer:

1. Did this stage introduce or break any hard constraint?
2. Is memory still bounded?
3. Can the project currently pass the large-file bounded-memory test, or which future stage will make it possible?
4. Who owns each buffer at each step?
5. Which acceptance tests are not ready yet, and when will they be addressed?

---

## 11. Review Mode

When asked to review, check for:

- stage creep;
- broken build;
- missing tests;
- unclear ownership;
- unbounded memory;
- unbounded queue;
- serial-only design;
- missing error handling;
- invented benchmark claims;
- overcomplicated C++;
- interview explanation risk.

Be strict.

Prefer pointing out risks early over producing a large but fragile implementation.

---

## 12. Communication Style

Use Chinese for explanations unless the user asks otherwise.

Code comments may be English or Chinese, but keep them concise.

When explaining C++ concepts:

1. Start from the project context.
2. Explain why this concept is needed here.
3. Show the relevant code.
4. Mention common bugs.
5. Give a short interview-ready explanation.

Do not hide uncertainty.

Do not claim something was tested unless a command was actually run.
