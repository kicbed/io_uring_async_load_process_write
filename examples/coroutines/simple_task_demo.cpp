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

SimpleTask hello_coroutine() {
  std::cout << "Hello from coroutine!\n";
  co_return;
}

}  // namespace asyncdataloader::stage1

int main() {
  std::cout << "AsyncDataLoader - Stage 1 simple task demo\n";
  // D1: 创建协程
  //     调用 hello_coroutine() → 创建帧 → promise → get_return_object() → initial_suspend() 挂起
  //     task 现在持有句柄，但协程体一行都没执行
  auto task = asyncdataloader::stage1::hello_coroutine();
  std::cout << "before resume\n";

  // D2: 恢复执行
  //     协程体开始跑：打印 "Hello from coroutine!" → co_return → final_suspend() 挂起
  task.resume();
  std::cout << "after resume\n";

  // D3: 检查状态
  //     应该输出 1（true），表示协程已经执行到 final_suspend
  std::cout << "done=" << (task.done() ? "true" : "false") << '\n';

  // D4: main 结束，task 析构
  //     ~SimpleTask() 调用 handle_.destroy()，协程帧释放

  return 0;
}
