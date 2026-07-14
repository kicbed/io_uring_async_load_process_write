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
