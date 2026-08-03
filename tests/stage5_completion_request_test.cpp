#include "coroutine/completion_request.h"
#include "coroutine/task.h"

#include <cerrno>
#include <coroutine>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

using asyncdataloader::coroutine::CompletionRequest;
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

class ManualCompletionAwaiter {
public:
    explicit ManualCompletionAwaiter(CompletionRequest& request) noexcept
        : request_(request) {}

    [[nodiscard]] bool await_ready() const noexcept {
        return false;
    }

    void await_suspend(std::coroutine_handle<> continuation) {
        request_.set_continuation(continuation);
    }

    int await_resume() const {
        return request_.completion_result();
    }

private:
    CompletionRequest& request_;
};

Task<int> wait_for_completion(
    CompletionRequest& request,
    int& resume_count
) {
    const int result = co_await ManualCompletionAwaiter{request};
    ++resume_count;
    co_return result;
}

Task<int> destroy_request_during_resume(
    std::unique_ptr<CompletionRequest>& request_owner
) {
    const int result = co_await ManualCompletionAwaiter{*request_owner};
    request_owner.reset();
    co_return result;
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<CompletionRequest>);
    static_assert(!std::is_copy_assignable_v<CompletionRequest>);
    static_assert(!std::is_move_constructible_v<CompletionRequest>);
    static_assert(!std::is_move_assignable_v<CompletionRequest>);

    CompletionRequest invalid_request;
    if (invalid_request.has_continuation() || invalid_request.completed()) {
        return fail("a new CompletionRequest should be empty and incomplete");
    }
    if (!throws_logic_error([&invalid_request] {
            (void)invalid_request.completion_result();
        })) {
        return fail("result before completion should throw std::logic_error");
    }
    if (!throws_logic_error([&invalid_request] {
            invalid_request.complete(1);
        })) {
        return fail("completion without a continuation should be rejected");
    }
    if (!throws_logic_error([&invalid_request] {
            invalid_request.set_continuation({});
        })) {
        return fail("an empty continuation should be rejected");
    }

    CompletionRequest success_request;
    int resume_count = 0;
    auto success_task = wait_for_completion(success_request, resume_count);
    success_task.start();

    if (success_task.done() || !success_request.has_continuation() ||
        success_request.completed() || resume_count != 0) {
        return fail("await_suspend should save the continuation and suspend Task");
    }

    std::coroutine_handle<> noop_continuation = std::noop_coroutine();
    if (!throws_logic_error([&success_request, noop_continuation] {
            success_request.set_continuation(noop_continuation);
        })) {
        return fail("a request should accept exactly one continuation");
    }

    success_request.complete(4096);
    if (!success_request.completed() || success_request.has_continuation() ||
        !success_task.done() || resume_count != 1) {
        return fail("completion should clear and resume the continuation exactly once");
    }
    if (success_request.completion_result() != 4096 ||
        success_task.result() != 4096) {
        return fail("completion result should flow through await_resume and Task");
    }
    if (!throws_logic_error([&success_request] {
            success_request.complete(8192);
        }) || resume_count != 1) {
        return fail("duplicate completion should not resume the coroutine again");
    }

    CompletionRequest error_request;
    int error_resume_count = 0;
    auto error_task = wait_for_completion(error_request, error_resume_count);
    error_task.start();
    error_request.complete(-EIO);
    if (error_resume_count != 1 || error_task.result() != -EIO) {
        return fail("negative CQE-style results should be preserved for the awaiter");
    }

    auto request_owner = std::make_unique<CompletionRequest>();
    CompletionRequest* raw_request = request_owner.get();
    auto destroying_task = destroy_request_during_resume(request_owner);
    destroying_task.start();
    raw_request->complete(17);
    if (request_owner || !destroying_task.done() ||
        destroying_task.result() != 17) {
        return fail("complete should not access request state after resuming");
    }

    return 0;
}
