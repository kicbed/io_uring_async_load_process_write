#include <coroutine>
#include <exception>
#include <iostream>

namespace asyncdataloader::stage1 {

class SimpleTask {
public:
  struct promise_type {
    SimpleTask get_return_object() {
      return SimpleTask{Handle::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept {
      return {};
    }
    std::suspend_always final_suspend() noexcept {
      return {};
    }
    void return_void() noexcept {}
    void unhandled_exception() {
      std::terminate();
    }
  };

  using Handle = std::coroutine_handle<promise_type>;

  explicit SimpleTask(Handle handle) : handle_(handle) {}

  SimpleTask(const SimpleTask&) = delete;
  SimpleTask& operator=(const SimpleTask&) = delete;

  SimpleTask(SimpleTask&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  SimpleTask& operator=(SimpleTask&& other) noexcept {
    if (this != &other) {
      if (handle_) {
        handle_.destroy();
      }

      handle_ = other.handle_;
      other.handle_ = nullptr;
    }

    return *this;
  }

  ~SimpleTask() {
    if (handle_) {
      handle_.destroy();
    }
  }

  void resume() {
    if (handle_ && !handle_.done()) {
      handle_.resume();
    }
  }

  bool done() const {
    return !handle_ || handle_.done();
  }

private:
  Handle handle_;
};

class ManualResumeAwaiter {
public:
  bool await_ready() const noexcept {
    return false;
  }

  void await_suspend(std::coroutine_handle<> handle) noexcept {
    saved_handle_ = handle;
  }

  void await_resume() noexcept {}

  bool has_handle() const noexcept {
    return saved_handle_ != nullptr;
  }

  void resume_saved() {
    auto handle = saved_handle_;
    saved_handle_ = nullptr;

    if (handle && !handle.done()) {
      handle.resume();
    }
  }

private:
  std::coroutine_handle<> saved_handle_{};
};

SimpleTask wait_for_manual_resume(ManualResumeAwaiter& awaiter) {
  std::cout << "before co_await\n";
  co_await awaiter;
  std::cout << "after co_await\n";
  co_return;
}

}  // namespace asyncdataloader::stage1

int main() {
  std::cout << "AsyncDataLoader - Stage 1 manual resume demo\n";

  asyncdataloader::stage1::ManualResumeAwaiter awaiter;

  auto task = asyncdataloader::stage1::wait_for_manual_resume(awaiter);
  std::cout << "before first resume\n";

  task.resume();
  std::cout << "after first resume\n";

  if (awaiter.has_handle()) {
    std::cout << "awaiter has handle=" << (awaiter.has_handle() ? "true" : "false") << "\n";

    std::cout << "before manual resume\n";
    awaiter.resume_saved();

    std::cout << "after manual resume\n";
  }

  std::cout << "done=" << (task.done() ? "true" : "false") << "\n";

  return 0;
}
