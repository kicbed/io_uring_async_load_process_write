#include "coroutine/task.h"

#include <coroutine>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

using asyncdataloader::coroutine::Task;

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

template <typename Function>
bool throws_logic_error(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const std::logic_error&) {
        return true;
    } catch (...) {
        return false;
    }

    return false;
}

Task<int> make_value_task(bool& body_entered) {
    body_entered = true;
    co_return 42;
}

Task<int> make_integer_task(int value) {
    co_return value;
}

Task<int> make_pending_task() {
    co_await std::suspend_always{};
    co_return 99;
}

Task<int> make_failure_task() {
    co_await std::suspend_never{};
    throw std::runtime_error("task failure");
}

Task<std::unique_ptr<int>> make_move_only_result_task() {
    co_return std::make_unique<int>(7);
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<Task<int>>);
    static_assert(!std::is_copy_assignable_v<Task<int>>);
    static_assert(std::is_move_constructible_v<Task<int>>);
    static_assert(std::is_move_assignable_v<Task<int>>);

    bool body_entered = false;
    auto value_task = make_value_task(body_entered);
    if (!value_task.valid() || value_task.done() || body_entered) {
        return fail("Task should be valid, lazy, and not done before start");
    }
    if (!throws_logic_error([&value_task] {
            (void)value_task.result();
        })) {
        return fail("result before start should throw std::logic_error");
    }

    value_task.start();
    if (!body_entered || !value_task.done()) {
        return fail("start should run the coroutine to final_suspend");
    }
    if (value_task.result() != 42) {
        return fail("co_return value should be available through result");
    }
    if (!throws_logic_error([&value_task] {
            (void)value_task.result();
        })) {
        return fail("a Task result should be consumed only once");
    }
    if (!throws_logic_error([&value_task] {
            value_task.start();
        })) {
        return fail("a Task should not be started twice");
    }

    auto move_source = make_integer_task(7);
    Task<int> move_target{std::move(move_source)};
    if (move_source.valid() || !move_target.valid()) {
        return fail("move construction should transfer coroutine ownership");
    }
    if (!throws_logic_error([&move_source] {
            move_source.start();
        })) {
        return fail("a moved-from Task should reject start");
    }
    move_target.start();
    if (move_target.result() != 7) {
        return fail("move-constructed Task should preserve its result");
    }

    auto move_assignment_target = make_integer_task(1);
    auto move_assignment_source = make_integer_task(2);
    move_assignment_target = std::move(move_assignment_source);
    if (move_assignment_source.valid() || !move_assignment_target.valid()) {
        return fail("move assignment should transfer coroutine ownership");
    }
    move_assignment_target.start();
    if (move_assignment_target.result() != 2) {
        return fail("move-assigned Task should own the replacement frame");
    }

    auto pending_task = make_pending_task();
    pending_task.start();
    if (pending_task.done()) {
        return fail("Task should remain incomplete while its coroutine is suspended");
    }
    if (!throws_logic_error([&pending_task] {
            (void)pending_task.result();
        })) {
        return fail("result before final_suspend should throw std::logic_error");
    }

    auto failure_task = make_failure_task();
    failure_task.start();
    if (!failure_task.done()) {
        return fail("a failed coroutine should still reach final_suspend");
    }
    try {
        (void)failure_task.result();
        return fail("stored coroutine exception should be rethrown");
    } catch (const std::runtime_error& error) {
        if (std::string{error.what()} != "task failure") {
            return fail("Task should preserve the original exception");
        }
    }

    auto move_only_task = make_move_only_result_task();
    move_only_task.start();
    auto move_only_result = move_only_task.result();
    if (!move_only_result || *move_only_result != 7) {
        return fail("Task should return move-only values without copying");
    }

    return 0;
}
