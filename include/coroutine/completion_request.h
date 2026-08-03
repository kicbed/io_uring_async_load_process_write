#pragma once

#include <coroutine>
#include <stdexcept>
#include <utility>

namespace asyncdataloader::coroutine {

class CompletionRequest {
public:
    CompletionRequest() = default;

    CompletionRequest(const CompletionRequest&) = delete;
    CompletionRequest& operator=(const CompletionRequest&) = delete;
    CompletionRequest(CompletionRequest&&) = delete;
    CompletionRequest& operator=(CompletionRequest&&) = delete;

    void set_continuation(std::coroutine_handle<> continuation) {
        if (!continuation) {
            throw std::logic_error("cannot store an empty continuation");
        }
        if (completed_) {
            throw std::logic_error("cannot attach a continuation after completion");
        }
        if (continuation_) {
            throw std::logic_error("CompletionRequest already has a continuation");
        }

        continuation_ = continuation;
    }

    [[nodiscard]] bool has_continuation() const noexcept {
        return static_cast<bool>(continuation_);
    }

    [[nodiscard]] bool completed() const noexcept {
        return completed_;
    }

    [[nodiscard]] int completion_result() const {
        if (!completed_) {
            throw std::logic_error("completion result is not available");
        }

        return completion_result_;
    }

    void complete(int completion_result) {
        if (completed_) {
            throw std::logic_error("CompletionRequest was completed more than once");
        }
        if (!continuation_) {
            throw std::logic_error("cannot complete a request without a continuation");
        }
        if (continuation_.done()) {
            throw std::logic_error("cannot resume an already completed coroutine");
        }

        completion_result_ = completion_result;
        completed_ = true;
        auto continuation = std::exchange(continuation_, {});

        // Resumption may destroy the owner of this request. Do not access
        // members after this call.
        continuation.resume();
    }

private:
    std::coroutine_handle<> continuation_{};
    int completion_result_{0};
    bool completed_{false};
};

}  // namespace asyncdataloader::coroutine
