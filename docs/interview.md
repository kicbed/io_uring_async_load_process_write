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
