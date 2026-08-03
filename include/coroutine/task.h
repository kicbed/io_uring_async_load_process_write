#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace asyncdataloader::coroutine {

template <typename T>
class Task {
public:
    struct promise_type {
        std::optional<T> value_;
        std::exception_ptr exception_;

        Task get_return_object() noexcept {
            using PromiseHandle = std::coroutine_handle<promise_type>;
            return Task{PromiseHandle::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept {
            return {};
        }

        std::suspend_always final_suspend() noexcept {
            return {};
        }

        void return_value(T value) {
            value_.emplace(std::move(value));
        }

        void unhandled_exception() noexcept {
            exception_ = std::current_exception();
        }
    };

    using Handle = std::coroutine_handle<promise_type>;

    explicit Task(Handle handle) noexcept : handle_(handle) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept
        : handle_(std::exchange(other.handle_, {})),
          started_(std::exchange(other.started_, false)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) {
                handle_.destroy();
            }

            handle_ = std::exchange(other.handle_, {});
            started_ = std::exchange(other.started_, false);
        }

        return *this;
    }

    ~Task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return static_cast<bool>(handle_);
    }

    [[nodiscard]] bool done() const noexcept {
        return handle_ && handle_.done();
    }

    void start() {
        if (!handle_) {
            throw std::logic_error("cannot start an invalid Task");
        }
        if (started_) {
            throw std::logic_error("Task has already been started");
        }

        started_ = true;
        handle_.resume();
    }

    T result() {
        if (!handle_) {
            throw std::logic_error("cannot get a result from an invalid Task");
        }
        if (!started_) {
            throw std::logic_error("cannot get a result before Task is started");
        }
        if (!handle_.done()) {
            throw std::logic_error("cannot get a result before Task completes");
        }

        auto& promise = handle_.promise();
        if (promise.exception_) {
            std::rethrow_exception(promise.exception_);
        }
        if (!promise.value_) {
            throw std::logic_error("Task result has already been consumed");
        }

        T value = std::move(*promise.value_);
        promise.value_.reset();
        return value;
    }

private:
    Handle handle_{};
    bool started_{false};
};

}  // namespace asyncdataloader::coroutine
