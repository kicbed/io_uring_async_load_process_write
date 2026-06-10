#include <chrono>
#include <coroutine>
#include <exception>
#include <iostream>
#include <thread>

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

class DelayAwaiter {
public:
  explicit DelayAwaiter(std::chrono::milliseconds delay) : delay_(delay) {}

  bool await_ready() const noexcept {
    return false;
  }

  void await_suspend(std::coroutine_handle<> handle) {
    auto delay = delay_;
    std::thread([delay, handle]() {
      std::this_thread::sleep_for(delay);
      if (handle && !handle.done()) {
        handle.resume();
      }
    }).detach();
  }

  void await_resume() noexcept {}

private:
  std::chrono::milliseconds delay_;
};

SimpleTask wait_for_delay() {
  std::cout << "before delay co_await\n";
  co_await DelayAwaiter{std::chrono::milliseconds{100}};
  std::cout << "after delay co_await\n";

  co_return;
}

}  // namespace asyncdataloader::stage1

int main() {
  std::cout << "AsyncDataLoader - Stage 1 delay awaiter demo\n";

  auto task = asyncdataloader::stage1::wait_for_delay();
  std::cout << "before first resume\n";

  task.resume();

  std::cout << "after first resume\n";

  std::cout << "waiting in main\n";
  while (!task.done()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
  }

  std::cout << "done=" << (task.done() ? "true" : "false") << "\n";

  return 0;
}
