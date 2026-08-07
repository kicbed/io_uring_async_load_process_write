# Interview Notes

Stage 0 establishes a minimal CMake/C++20 project skeleton.

Important boundary:

- this is not a file loader;
- the final project is a bounded-memory preprocessing pipeline;
- coroutines and io_uring will be introduced only in their planned stages;
- benchmark claims must be measured, not invented.

## Stage 1: C++20 Coroutine Learning

### How does the simple task demo work?

`simple_task_demo` implements a minimal coroutine return type named `SimpleTask`.
When `hello_coroutine()` is called, C++ creates a coroutine frame, constructs
`promise_type`, calls `get_return_object()`, and returns a `SimpleTask` that owns
the coroutine handle. Because `initial_suspend()` returns `std::suspend_always`,
the coroutine body does not run until `SimpleTask::resume()` is called.

The important ownership rule is that `std::coroutine_handle` is only a handle, not
an owning smart pointer. `SimpleTask` is move-only and destroys the coroutine
frame in its destructor.

### What does `await_suspend()` do?

In `manual_resume_demo`, `co_await awaiter` calls the awaiter protocol:

```text
await_ready()
-> await_suspend(current_coroutine_handle)
-> later: handle.resume()
-> await_resume()
```

`await_suspend()` receives the current coroutine handle from the compiler. The
demo stores that handle in the awaiter, returns control to `main()`, and then
`main()` manually resumes the saved handle.

This is the conceptual bridge to later `io_uring` integration: instead of `main()`
resuming the handle, an I/O completion event will resume it.

### Why does the delay awaiter demo matter?

`delay_awaiter_demo` replaces manual resume with a small background thread. The
awaiter copies the delay value, starts a thread, sleeps, and then resumes the
saved coroutine handle.

This demonstrates the key lifetime risk in asynchronous systems: the coroutine
frame must remain alive until the external event resumes it. If the owning
`SimpleTask` destroys the coroutine frame before the background thread calls
`resume()`, the handle dangles.

### Do coroutines improve performance by themselves?

No. Coroutines organize asynchronous control flow. Performance comes from the I/O
backend, scheduling, batching, reduced blocking, and pipeline overlap. In this
project, coroutines are a way to express async read/write workflows clearly; they
are not a standalone speed guarantee.

## Stage 4: Native liburing Basics

### Why learn native liburing before combining it with coroutines?

Stage 4 isolates the kernel I/O protocol from the coroutine protocol. A native
request follows this lifecycle:

```text
get SQE -> prepare operation -> attach request identity -> submit
        -> wait for CQE -> inspect cqe->res -> mark CQE seen
```

This makes Stage 5 easier to reason about: a coroutine awaiter will reorganize
the waiting and resumption, but it must preserve the same request lifetime,
completion decoding, and CQE consumption rules.

### What are SQEs and CQEs?

An SQE describes work submitted to the kernel, such as a read or write with a
file descriptor, buffer address, byte count, and offset. A CQE reports that a
submitted request completed. `cqe->res` contains either a non-negative result
such as the transferred byte count, or a negative errno value.

`io_uring_sqe_set_data()` attaches application-owned request identity to an SQE.
The completion path retrieves that identity with `io_uring_cqe_get_data()`, so it
does not need to assume completions arrive in submission order.

### Why must buffers and request metadata outlive the completion?

The kernel borrows the buffer address and the `user_data` pointer while the
operation is in flight; it does not take C++ ownership of those objects. In the
Stage 4 demos, the vectors, arrays, strings, and request records live in `main()`
until every corresponding CQE has been collected. Destroying or moving them too
early could leave the kernel or completion path with dangling addresses.

### What did the batch-read demo prove?

It prepared four fixed-size reads at explicit offsets, submitted them together,
and associated each completion with its own `ReadRequest`. Completion order is
not treated as output order. The demo drains all expected CQEs, calls
`io_uring_cqe_seen()` once per completion, and reports results in request order.

This is an early model of the future pipeline's request bookkeeping, not yet a
pipeline: it has no processor, writer overlap, BufferPool, or backpressure.

### How are errors and short I/O represented?

- A negative liburing return or negative `cqe->res` encodes `-errno`; error text
  uses `std::strerror(-result)`.
- A read result of zero means EOF at that offset.
- A positive read smaller than the requested size is a valid short read, often
  caused by reaching EOF.
- A short write must not be silently accepted. The Stage 4 fixed-payload demo
  reports it as failure; a general writer must resubmit the unwritten suffix.
- `io_uring_wait_cqe()` retries `-EINTR` because signal interruption is not the
  completion result of the submitted request.

### Is io_uring always faster?

No. Its value depends on workload, queue depth, storage, filesystem, kernel,
batching, and cache state. Stage 4 proves API correctness only. Performance
claims require the controlled benchmark work planned for Stage 11.

## Stage 5: `io_uring` and C++20 Coroutine Integration

### How does `co_await read_at()` connect to `io_uring`?

`UringContext::read_at()` returns a `ReadAwaiter`. The C++ compiler applies the
awaiter protocol to that object:

```text
await_ready()
-> await_suspend(current coroutine handle)
-> later: saved handle.resume()
-> await_resume()
```

`ReadAwaiter::await_suspend()` prepares and submits the read SQE, attaches its
embedded `CompletionRequest` through `user_data`, saves the current coroutine
handle in that request, and returns `true`. Returning `true`, rather than
`io_uring_submit()` itself, is what keeps the coroutine suspended.

### What does `co_return co_await read_at(...)` mean?

The inner `co_await` completes first and produces a byte count. The outer
`co_return` passes that byte count to `Task<T>::promise_type::return_value()`.

```cpp
const std::size_t bytes_read = co_await context.read_at(...);
co_return bytes_read;
```

`Task<T>` manages the whole coroutine and its final result. `ReadAwaiter`
manages one asynchronous read operation.

### Who resumes the coroutine?

The kernel does not call `coroutine_handle::resume()`. It only produces a CQE.
`UringContext::wait_one()` retrieves the `CompletionRequest` pointer from
`CQE.user_data`, copies `cqe->res`, marks the CQE seen, and calls
`CompletionRequest::complete()`. `complete()` stores the result and resumes the
saved continuation exactly once.

### Why is `CompletionRequest` separate from `ReadAwaiter`?

It isolates the completion boundary:

```text
CQE.user_data -> CompletionRequest -> coroutine_handle
```

The request owns neither the coroutine frame nor the buffer. It stores borrowed
continuation state and the completion result. Keeping duplicate-completion
checks in this small class makes the exactly-once resume rule directly
testable without requiring a live `io_uring`.

### Why are `ReadAwaiter` and `CompletionRequest` non-movable?

The SQE stores `&request_` in `user_data`. The kernel returns that same address
in the CQE. Moving the awaiter would move its embedded request and invalidate
the address held by the in-flight operation.

The `Task` therefore owns a coroutine frame that keeps the active awaiter at a
stable address across suspension.

### Who owns the buffer during an asynchronous read?

The vector in `async_read_demo::main()` owns the buffer. The kernel only borrows
`buffer.data()` from submission until completion. The vector must not be
destroyed, resized, or otherwise reallocated while the read is in flight.

The same rule applies to the file descriptor, ring, coroutine frame, and request
metadata: their owners must outlive the CQE path that borrows them.

### How are results and errors propagated?

- `cqe->res > 0` is the number of bytes read;
- `cqe->res == 0` is EOF;
- `cqe->res < 0` is `-errno`.

`ReadAwaiter::await_resume()` returns a non-negative byte count or throws
`std::system_error` for a negative result. An exception escaping the coroutine
body is captured by `promise_type::unhandled_exception()` and rethrown when the
caller invokes `Task::result()`.

### What does Stage 5 prove, and what does it not prove?

It proves that a real `io_uring` completion can resume the correct C++20
coroutine, that request and buffer lifetimes are explicit, that one completion
resumes once, and that CQE errors reach the caller.

The current context waits for one completion on the calling thread. It is not
yet a reusable `IOBackend`, a fallback system, or a read-process-write
pipeline. Those boundaries belong to Stages 6-10. Coroutines organize control
flow; they are not, by themselves, a performance optimization.

## Stage 6: `IOBackend` Abstraction and Fallback

### Why introduce an `IOBackend` interface?

The future pipeline should depend on read semantics, not on liburing calls or
thread-pool details. `IOBackend` gives every implementation the same
`name()`, `read_at()`, and `wait_one()` contract. The pipeline can therefore be
written once and select an implementation at runtime.

The current interface is read-only. A common write operation still has to be
designed before the final pipeline is complete.

### Which design patterns are used?

`BackendFactory` is a factory: it owns construction and fallback policy.
`UringBackend`, `ThreadPoolBackend`, and `SyncBackend` are interchangeable
strategies behind `IOBackend`. A `std::unique_ptr<IOBackend>` expresses both
runtime polymorphism and unique RAII ownership of the selected implementation.

### Is `ThreadPoolBackend` real asynchronous I/O?

It is asynchronous relative to the caller, but its workers still execute
blocking `pread()`. It is a bounded blocking-I/O offload mechanism, not kernel
asynchronous I/O. A fixed worker set processes many requests; there is not one
thread per coroutine.

### How does the thread pool connect to a coroutine?

`ReadAwaiter::await_suspend()` saves the current coroutine handle in the
request and places a pointer to that request in the bounded work queue. A
worker performs `pread()` and publishes the result to the completion queue.
The caller's `wait_one()` removes that completion and calls `resume()` on the
saved handle. `await_resume()` then returns the byte count or throws.

### Why does the worker not resume the coroutine directly?

Direct resumption would run everything after `co_await` on an I/O worker. The
current design keeps workers responsible only for blocking reads and resumes
continuations on the thread that calls `wait_one()`. This makes execution
context explicit and matches the current io_uring backend's caller-driven
completion model.

### Why does only Auto mode fall back?

An explicit backend request is a requirement and should fail visibly if it
cannot be satisfied. Auto mode is permission to choose the best available
candidate. Silently replacing an explicitly requested Uring backend with Sync
would make configuration and benchmark results misleading.

### Which failures trigger fallback?

Only backend construction-time `std::system_error` triggers the Auto chain.
Invalid configuration is reported rather than hidden. A later read failure,
such as `EBADF` or `EACCES`, is returned to the caller and is not retried on a
different backend. Retrying arbitrary operations would hide bugs and could
duplicate future write side effects.

### Why check `Task::done()` before `wait_one()`?

`SyncBackend` executes its blocking read during `Task::start()` and completes
immediately. Uring and thread-pool requests normally suspend and need a
completion event. The common caller therefore uses:

```cpp
task.start();
if (!task.done()) {
    backend.wait_one();
}
```

Calling `wait_one()` unconditionally would be an error for SyncBackend because
it has no pending asynchronous completion.

### How is the thread-pool queue bounded?

`inflight_count_` includes requests waiting for workers, executing in workers,
and waiting in the completion queue. Once it reaches `max_inflight`, a new
submission completes immediately with `EAGAIN`. This bounds backend-owned
request queues, although full BufferPool and inter-stage backpressure still
belong to Stage 8.

### What are the buffer ownership rules?

The caller owns the file descriptor, buffer, backend, and `Task`. The selected
backend only borrows the fd and buffer until completion. The Task owns the
coroutine frame; the frame owns the active awaiter and request metadata. All
four caller-owned objects must outlive the operation.

### What does the fallback demo prove?

It proves that the same caller code can read through all three strategies and
that disabling io_uring in Auto mode selects ThreadPool, while disabling both
asynchronous candidates selects Sync. It reports both requested and selected
backend names so fallback is observable.

It does not prove end-to-end throughput, processing-stage overlap, large-file
memory bounds, or that io_uring is faster. Those claims require later pipeline
and benchmark stages.

## Stage 7: Stage Registration and Pipeline Framework

### What problem does the Stage interface solve?

It separates the pipeline's control flow from a particular preprocessing
algorithm. The orchestrator only needs to call `Stage::process(span)`; a
normalizer, validator, checksum calculator, or caller-defined transform can be
substituted without changing that orchestration code.

In design-pattern terms, each concrete Stage is a Strategy, and `Pipeline` is
the ordered composition that owns and invokes those strategies.

### Why use virtual objects instead of only function pointers?

A plain function pointer can select stateless behavior, but it cannot naturally
carry per-stage configuration or state. A Stage object can hold values such as
the affine multiplier and offset or the checksum from its latest call. Virtual
dispatch gives all such objects one stable interface.

`std::function` could also store stateful callables, but the explicit class
interface makes the stage name, ownership rule, extension point, and future
stage-specific API easier to explain and test in this project.

### Why does Pipeline store `std::unique_ptr<Stage>`?

Concrete stages have different sizes, so storing base objects by value would
slice away derived behavior. A unique pointer preserves polymorphism and says
that exactly one Pipeline owns each registered stage. `std::move` makes that
ownership transfer explicit, and destruction is automatic through RAII.

### Why must Stage have a virtual destructor?

The Pipeline destroys concrete stages through `Stage*` held by
`unique_ptr<Stage>`. Without a virtual base destructor, deleting a derived
object through that base pointer is undefined behavior and may skip derived
cleanup.

### What ownership does `std::span<std::byte>` express?

The span is a non-owning view: pointer plus length. The caller owns the byte
storage, and each stage may mutate it only during `process()`. A stage must not
save the span or pointer because the underlying BufferPool lease may be
returned immediately after later pipeline work completes.

### In what order do stages run?

They run in registration order over the same block. If `A`, `B`, and `C` are
registered in that order, the data path is:

```text
block -> A -> B -> C -> block
```

If a stage throws, the error propagates and later stages do not run. Earlier
in-place modifications are not rolled back.

### What do the built-in stages prove?

- `NoOpStage` proves interface composition while preserving bytes.
- `NormalizeStage` performs an actual per-block min-max byte transformation
  using constant extra memory.
- `ChecksumStage` proves that a stage may observe data and retain a small
  result without changing the block.

NoOp and checksum alone are not final CPU-processing evidence. Normalize is a
real transform, but it still needs end-to-end streaming integration in Stage
10 before making a pipeline-level claim.

### Is ChecksumStage thread-safe?

Not for concurrent calls on the same instance, because every call writes
`last_checksum_` without synchronization. Stage 7 executes stages
sequentially. A future parallel processor should use one stage chain per
worker or redesign how per-block results are returned; adding a mutex without
considering the architecture could unnecessarily serialize processing.

### How is Stage 7 still bounded?

The pipeline receives one existing span, copies no block, and built-in stages
use constant extra memory. The vector grows only with the configured number of
stage objects. However, Stage 7 cannot bound an external reader's number of
blocks; BufferPool capacity and backpressure are Stage 8 responsibilities.

### What does the custom affine demo demonstrate?

Caller code derives from `Stage`, stores configuration `(multiplier=2,
offset=1)`, registers the object by moving a `unique_ptr`, and transforms
`[1,2,3]` into `[3,5,7]`. The library itself is unchanged, which demonstrates
an open extension point rather than three hard-coded function calls.

### What does Stage 7 not prove?

It does not yet connect reading to processing or writing, overlap the three
stages, enforce file-level backpressure, preserve parallel output order,
persist through temporary-file plus `fsync` plus rename, or collect metrics.
Those are explicit later-stage boundaries, so the single-block synchronous
demo is not presented as the final pipeline.

Interview-ready short version:

> I introduced a polymorphic Stage strategy and a Pipeline that owns stages
> with unique_ptr and invokes them in registration order over a borrowed span.
> The caller retains buffer ownership, stages cannot retain the view, and the
> built-ins include a real constant-space normalization transform. This stage
> establishes the extensible CPU-processing boundary; bounded BufferPool
> handoff and end-to-end read/process/write overlap are deliberately later
> stages.

## Stage 8: BufferPool, Configuration, and Backpressure

### Why are both a BufferPool and an SPSC queue needed?

They control different resources. The pool bounds and reuses physical block
memory across the whole pipeline. The queue bounds how many completed blocks
may wait between two adjacent stages and preserves FIFO handoff. The queue
stores small move-only handles, not copies of the block bytes.

### Why is `BufferHandle` move-only?

One active lease must have one logical owner. Copying a handle would allow two
destructors to return the same slot. Moving transfers the pool pointer and
index, then clears the source, so only the destination can return the lease.

### How does automatic buffer return work?

`BufferHandle::~BufferHandle()` calls its private release helper. That helper
clears the handle first and asks the pool to mark the index available. Clearing
first makes repeated cleanup of a moved-from or already-released handle a
no-op. The physical allocation remains owned by the pool and is freed only
when the pool itself is destroyed.

### How does blocking backpressure work?

`AlignedBufferPool::acquire()` waits while the free-index list is empty.
`SPSCQueue::push()` waits while the ring is full, and `pop()` waits while it is
empty. Condition-variable predicates handle spurious wakeups. A consumer
return or pop updates protected state and notifies one waiting producer.

### Why is the queue implemented as a ring?

Its vector is allocated once at the configured capacity. `head_` and `tail_`
wrap and reuse old slots, so FIFO operations do not shift elements or grow an
unbounded container. The first implementation uses a mutex and condition
variables; SPSC describes the usage contract, not a lock-free performance
claim.

### What is the Stage 8 memory bound?

Payload memory is exactly:

```text
block_size * max_inflight_buffers
```

plus fixed allocator, pool, thread, and queue metadata. Queue slots hold
handles, so increasing input-file size does not allocate one new block per
input block. End-to-end RSS still needs to be measured after Stage 10
integration.

### Is a 4096-byte aligned allocation enough for `O_DIRECT`?

Not universally. Direct I/O may constrain buffer address, request length, and
file offset, and the values vary by filesystem and kernel. `statx()` with
`STATX_DIOALIGN` can report requirements when supported. Stage 8 prepares
aligned storage; the future I/O path must still validate offsets, partial
blocks, support, and fallback behavior.

### What does the backpressure demo prove?

With two pool buffers and one queue slot, the first handle fills the queue and
the second producer push cannot complete until the consumer pops. Markers are
consumed in FIFO order, and both handles return to the pool by RAII.

It does not prove real read/process/write overlap, large-file RSS, ordered
output, reliable persistence, metrics, or performance. Those remain explicit
later stages.

Interview-ready short version:

> I bounded physical memory with a fixed aligned BufferPool and transferred
> move-only RAII leases through a fixed-capacity SPSC ring. Pool exhaustion and
> queue exhaustion block upstream work, so a slow consumer creates real
> backpressure without copying blocks or letting memory grow with file size.

## Stage 9: Metrics Data Structures and Instrumentation

### Why use three metric types?

They preserve different information:

- a Counter answers how many blocks or bytes have completed in total;
- a Gauge answers how many buffers or queue entries exist now and records the
  peak pressure;
- a Histogram answers how latency samples are distributed rather than hiding
  variation behind one average.

A throughput rate is not a fourth stored primitive here. A reporter can sample
a bytes Counter twice and divide its delta by measured elapsed time. Stage 9
provides the safe inputs; later end-to-end stages provide the real byte events
and reporting interval.

### Why is MetricsRegistry not a singleton?

The application owns and injects it. This makes lifetime and dependencies
visible, allows isolated registries in tests or separate pipeline instances,
and avoids hidden global state leaking between runs. Pipeline and BufferPool
borrow metric references, so the registry must outlive those borrowers.

### Why use small entry vectors instead of unordered_map?

The registry is capped at 64 metrics, and lookup or registration is not the
per-block hot path. A short linear scan is simple, iteration order is stable,
and it avoids hash buckets and their extra allocations. Each metric itself is
heap-owned by `unique_ptr`, so references remain stable if an entry vector
reallocates. If measured production cardinality later made lookup hot, the
container could change without changing the metric API.

### How does the fixed Histogram choose a bucket?

For bounds `[20, 50]`, the exclusive buckets are:

```text
sample <= 20       -> bucket 0
20 < sample <= 50  -> bucket 1
sample > 50        -> bucket 2 (overflow)
```

`lower_bound` returns the first upper bound greater than or equal to the sample.
If no such bound exists, it returns `end()`; that iterator's index equals the
finite-bucket count, deliberately selecting the following overflow bucket.
Every observation updates both its bucket and the total count/sum, preserving
both distribution and aggregate information without storing every sample.

### Why is ScopedTimer based on RAII?

The constructor stores a `steady_clock` start time and the destructor observes
the elapsed nanoseconds. Putting one timer directly around each
`Stage::process()` call gives one measurement per stage. If processing throws,
stack unwinding still runs the destructor, so the failed call's elapsed time is
recorded and later stages remain unexecuted.

`steady_clock` is used because elapsed time must not follow wall-clock changes.
The timer is non-copyable and non-movable so one scope produces exactly one
observation.

### How is a BufferPool lease reflected in the Gauge?

The metric follows lease state rather than C++ object count. A successful
acquire changes one slot from free to leased and increments the Gauge. Returning
the lease through BufferHandle RAII changes it back and decrements the Gauge.
Moving a handle only transfers ownership of the same lease, so it must not
change the Gauge. A failed `try_acquire()` also makes no change.

The high watermark records the maximum simultaneous number of leased buffers.
It exposes memory pressure while physical ownership still remains entirely in
the fixed BufferPool.

### Why can metric atomics use relaxed ordering?

The atomics prevent data races on numeric observations, but metrics are not a
handoff protocol. A reader must never infer buffer readiness from a Counter or
Gauge. Queue mutexes, condition variables, and backend completion mechanisms
publish the actual work. Because metric updates establish no cross-thread data
dependency, relaxed ordering is sufficient for these totals.

### Is a registry snapshot one exact instant?

Not while workers are updating. Counter values, Gauge current/high watermark,
and Histogram totals/buckets are separate atomic loads, so an update can occur
between them. The snapshot is exact after workers quiesce and is a bounded,
low-overhead operational view while they run. Claiming a transactional snapshot
would require additional synchronization and could perturb the measured path.

### How are metrics kept bounded?

The registry accepts at most 64 names, each at most 128 bytes. Every Histogram
has at most 32 finite buckets plus overflow. Snapshots copy only this bounded
registered set; there is no arbitrary label map and no vector containing one
sample per block. Metric memory therefore does not grow with input-file size.

### What does Stage 9 prove, and what does it not prove?

It proves reusable bounded metric primitives, stable registry ownership, a
unified reporting snapshot, automatic CPU-stage timing, and RAII-consistent
tracking of active BufferPool leases.

It does not yet prove real read/process/write overlap, final queue depth,
read/write latency, throughput, output ordering, reliable `fsync`/rename, or
large-file RSS. Those claims require the Stage 10 pipeline and later controlled
benchmarks. Instrumentation itself is not a performance improvement.

Interview-ready short version:

> I built bounded atomic Counter, Gauge, and fixed-bucket Histogram primitives,
> then placed them in an injected registry with a unified non-transactional
> snapshot. RAII timers automatically measure each CPU stage, and BufferPool
> lease transitions update current and peak in-flight buffers without changing
> ownership or backpressure. Metric memory is capped and independent of file
> size; end-to-end throughput claims remain deferred until the real pipeline is
> measured.

## Stage 10: End-to-End Preprocessing Pipeline

### Why is this no longer just a file loader?

The reader does not return raw blocks to an application. Every non-empty block
must cross a registered CPU `Pipeline`, the demo's `ByteIncrementStage` changes
each byte, and only transformed bytes reach the output writer. The demo then
checks `output[i] == input[i] + 1 modulo 256` in a bounded streaming pass. This
is a complete data-product path, not a read API with a checksum attached.

### What is the complete function flow?

```text
main
  -> BackendFactory::create()
  -> Pipeline::add_stage(ByteIncrementStage)
  -> PipelineExecutor::run_file()
       -> create temporary output
       -> PipelineExecutor::run()
            -> writer_loop()
            -> processor_loop()
            -> reader_loop()
       -> fsync(file) -> rename -> fsync(directory)
  -> bounded output verification
  -> final metric snapshot
```

`run()` starts consumers before the producer, then joins all three threads
before returning or rethrowing the first failure. Starting order improves
startup behavior; correctness still comes from queue predicates and ownership,
not from assuming a particular scheduler order.

### What does each BlockWorkItem field do?

- `block_index` identifies the logical block.
- `file_offset` tells positional write where this block belongs.
- `valid_bytes` excludes unused capacity in a short final buffer.
- `BufferHandle` owns the one active lease and returns it by RAII.

The metadata is not the data itself. It is the shipping label attached to one
pool-owned block while ownership moves reader → queue → processor → queue →
writer.

### How do three-stage overlap and backpressure coexist?

Separate reader, processor, and writer threads allow different blocks to be in
different stages simultaneously. Two fixed-capacity queues allow a small,
bounded distance between stages. If the processor is slow, the first queue
fills and blocks the reader. If buffers are all leased, pool acquisition also
blocks. Overlap is allowed, unlimited run-ahead is not.

### Why move a BufferHandle instead of copying a vector?

Moving changes who owns permission to use the pool slot without copying its
payload. A copyable lease could lead to two destructors returning the same
slot. The move-only type makes exclusive ownership visible to the compiler and
keeps memory fixed regardless of input size.

### How do normal EOF and failures differ?

Normal EOF calls `close()`: the consumer drains already queued blocks and then
gets `nullopt`. A worker exception calls `fail()`: delivery stops, queued items
are destroyed, blocked threads wake, and all queues rethrow the same first
exception. Destroying queued work also destroys its handles, so buffers return
without a hand-written cleanup loop.

### How can one executor work with sync, thread-pool, and io_uring reads?

It depends only on `IOBackend::read_at()` and `wait_one()`. A synchronous task
is already done after `start()`; an asynchronous task needs completion progress.
Concrete selection stays in `BackendFactory`. Auto mode is permission to fall
back, while an explicit backend request remains fail-fast so results are not
mislabelled.

### Why write to a temporary file first?

Writing directly to the final name exposes a partially processed file if a
stage fails. The executor writes beside the final path, fsyncs all payload,
atomically renames it, then fsyncs the parent directory entry. Before rename,
RAII cleanup unlinks the temporary path and leaves an old final file intact.

### What do the Stage 10 metrics mean?

Counters show how many blocks and bytes completed each stage. Gauges show
current and peak queue occupancy and active buffer leases. Histograms show
read, whole-pipeline process, write, and individual Stage latency. Throughput
is derived from actually written bytes and measured elapsed time. Metrics
observe state; they never synchronize work or grant buffer ownership.

### What does Stage 10 still not prove?

It does not prove that io_uring is fastest, that coroutines improve speed, or
that a particular backend wins. It also does not yet provide controlled
large-file RSS evidence, multi-core out-of-order processing, automated
`kill -9` acceptance, polished terminal UI, or JSON. Those remain explicit
Stage 11-13 boundaries.

Interview-ready short version:

> I assembled a bounded three-stage read-process-write pipeline around a fixed
> aligned BufferPool and two bounded queues. Move-only work items carry one
> RAII lease plus offset metadata, so a slow downstream stage applies
> backpressure and memory does not grow with file size. The CPU stage really
> transforms bytes, output is positionally correct and published through
> temp-file/fsync/rename/directory-fsync, Auto backend fallback is visible, and
> live counters, queue depths, in-flight buffers, and stage latencies come from
> the actual execution path. Performance comparisons are deliberately deferred
> to controlled benchmarks.
