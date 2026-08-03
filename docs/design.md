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
