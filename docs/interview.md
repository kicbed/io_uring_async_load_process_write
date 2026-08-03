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
