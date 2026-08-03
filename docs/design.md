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
