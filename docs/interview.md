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
