# Stage 7 Summary: Stage Registration and Pipeline Framework

Date: 2026-08-04

## 1. What Was Completed

Stage 7 introduced an extensible CPU-processing boundary for one
caller-owned block.

- Added the abstract, non-copyable `Stage` interface with `name()` and
  `process(std::span<std::byte>)`.
- Added a movable, uniquely owning `Pipeline` that stores
  `std::unique_ptr<Stage>` objects.
- Defined registration order as processing order.
- Rejects null registration with `std::invalid_argument`.
- Propagates stage exceptions and does not run later stages after a failure.
- Added `NoOpStage`, per-block min-max `NormalizeStage`, and FNV-1a
  `ChecksumStage`.
- Added a caller-defined `AffineStage` demo that transforms `[1,2,3]` into
  `[3,5,7]` using `output = input * 2 + 1`.
- Added focused tests for interface properties, ownership transfer,
  registration order, borrowed-buffer identity, error flow, built-in edge
  cases, checksum behavior, and custom-stage output.
- Updated README, design notes, and interview notes with Stage 7 behavior and
  its explicit engineering boundaries.

The framework processes an existing block synchronously. It does not yet
schedule blocks, read or write files, own block storage, collect metrics, or
claim three-stage overlap.

## 2. Current Relevant Directory Structure

```text
.
|-- CMakeLists.txt
|-- README.md
|-- include/
|   `-- pipeline/
|       |-- stage.h
|       |-- pipeline.h
|       `-- builtin_stages.h
|-- src/
|   `-- pipeline/
|       |-- pipeline.cpp
|       `-- builtin_stages.cpp
|-- examples/
|   `-- stage7_custom_stage_demo.cpp
|-- tests/
|   |-- stage7_pipeline_registration_test.cpp
|   |-- stage7_builtin_stages_test.cpp
|   `-- stage7_custom_stage_demo_test.cmake
`-- docs/
    |-- design.md
    |-- interview.md
    `-- stage_summaries/
        `-- stage7_summary.md
```

The Stage 0-6 sources, examples, and tests remain present and continue to be
built; the tree above highlights only the Stage 7 surface.

## 3. Added or Modified Files

- `CMakeLists.txt`
  - Builds the pipeline library, two C++ tests, custom-stage demo, and its
    CMake-driven output test.
- `README.md`
  - Marks Stage 7 complete, explains the current boundary, and shows how to
    run the custom-stage demo.
- `include/pipeline/stage.h`
  - Declares the abstract processing contract and borrowed-buffer rule.
- `include/pipeline/pipeline.h`
  - Declares stage registration, ordered execution, ownership, and stage
    count.
- `include/pipeline/builtin_stages.h`
  - Declares the no-op, normalization, and checksum strategies.
- `src/pipeline/pipeline.cpp`
  - Implements null rejection, ownership transfer, ordered dispatch, and
    stage count.
- `src/pipeline/builtin_stages.cpp`
  - Implements all three built-in processing behaviors.
- `examples/stage7_custom_stage_demo.cpp`
  - Defines an external affine stage and displays registration and data flow.
- `tests/stage7_pipeline_registration_test.cpp`
  - Verifies interface traits, stage destruction, move-only ownership,
    registration order, span identity, empty pipelines, and exception flow.
- `tests/stage7_builtin_stages_test.cpp`
  - Verifies names, normalization boundaries and edge cases, checksum values,
    non-mutation, and built-in composition.
- `tests/stage7_custom_stage_demo_test.cmake`
  - Verifies the demo's input, registered name, count, and transformed output.
- `docs/design.md`
  - Documents components, flow, ownership, algorithms, failure behavior,
    boundedness, and future boundaries.
- `docs/interview.md`
  - Records interview-ready explanations of polymorphism, unique ownership,
    spans, ordering, state, and limitations.
- `docs/stage_summaries/stage7_summary.md`
  - Records this handoff, validation, remaining work, and anti-collapse audit.

## 4. Purpose and Data Flow

### Stage object registration

```text
caller creates unique_ptr<ConcreteStage>
  -> Pipeline::add_stage(std::move(stage))
       -> validate non-null
       -> Pipeline owns Stage for its remaining lifetime
```

### One-block processing

```text
caller-owned bytes
  -> borrowed span
  -> registered Stage 0
  -> registered Stage 1
  -> registered Stage 2
  -> same caller-owned bytes
```

### Visible custom-stage example

```text
input [1, 2, 3]
  -> AffineStage(multiplier=2, offset=1)
  -> output [3, 5, 7]
```

`Pipeline` owns behavior objects; it does not own the supplied block. Every
Stage may borrow and modify the span during its call but must not retain the
view or underlying pointer.

## 5. Commands That Currently Work

Configure, build, and run all Debug tests:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the custom-stage demo and focused Stage 7 tests:

```bash
./build/stage7_custom_stage_demo
ctest --test-dir build -R '^stage7_' --output-on-failure
```

Configure, build, and run all Release tests:

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j
ctest --test-dir build-release --output-on-failure
```

Build and test Stage 7 with warnings and ASan/UBSan:

```bash
cmake -S . -B build-stage7-sanitized \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-stage7-sanitized \
  --target stage7_pipeline_registration_test \
           stage7_builtin_stages_test \
           stage7_custom_stage_demo \
  -j
ctest --test-dir build-stage7-sanitized \
  -R '^stage7_' \
  --output-on-failure
```

## 6. Current Test Results

Verified on 2026-08-04:

- Debug configure and full build: passed.
- Debug custom-stage demo: produced `[1,2,3] -> [3,5,7]` as expected.
- Debug focused Stage 7 CTest: 3/3 passed, 0 failed.
- Debug full CTest: 28/28 passed, 0 failed.
- Release configure and full build: passed.
- Release full CTest: 28/28 passed, 0 failed.
- Warning-enabled ASan/UBSan Stage 7 build: passed without compiler warnings.
- Warning-enabled ASan/UBSan Stage 7 CTest: 3/3 passed, 0 failed.
- TSan was not run for Stage 7; no TSan-safety claim is made.
- No performance benchmark was run; no Stage 7 performance claim is made.

## 7. Bugs Encountered and How They Were Fixed

- **A pre-existing sanitizer build directory initially lacked the new Stage 7
  target graph.**
  - CMake was rerun to regenerate that build tree before building the new
    targets. No source workaround was required.
- **Normalization needs defined behavior for an empty or constant block.**
  - Empty input returns unchanged; a constant block becomes all zeroes so the
    implementation never divides by zero.
- **A stateful checksum stage creates a concurrency boundary.**
  - The header and design notes explicitly state that `last_checksum_` is
    overwritten per call and is not synchronized. Stage 7 remains sequential;
    later parallel design must not silently share one instance.
- **In-place failure cannot promise automatic rollback.**
  - Exception tests and documentation specify that later stages stop, while
    modifications already made by earlier stages remain visible.

## 8. Remaining Issues and Explicit Boundaries

- There is no BufferPool or RAII buffer lease yet.
- There are no bounded inter-stage queues or full-pipeline backpressure yet.
- `IOBackend` is not connected to `Pipeline` yet.
- Processing is a synchronous call over one caller-owned block; there is no
  processor worker pool or read/process/write overlap.
- `ChecksumStage` is not safe for simultaneous calls on one instance.
- There is no common writer backend, ordered completion coordinator, temporary
  output file, `fsync`, or atomic rename integration yet.
- There are no pipeline metrics or stage timing measurements yet.
- `docs/project_manual.md` is absent; the repository currently contains
  `docs/project_manual.docx` instead.
- The demo proves extension and processing semantics, not large-file
  throughput or final-pipeline correctness.

## 9. Next Stage

Stage 8 is **BufferPool, PipelineConfig, and Backpressure**.

Its smallest runnable delivery should introduce a fixed-capacity pool with an
RAII lease, configuration validation, and a test showing that attempting to
acquire beyond capacity waits or reports bounded exhaustion according to the
chosen contract. It must not skip directly to the complete Stage 10 pipeline.

Stage 8 will change the block owner from demo-local storage to a pool lease,
while the Stage 7 `std::span` contract remains a non-owning view over that
leased storage.

## 10. How to Explain This Stage in Interviews

Short version:

> I introduced a polymorphic Stage strategy and a Pipeline that owns stages
> with unique_ptr and invokes them in registration order over a borrowed span.
> The caller retains buffer ownership, stages cannot retain the view, and the
> built-ins include a real constant-space normalization transform. A custom
> affine stage proves external extensibility. This establishes the CPU
> processing boundary; BufferPool backpressure and end-to-end overlap remain
> explicit later stages.

Important engineering distinctions:

- `unique_ptr` owns each stage; `span` only borrows block storage.
- Registration order is deterministic dataflow order.
- Virtual dispatch supports configurable, stateful processing strategies.
- NoOp is a control and checksum is an observer; neither alone proves final
  preprocessing.
- Stage registration improves extensibility, not I/O performance by itself.

## 11. Anti-Collapse Self-Check

### Did this stage introduce or break any hard constraint?

No. Stage 7 added no whole-file read, all-block vector, unbounded queue,
reader, writer, invented benchmark value, or claim that the synchronous demo
is the final pipeline. Existing BufferPool, backpressure, metrics, and reliable
persistence mechanisms were not deleted; they have not been implemented yet
and retain their planned stages.

### Is memory still bounded?

For Stage 7's current scope, yes: each call uses one caller-owned block,
built-in transforms use constant extra space, and stage-object memory grows
only with the explicitly registered stage count. Nothing grows with input file
size inside this component.

This is not yet an end-to-end memory guarantee because there is no streaming
producer. Stage 8 must bound the number and size of all in-flight buffers and
handoff queues.

### Can the project currently pass the large-file bounded-memory test?

No. Stage 7 proves bounded one-block processing, not a 50 GiB streaming path.
Stage 8 supplies the BufferPool/backpressure mechanism, Stage 10 integrates
read/process/write streaming, and Stages 11/13 measure and verify the final
large-file acceptance case.

### Who owns each buffer at each step?

- In Stage 7 tests and the demo, the caller owns its `std::array` or
  `std::vector` storage.
- `Pipeline::process()` and each `Stage::process()` borrow a mutable
  `std::span<std::byte>` for the duration of the call.
- No stage may retain the span or raw pointer afterward.
- `Pipeline` owns Stage objects, not the bytes they process.
- Stage 8 will introduce a pool-owned allocation and RAII lease. Stages 8-10
  will establish the final lifecycle:

  ```text
  BufferPool -> reader -> processor -> writer -> BufferPool
  ```

### Which acceptance tests are not ready, and when will they be addressed?

- T1/T1b bounded large-file memory: Stage 8 mechanism, Stage 10 integration,
  Stages 11/13 verification.
- T2 backpressure comparison: Stage 8 implementation and Stage 11
  measurement.
- T3 three-stage overlap: Stage 10 implementation and Stage 11 evidence.
- T4/T5 ordered pipeline output: Stage 10 implementation and Stage 13 tests.
- T6 crash-safe final output: writer integration followed by Stage 13
  validation.
- T7 full-pipeline ASan/TSan: Stage 13. Stage 7 has focused ASan/UBSan only.
- T8 configured io_uring fallback: already covered in Stage 6; compile-time
  liburing absence remains unresolved.
- T9 real CPU processing: Stage 7 provides normalization behavior; Stage 10
  must prove it in the end-to-end pipeline and Stage 11 must measure it without
  inventing results.
