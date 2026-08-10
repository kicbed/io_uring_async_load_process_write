# Design Notes

## Project Invariants

The final design must preserve:

- bounded memory;
- backpressure;
- streaming read-process-write flow;
- clear buffer ownership;
- reliable ordered output;
- backend fallback;
- metrics and benchmark honesty.

Stage 5 does not implement the final pipeline. It establishes the coroutine and
`io_uring` completion bridge that later backends and pipeline stages will reuse.

## Stage 5: `io_uring` and Coroutine Integration

### Scope

Stage 5 proves one complete asynchronous read lifecycle:

```text
coroutine submits an SQE
  -> coroutine suspends
  -> the kernel completes the read
  -> user space receives a CQE
  -> the saved coroutine handle resumes
  -> the read result flows through `co_await`
```

The current `UringContext` is a small, single-threaded learning context. It owns
the ring, creates `ReadAwaiter` objects, and waits for one completion. The
uniform `IOBackend` interface, concrete backend selection, and fallback policy
belong to Stage 6.

### Components and Responsibilities

| Component | Responsibility |
| --- | --- |
| `Task<T>` | Owns the coroutine frame, starts it, exposes completion state, and returns the promise result. |
| `Task<T>::promise_type` | Stores the returned value or an unhandled exception. |
| `ReadAwaiter` | Describes one read, prepares and submits its SQE, and converts the completion result. |
| `CompletionRequest` | Stores one coroutine continuation and one CQE-style result, then resumes exactly once. |
| `UringContext` | Owns `io_uring`, creates read awaiters, waits for CQEs, and dispatches completions. |
| `async_read_demo` | Demonstrates the complete `co_await read_at` path with a fixed 4 KiB buffer. |

### What `co_return co_await` Means

The demo coroutine contains:

```cpp
co_return co_await context.read_at(fd, buffer, size, offset);
```

It can be read as two operations:

```cpp
const std::size_t bytes_read =
    co_await context.read_at(fd, buffer, size, offset);
co_return bytes_read;
```

`co_await` operates on the `ReadAwaiter` returned by `read_at()`. The compiler
uses its `await_ready()`, `await_suspend()`, and `await_resume()` methods.
`co_return` runs only after the await completes and passes the byte count to
`promise_type::return_value()`.

`Task<T>` manages the whole coroutine and its final result. It does not submit
I/O. `ReadAwaiter` is the object that connects this particular suspension point
to `io_uring`.

### Submission, Suspension, and Resumption

```text
main
  |
  | async_read(...)
  |   create coroutine frame and promise
  |   initial_suspend()
  v
Task<std::size_t>
  |
  | start() -> coroutine_handle::resume()
  v
async_read coroutine
  |
  | co_await UringContext::read_at(...)
  v
ReadAwaiter::await_suspend(current coroutine handle)
  |
  | get SQE
  | prepare read(fd, buffer, byte_count, offset)
  | SQE.user_data = &request_
  | submit SQE
  | request_.continuation = current coroutine handle
  | return true
  v
coroutine remains suspended; Task::start() returns to main
  |
  | UringContext::wait_one()
  v
CQE arrives
  |
  | recover CompletionRequest from CQE.user_data
  | copy cqe->res
  | mark CQE seen
  | request->complete(cqe->res)
  v
CompletionRequest::complete()
  |
  | store result
  | clear the saved member handle
  | resume the local handle exactly once
  v
ReadAwaiter::await_resume()
  |
  | non-negative result -> byte count
  | negative result -> std::system_error
  v
co_return -> promise_type -> Task::result()
```

`io_uring_submit()` does not suspend a C++ coroutine. The suspension decision is
the `bool` returned from `ReadAwaiter::await_suspend()`:

- `true` means that an external completion path is responsible for resuming the
  coroutine later;
- `false` means that no future CQE will resume this await operation, so execution
  continues immediately into `await_resume()`.

The current implementation uses the `false` path for immediate setup or submit
failures. It stores a negative result first, then lets `await_resume()` translate
that result into a `std::system_error`.

### Request Identity

The kernel does not understand C++ coroutine handles. The bridge uses two
application-owned links:

```text
SQE.user_data -> CompletionRequest
CompletionRequest.continuation_ -> coroutine frame
```

When a CQE arrives, `io_uring_cqe_get_data()` recovers the
`CompletionRequest`. The request stores the result and calls the saved
continuation's `resume()` method.

### Ownership and Lifetime

| Object | Owner | Borrowers and required lifetime |
| --- | --- | --- |
| Coroutine frame | `Task<T>` | `CompletionRequest` borrows its handle until completion. |
| `ReadAwaiter` | Coroutine frame while the `co_await` is active | Its address-dependent state must remain stable across suspension. |
| `CompletionRequest` | Embedded in `ReadAwaiter` | SQE/CQE `user_data` borrows its address until the CQE is consumed. |
| Read buffer | The vector in `async_read_demo::main()` | The kernel borrows `buffer.data()` until completion. |
| File descriptor | `FdGuard` returned by `open_read_only()` | The in-flight read borrows the raw descriptor. |
| Ring | `UringContext` | `ReadAwaiter` borrows it while preparing and submitting the operation. |

`ReadAwaiter` and `CompletionRequest` are non-copyable and non-movable because
the kernel returns the exact `CompletionRequest` address stored in
`user_data`. Moving either object could make that address dangle.

`CompletionRequest::complete()` moves the saved continuation to a local handle
before resuming it. Resumption may finish the coroutine and change object
lifetimes, so the completion path does not access request members afterward.
`UringContext::wait_one()` similarly copies `cqe->res` and marks the CQE seen
before it resumes the coroutine.

### Result and Error Flow

```text
successful CQE: cqe->res >= 0
  -> CompletionRequest
  -> ReadAwaiter::await_resume()
  -> byte count (zero means EOF)
  -> promise_type::return_value()
  -> Task::result()

failed CQE: cqe->res < 0
  -> CompletionRequest
  -> ReadAwaiter::await_resume()
  -> std::system_error(-cqe->res)
  -> promise_type::unhandled_exception()
  -> Task::result() rethrows
```

A positive result smaller than the requested size is a valid short read. A zero
result is EOF. A negative CQE result is `-errno`; the completion path must not
read the process-global `errno` to decode it.

### Current Boundary

The teaching context has no concurrent CQ poller. It submits from
`await_suspend()`, stores the continuation, returns to `main()`, and only then
does `main()` call `wait_one()`. This ordering is not a general multi-threaded
completion design. A future concurrent event loop must explicitly handle a CQE
racing with continuation registration.

This stage proves API correctness and lifecycle discipline only. It does not
claim that coroutines themselves improve performance or that `io_uring` is
always the fastest backend.

## Stage 6: `IOBackend` Abstraction and Fallback

### Scope

Stage 6 puts the Stage 5 coroutine read path behind one runtime-polymorphic
interface and supplies two fallback implementations:

```text
BackendConfig
  -> BackendFactory
       -> UringBackend
       -> ThreadPoolBackend
       -> SyncBackend
  -> IOBackend::read_at()
  -> Task<std::size_t>
```

This stage unifies read-side control flow. It does not yet define a common
asynchronous write operation, processing stages, a BufferPool, inter-stage
queues, metrics, or the final pipeline scheduler.

### Common Backend Contract

`IOBackend` exposes three virtual operations:

| Operation | Contract |
| --- | --- |
| `name()` | Returns the selected implementation name for diagnostics. |
| `read_at(fd, buffer, offset)` | Returns a lazy `Task<std::size_t>` for one positional read. |
| `wait_one()` | Advances one pending asynchronous completion when a started task did not complete immediately. |

The caller starts and consumes every backend in the same way:

```cpp
auto task = backend.read_at(fd, buffer, offset);
task.start();
if (!task.done()) {
    backend.wait_one();
}
const std::size_t bytes_read = task.result();
```

The returned byte count has identical meaning for every implementation:

- a full or short non-zero result is a successful read;
- zero bytes means EOF;
- an operation failure reaches `Task::result()` as `std::system_error`.

### Concrete Backend Semantics

| Backend | Where the read runs | Completion behavior |
| --- | --- | --- |
| `SyncBackend` | Calling thread, using blocking `pread()` | Completes during `Task::start()`; `wait_one()` is invalid. |
| `UringBackend` | Kernel io_uring request path | Suspends after SQE submission; `wait_one()` consumes a CQE and resumes the task. |
| `ThreadPoolBackend` | One of a fixed number of worker threads, using blocking `pread()` | Suspends after bounded queue submission; `wait_one()` consumes a published completion and resumes the task. |

Coroutines provide a common suspension and result interface. They do not turn
blocking `pread()` into kernel asynchronous I/O and are not themselves a
performance optimization.

### Thread-Pool Request Flow

The fallback thread pool uses a fixed worker set and two bounded vectors:

```text
calling thread
  -> ReadAwaiter::await_suspend(handle)
  -> save continuation in ReadRequest
  -> bounded work_queue
  -> fixed worker executes blocking pread()
  -> completion_queue
  -> calling thread invokes wait_one()
  -> CompletionRequest::complete(result)
  -> resume saved coroutine
  -> ReadAwaiter::await_resume()
```

`inflight_count_` covers queued, actively executing, and completed-but-not-yet-
consumed requests. Submission at `max_inflight` fails immediately with
`EAGAIN`. Both request vectors reserve `max_inflight` entries, and no accepted
path pushes beyond that limit.

Workers do not resume coroutines directly. They only publish completion state.
`wait_one()` resumes on the calling thread, keeping continuation execution out
of the I/O worker and matching the current caller-driven io_uring completion
model.

### Factory and Fallback Policy

`BackendFactory` combines factory and strategy patterns. It returns a
`std::unique_ptr<IOBackend>` whose dynamic type is selected from
`BackendConfig`.

Explicit selection is fail-fast:

```text
Uring      -> create UringBackend or propagate the error
ThreadPool -> create ThreadPoolBackend or propagate the error
Sync       -> create SyncBackend
```

Only `BackendKind::Auto` enables fallback:

```text
try UringBackend
  -> initialization std::system_error: try ThreadPoolBackend
       -> worker initialization std::system_error: use SyncBackend
```

The factory catches only construction-time `std::system_error`. Invalid
configuration, allocation failure, and unknown enum values are not silently
hidden. An error from a submitted `read_at()` is also not retried through a
different backend: a bad file descriptor or permission error is an operation
failure, not evidence that the backend is unavailable. This distinction will
also prevent duplicate side effects when write support is added later.

The demo exposes deterministic `--disable-uring` and
`--disable-thread-pool` switches for Auto-mode tests. They remove a candidate
from the selection chain; they do not alter explicit backend requests.

### Ownership and Lifetime

For one backend read, ownership is:

| Object | Owner | Borrower |
| --- | --- | --- |
| Concrete backend | `std::unique_ptr<IOBackend>` in the caller | Active task/awaiter uses the backend until completion. |
| Coroutine frame | Caller-owned `Task<std::size_t>` | `CompletionRequest` stores a non-owning continuation handle. |
| Buffer | Caller | Sync read, kernel, or worker borrows its address until completion. |
| File descriptor | Caller-owned `FdGuard` | Selected backend borrows the integer descriptor until completion. |
| Thread-pool request | Awaiter inside the coroutine frame | Work and completion queues temporarily borrow its stable pointer. |

The required lifetime ordering is:

```text
start read
  -> keep fd + buffer + backend + Task alive
  -> consume completion
  -> obtain Task result
  -> destroy Task before backend/buffer/fd
```

The fallback demo declares its objects so normal reverse destruction follows
that order. Stage 8 will replace the demo-owned fixed buffer with an RAII
BufferPool handle.

### Boundedness and Backpressure Boundary

The fallback demo reads one fixed 4 KiB block. `ThreadPoolBackend` bounds its
accepted requests with `max_inflight`; it never creates one thread per
coroutine and never uses an unbounded work queue.

This is a backend-local capacity limit, not the final pipeline backpressure
mechanism. A full producer must wait and retry when capacity becomes available
instead of treating `EAGAIN` as permanent failure. BufferPool ownership and
bounded inter-stage queues belong to Stage 8, and read/process/write overlap
belongs to Stage 10.

### Current Boundaries

- The common interface currently covers positional reads only.
- `wait_one()` is a caller-driven one-completion API, not a general event loop.
- CMake currently requires the liburing development package at build time;
  runtime ring-initialization failure can fall back, but a no-liburing build is
  not yet supported.
- Auto mode exposes the selected backend, but does not retain the original
  initialization error as structured diagnostic data.
- There is no claim that io_uring is universally faster than the alternatives.
- The current demos are not the final processing pipeline.

## Stage 7: Stage Registration and Pipeline Framework

### Scope

Stage 7 defines the CPU-processing boundary between the read-side backend and
the future writer. It answers two questions:

1. What interface must a processing step implement?
2. How can several processing steps be registered and run in a deterministic
   order over one caller-owned block?

The stage is deliberately independent of file I/O. It does not yet schedule
blocks, own buffers, create worker threads, record metrics, or write output.
Those responsibilities remain in later stages.

### Components and Responsibilities

| Component | Responsibility |
| --- | --- |
| `Stage` | Abstract processing contract: expose a name and process one borrowed mutable byte span. |
| `Pipeline` | Own registered stage objects and invoke them in registration order. |
| `NoOpStage` | Leave the block unchanged; useful as a control and composition test. |
| `NormalizeStage` | Perform real per-block min-max normalization into the byte range `[0, 255]`. |
| `ChecksumStage` | Compute the current block's 64-bit FNV-1a checksum without changing the block. |
| `AffineStage` demo | Show that caller code can add a new stage without modifying the library. |

### Registration and Processing Flow

Registration transfers unique ownership of each concrete stage into the
pipeline:

```text
make_unique<ConcreteStage>()
  -> Pipeline::add_stage(unique_ptr<Stage>)
       -> reject null
       -> move into vector<unique_ptr<Stage>>
```

Processing borrows one block and uses virtual dispatch for each registered
object:

```text
caller-owned mutable block
  -> Stage 0::process(span)
  -> Stage 1::process(span)
  -> Stage 2::process(span)
  -> same caller-owned block
```

The custom demo makes the data change visible:

```text
[1, 2, 3]
  -> AffineStage: output = input * 2 + 1
  -> [3, 5, 7]
```

Registration order is execution order. The pipeline stores objects rather
than function pointers so that stages can have configuration and state while
sharing one polymorphic interface.

### Ownership and Lifetime

`Pipeline` owns every registered `Stage` through `std::unique_ptr<Stage>`.
Calling `add_stage(std::move(stage))` is an explicit ownership transfer; the
caller's pointer becomes empty after the move. A virtual destructor on `Stage`
ensures deleting a concrete object through the base pointer runs the complete
destructor chain.

The data block follows a different rule:

| Object | Owner | Borrower |
| --- | --- | --- |
| Pipeline | Caller | Call stack during `process()`. |
| Registered stage | `Pipeline` | No external borrower is retained. |
| Byte storage | Caller | Each stage borrows it through `std::span<std::byte>` only for the current call. |

`std::span` contains a pointer and a length; it does not own or copy the
bytes. A stage may change bytes in place but must not retain the span or its
data pointer after `process()` returns. The caller therefore controls storage
lifetime now, while Stage 8 will make that caller an RAII BufferPool handle.

### Built-In Stage Semantics

`NoOpStage` performs no work and preserves all bytes. It is a test/control
stage, not evidence of final preprocessing.

`NormalizeStage` scans one block for its minimum and maximum and then maps each
byte with integer arithmetic:

```text
normalized = (value - minimum) * 255 / (maximum - minimum)
```

An empty block is unchanged. If every byte is equal, the block becomes all
zeroes, avoiding division by zero. The operation uses constant extra memory
and constitutes a real byte transformation, although end-to-end processing
proof still requires its later integration into the streaming pipeline.

`ChecksumStage` computes FNV-1a into `last_checksum_` and leaves the input
unchanged. The value is replaced on every call. A single instance is not safe
for simultaneous calls because its state is unsynchronized; a future parallel
processor must choose per-worker instances or redesign result ownership rather
than silently sharing it.

### Error and Partial-Update Semantics

- `Pipeline::add_stage(nullptr)` throws `std::invalid_argument`.
- If a stage throws, the exception propagates to the caller.
- Later stages are not invoked after that failure.
- Changes made by earlier stages remain in the caller's block; Stage 7 does
  not provide transactional rollback.

These rules keep failures visible and avoid pretending that in-place mutation
can be automatically undone.

### Boundedness and Backpressure Boundary

Stage 7 processes exactly the span supplied by its caller and allocates no
second copy of that block. The built-in transforms use constant extra space.
The stage list uses memory proportional to the configured number of stages,
not the input file size.

This framework alone does not bound how many blocks an external producer may
create. Stage 8 must supply a fixed-capacity BufferPool and bounded handoff so
the reader waits when downstream work is full. Stage 10 will then integrate
the stage chain into overlapping read/process/write execution.

### Current Boundaries

- Processing is synchronous and single-call; there is no processor thread or
  inter-stage queue yet.
- The framework operates on one caller-owned block and never reads a whole
  file.
- There is no backend-to-pipeline connection or writer integration yet.
- There are no stage latency or throughput metrics yet.
- Ordered output, reliable temporary-file persistence, `fsync`, and rename
  remain later-stage work.
- No performance claim is made from stage registration or virtual dispatch.

## Stage 8: Bounded Buffer Ownership and Backpressure

### Components

| Component | Responsibility |
| --- | --- |
| `PipelineConfig` | Validate block size, pool capacity, queue depth, alignment, and pool-size overflow. |
| `AlignedBuffer` | Own and free one `posix_memalign()` allocation. |
| `AlignedBufferPool` | Own a fixed number of aligned buffers and block acquisition when all leases are active. |
| `BufferHandle` | Represent one move-only lease and return its slot to the pool on destruction. |
| `SPSCQueue<T>` | Transfer move-only work FIFO between one producer and one consumer with fixed capacity. |

### Ownership and Handoff

The pool always owns the physical allocations. A handle owns only the right to
use one indexed slot:

```text
pool free list
  -> acquire handle
  -> producer fills pool-owned bytes
  -> queue slot owns moved handle
  -> consumer pops moved handle
  -> handle destructor returns index to pool free list
```

Moving a handle changes lease ownership without copying block bytes. A
moved-from handle has no pool pointer, so its destructor performs no second
return.

### Two Bounded Layers

The BufferPool and queue are complementary:

- `max_inflight_buffers` bounds physical block memory across the system;
- `queue_depth` bounds the backlog between two adjacent stages.

Pool exhaustion blocks `acquire()`. Queue exhaustion blocks `push()`. Both
wait using a condition variable predicate, which releases the mutex while the
thread sleeps and rechecks state after wakeup.

`SPSCQueue` uses a fixed vector of optional slots with `head_`, `tail_`, and
`size_`. Reusing indices as they wrap avoids an unbounded container. This first
version is mutex-based rather than lock-free; correctness is the Stage 8 goal.

### Lifetime Boundary

The pool must outlive all handles. The queue must outlive its producer and
consumer, and waiting threads must be stopped and joined before either object
is destroyed. The Stage 8 demo uses a fixed item count, so it does not yet need
an end-of-stream, close, cancellation, or cross-thread error protocol. Those
protocols must be designed before the Stage 10 end-to-end pipeline.

### `O_DIRECT` Boundary

Aligned allocation is necessary but not sufficient for direct I/O. Buffer
address, request length, and file offset may each have filesystem-specific
alignment requirements. See `docs/stage8_odirect_alignment.md` for the exact
Stage 8 guarantee and remaining runtime checks.

### Current Boundaries

- The demo uses synthetic byte markers, not real file I/O or CPU processing.
- There is one handoff queue, not the final read/process and process/write
  pair.
- There are no queue-depth, wait-time, or memory-high-watermark metrics yet.
- Ordered output, temporary-file persistence, `fsync`, and rename are not
  implemented here.
- No throughput or direct-I/O performance claim is made.

## Stage 9: Bounded Metrics and Automatic Instrumentation

### Scope

Stage 9 turns important runtime events into small, queryable metric objects:

```text
pipeline event
  -> Counter / Gauge / Histogram atomic update
  -> MetricsRegistry::snapshot()
  -> independent reporting data
  -> future terminal or JSON formatter
```

It establishes the data and instrumentation layer only. It does not create the
Stage 10 read/process/write scheduler, infer throughput from invented timings,
or add a dashboard.

### Components and Responsibilities

| Component | Responsibility |
| --- | --- |
| `Counter` | Record a monotonically increasing event total. |
| `Gauge` | Record a signed current value and its lifetime high watermark. |
| `Histogram` | Place samples into at most 32 finite buckets plus one overflow bucket, while also recording count and sum. |
| `MetricsRegistry` | Own at most 64 globally unique named metrics and produce one unified snapshot. |
| `ScopedTimer` | Measure one scope with `steady_clock` and record nanoseconds during RAII destruction. |
| Instrumented `Pipeline` | Register and update one latency histogram per CPU stage. |
| Instrumented `AlignedBufferPool` | Track current and peak active BufferHandle leases. |

The three primitive types answer different questions:

```text
Counter:   how many events have happened?
Gauge:     how many items exist right now, and what was the peak?
Histogram: how are many latency samples distributed?
```

### Registry Ownership and Reporting Flow

The registry is an ordinary injected object rather than a singleton:

```text
application owns MetricsRegistry
  |-- owns Counter / Gauge / Histogram objects through unique_ptr
  |-- Pipeline borrows stable Histogram pointers
  `-- AlignedBufferPool borrows one dedicated Gauge pointer

worker events update borrowed metric objects
reporter asks registry for a copied Snapshot
```

Registration must finish before worker threads begin. Registration itself is
not concurrent, but the heap-owned metric objects keep stable addresses even
if an entry vector reallocates. The registry must outlive every Pipeline or
BufferPool that borrows one of its metrics.

`snapshot()` copies metric names, current numeric values, histogram bounds, and
bucket totals. The copy is independent: later metric updates cannot mutate an
older snapshot. This is the terminal-output foundation because a presenter no
longer has to know which component owns each live metric.

The snapshot is deliberately non-transactional. Its fields are separate atomic
loads, so concurrent updates can occur between two reads. After workers have
quiesced, the snapshot is exact; while they are active, it is a low-overhead
operational view rather than one globally frozen instant.

### Histogram Bucket Semantics

Finite upper bounds are strictly increasing. Each finite bucket includes its
upper bound, and the final active bucket contains samples above every finite
bound. For bounds `[20, 50]`:

```text
bucket 0: sample <= 20
bucket 1: 20 < sample <= 50
bucket 2: sample > 50        (overflow)
```

`std::lower_bound()` finds the first upper bound that is not less than the
sample. If it returns `end()`, its distance from `begin()` equals the number of
finite bounds, which is exactly the overflow-bucket index. Updating the bucket
preserves the distribution; updating only total count and sum would lose the
ability to distinguish consistently fast work from a mixture of very fast and
very slow work.

### Automatic CPU Stage Timing

An instrumented Pipeline receives a borrowed `MetricsRegistry` in its
constructor. When a stage is registered, the Pipeline creates a histogram named

```text
stage.<registration-index>.<stage-name>.latency_ns
```

For every `process()` call, a `ScopedTimer` surrounds that stage only:

```text
create timer
  -> Stage::process(block)
  -> timer destructor observes elapsed nanoseconds
  -> next stage
```

`std::chrono::steady_clock` is monotonic, so wall-clock adjustments cannot make
an elapsed duration run backward. RAII also records the elapsed time if a stage
throws: stack unwinding destroys the timer before the exception reaches the
caller, while later stages remain skipped as before.

The default Pipeline constructor remains available for Stage 7 callers that do
not supply a registry. Instrumentation therefore changes observation, not the
data-processing contract.

### BufferPool In-Flight Instrumentation

An optionally instrumented `AlignedBufferPool` borrows a dedicated Gauge that
must begin with both current value and high watermark at zero:

```text
successful acquire
  -> slot changes free -> leased
  -> Gauge increment

move BufferHandle
  -> lease owner changes
  -> Gauge unchanged

BufferHandle destruction or replacement
  -> slot changes leased -> free
  -> Gauge decrement
```

A failed `try_acquire()` creates no lease and therefore performs no increment.
The pool serializes free/leased transitions with its existing mutex, so the
Gauge follows the same state transition that changes the pool. The metric does
not own the buffer, release it, or provide backpressure; BufferHandle RAII and
the pool condition variable retain those responsibilities.

### Concurrency and Boundedness

Metric numeric fields use atomic operations so several workers can report
without a metric-level data race. Relaxed memory ordering is sufficient because
metrics are observations: no pipeline thread may use a metric update to publish
or acquire a buffer. Actual handoff synchronization still belongs to the pool,
queue, backend, and future scheduler.

Observability is bounded by explicit limits:

- one registry owns at most 64 metrics;
- each metric name is at most 128 bytes;
- one histogram owns at most 32 finite buckets plus overflow;
- snapshots copy only the registered bounded set;
- no unbounded label map or per-block sample vector is used.

These allocations are independent of input-file size. Buffer payload memory
remains bounded by the Stage 8 pool configuration.

### Current Boundaries

- Automatic instrumentation currently covers CPU-stage latency and active
  BufferPool leases.
- Counter, Gauge, Histogram, and registry snapshots are ready for read/write
  latency, processed-block totals, queue depth, and throughput inputs, but the
  real events do not exist until the Stage 10 topology is assembled.
- The Stage 8 teaching queue is not presented as the final pair of
  read-to-process and process-to-write queues, so Stage 9 does not give it
  misleading production metric names.
- A snapshot provides reporting data; Stage 12 now consumes it for terminal
  refreshing and optional JSON without changing this Stage 9 ownership model.
- Metrics do not prove read/process/write overlap, ordered output, reliable
  persistence, bounded large-file RSS, or performance improvement.
- No benchmark number is inferred or reported in this stage.

## Stage 10: End-to-End Preprocessing Pipeline

### Scope and Topology

Stage 10 connects the components built in Stages 6-9 into one runnable
bounded-memory file transformation:

```text
BackendFactory -> IOBackend
                     |
reader thread:  BufferPool::acquire -> read_at(offset)
                     |
                     v
              read_to_process queue
                     |
processor thread: Pipeline::process(valid_data)
                     |
                     v
              process_to_write queue
                     |
writer thread:  pwrite(valid_data, offset)
                     |
                     v
              BufferHandle destructor -> BufferPool
```

The three worker threads are started consumer-first. Once warmed up, the
reader may read block `i+1` while the processor transforms block `i` and the
writer persists block `i-1`. This is real stage overlap, although Stage 11 must
still measure how much it helps on controlled workloads.

### Work Item and Ownership

`BlockWorkItem` is the envelope for one logical block. Its fields answer four
separate questions:

| Field | Meaning |
| --- | --- |
| `block_index` | Which logical block this is, for identity and diagnostics. |
| `file_offset` | Where these bytes belong in the output file. |
| `valid_bytes` | How much of the fixed-capacity buffer was actually read. |
| `BufferHandle` | The unique RAII lease that grants access to the pool slot. |

Only `valid_data()` is processed and written. This matters for the final block:
a 1 MiB buffer may contain only 37 valid bytes at EOF, and stale capacity bytes
must never reach either CPU processing or output.

The item is move-only because its handle is move-only. Queue `push()` and
`pop()` transfer the item instead of copying its bytes:

```text
pool owns allocation
  -> reader-local BufferHandle owns lease
  -> read queue slot owns moved BlockWorkItem
  -> processor-local optional owns it
  -> write queue slot owns it
  -> writer-local optional owns it
  -> destruction returns lease to pool
```

At no point are two objects allowed to own the same lease.

### Bounded Queues, EOF, and Failure

Both handoff queues are fixed-capacity SPSC rings. A full `push()` blocks the
upstream stage, so the reader cannot run indefinitely ahead of a slow
processor or writer. Physical payload memory remains bounded by:

```text
block_size * max_inflight_buffers
```

plus fixed queue, metric, thread, and allocator overhead.

`close()` means normal end-of-stream: queued values remain available and the
consumer receives `nullopt` only after draining them. `fail(exception_ptr)`
means abnormal termination: it preserves the first exception, destroys queued
items immediately, returns their buffer leases through RAII, and wakes blocked
producers and consumers. Every worker is wrapped by the same failure broadcast,
and the caller receives the original exception after all threads join.

### Backend and Processing Boundaries

The reader drives the Stage 6 backend contract in one place:

```text
IOBackend::read_at(fd, buffer, offset) -> Task<size_t>
  -> Task::start()
  -> while suspended: IOBackend::wait_one()
  -> Task::result()
```

For SyncBackend the task completes during `start()`. ThreadPoolBackend and
UringBackend normally require completion progress through `wait_one()`. The
pipeline does not branch on concrete backend type.

The processor calls the registered Stage 7 `Pipeline` over only the valid byte
span. The final demo uses `ByteIncrementStage`, which increments every byte
modulo 256. It is deliberately deterministic and block-boundary independent,
so a second bounded pass can prove that output differs from raw input in the
expected way. The existing `NormalizeStage` remains another real CPU transform.

The writer currently uses robust positional `pwrite()` through
`write_all_at()`. It retries `EINTR`, completes short writes, and writes at the
work item's explicit offset. A common asynchronous write backend is not added
in this stage.

### Runtime Metrics

An instrumented executor registers bounded counters, gauges, and histograms
before workers start:

```text
Counters:   read / processed / written blocks and bytes
Gauges:     two queue depths and active BufferPool leases
Histograms: total read / process / write latency
Pipeline:   one existing per-Stage latency histogram
```

Queue Gauges change under the same mutex-protected transitions that change
queue size. Failure resets current depth to zero while retaining the peak.
The BufferPool Gauge follows lease acquisition and RAII return. Total process
latency surrounds the whole stage chain; the nested Stage histogram explains
the cost of each registered transformation.

Stage 12's reporting layer samples these atomics while workers run. The final
snapshot is exact after the workers stop. Its throughput is bytes written
divided by measured pipeline-and-commit elapsed time; it is one run's
observation, not a benchmark comparison.

### Reliable Publication

`PipelineExecutor::run_file()` never writes directly into the advertised final
path:

```text
open input
  -> reject same input/output inode
  -> mkstemp beside final output
  -> stream read/process/write into temporary file
  -> fsync temporary file
  -> rename temporary path over final path
  -> fsync parent directory
```

Before `rename()`, any exception unlinks the temporary file and preserves an
existing final output. `rename()` is atomic because temporary and final paths
share a directory. File fsync makes payload data durable before publication;
directory fsync asks the filesystem to persist the renamed directory entry.
If the final directory fsync itself fails after a successful rename, the new
name may already be visible and the caller receives an error because crash
durability could not be confirmed.

### Demo and Verification

The CLI accepts `auto`, `uring`, `threadpool`, and `sync`. Explicit choices are
fail-fast. Auto mode may fall back from io_uring to the bounded thread pool and
finally sync, and the selected backend is printed rather than hidden.

After publication, the demo opens input and output and compares them one fixed
block at a time using the `+1 modulo 256` rule. This validation uses two
reusable verification blocks and therefore does not load either file in full.

### Current Boundaries

- Large-file RSS under a memory limit, controlled overlap evidence, and backend
  performance comparisons belong to Stage 11; no Stage 10 timing is promoted
  into a benchmark claim.
- Stage 12 now provides TTY-aware terminal presentation and optional JSON over
  the same Stage 10 metrics without changing the executor.
- The processor currently has one worker and preserves FIFO order. Multi-core
  out-of-order processing and T5 acceptance are not claimed.
- The temp/fsync/rename mechanism is implemented, but automated `kill -9`
  crash testing remains Stage 13.
- TSan-instrumented binaries compile here, but this WSL runtime aborts before
  project code with `unexpected memory mapping`; no TSan-safety claim is made.

## Stage 12: Terminal and Optional JSON Metrics

### Reporting Is an Observer, Not a Pipeline Stage

Stage 12 consumes the metrics created by Stage 9 and updated by the Stage 10
executor. It does not sit between reader, processor, and writer:

```text
bounded data path:
BufferPool -> reader -> bounded queue -> processor -> bounded queue -> writer

observation path:
Counter / Gauge / Histogram -> terminal reporter -> final Snapshot -> JSON
```

`PipelineRunReport` holds only fixed run metadata and final totals. It contains
no block payload or `BufferHandle`. `LiveTerminalReporter` borrows stable
Counter and Gauge references from the caller-owned `MetricsRegistry`; the
registry and executor outlive the reporter.

### Live Reporter Lifetime

When `--report-ms` is positive, `LiveTerminalReporter` owns one `std::jthread`.
The thread periodically loads written bytes, queue depths, and active buffer
leases with the existing metric APIs. It never locks a pipeline queue or uses a
metric as synchronization. A TTY receives one refreshed line; redirected
stdout receives newline-delimited records.

`stop()` requests cooperative cancellation, wakes the timed wait, joins the
thread, and is safe to call again from the destructor. The join occurs before
the final summary is printed, so live and final terminal writes cannot overlap.
Reporting exceptions stop only the best-effort observer; they do not terminate
pipeline worker threads.

### Final Snapshot and Formats

The executor first finishes and joins its three workers, processed output is
committed, and the demo verifies it in a bounded second pass. It then takes one
final `MetricsRegistry::Snapshot` and uses that same value for:

- the human-readable terminal summary;
- optional schema-versioned JSON;
- the existing machine-readable `key=value` records.

The snapshot is stable because workers have quiesced. Periodic live loads are
not advertised as transactional. `StreamStateGuard` restores caller stream
flags after fixed-precision formatting, so one formatter cannot accidentally
change later output.

### Reliable JSON Publication

`render_metrics_json()` emits fixed run/config/result objects and bounded
Counter, Gauge, and Histogram arrays. JSON strings escape control characters,
and non-finite numbers are rejected because JSON has no NaN or infinity
literal.

`write_metrics_json_atomic()` renders before touching the destination, then
uses a same-directory temporary file:

```text
mkstemp -> complete write -> fsync(file) -> rename -> fsync(directory)
```

RAII owns the temporary file descriptor and removes the temporary name if
publication fails before rename. Input, processed output, and metrics JSON must
name different files. The JSON parent directory must already exist; Stage 12
does not silently create an unexpected directory tree.

### Compatibility and Boundedness

Stage 11 tools parse the original `key=value` lines, so those records remain
stable. They can run with `--report-ms=0` to avoid periodic output while still
receiving final metrics. `Auto` still reports both requested and actually
selected backends.

Report memory consists of a fixed metadata object, at most one small progress
line, and one snapshot/JSON document bounded by the registry's 64-metric and
fixed-bucket limits. Nothing is stored per block. Buffer ownership,
fixed-capacity queues, backpressure, write offsets, and reliable processed-file
publication remain unchanged.

### Current Boundaries

- Terminal output and a local JSON artifact are supported; HTTP, dashboards,
  databases, and remote collection are out of scope.
- JSON is a completed-run snapshot, not an event log and not a per-block trace.
- A live rate is an observation, not evidence that coroutines or io_uring made
  the pipeline faster.
- Stage 13 still owns final error/acceptance tests and unresolved large-file,
  crash-kill, TSan-environment, and interview packaging work.
