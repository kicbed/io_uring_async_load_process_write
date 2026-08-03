#pragma once

#include "coroutine/completion_request.h"

#include <liburing.h>

#include <coroutine>
#include <cstddef>
#include <cstdint>

namespace asyncdataloader::coroutine {

class ReadAwaiter;

class UringContext {
public:
    explicit UringContext(unsigned queue_depth);
    ~UringContext();

    UringContext(const UringContext&) = delete;
    UringContext& operator=(const UringContext&) = delete;
    UringContext(UringContext&&) = delete;
    UringContext& operator=(UringContext&&) = delete;

    [[nodiscard]] ReadAwaiter read_at(
        int fd,
        void* buffer,
        std::size_t byte_count,
        std::uint64_t offset
    );

    void wait_one();

private:
    friend class ReadAwaiter;

    io_uring ring_{};
};

class ReadAwaiter {
public:
    ReadAwaiter(
        UringContext& context,
        int fd,
        void* buffer,
        std::size_t byte_count,
        std::uint64_t offset
    );

    ReadAwaiter(const ReadAwaiter&) = delete;
    ReadAwaiter& operator=(const ReadAwaiter&) = delete;
    ReadAwaiter(ReadAwaiter&&) = delete;
    ReadAwaiter& operator=(ReadAwaiter&&) = delete;

    [[nodiscard]] bool await_ready() const noexcept;
    bool await_suspend(std::coroutine_handle<> continuation);
    std::size_t await_resume();

private:
    UringContext& context_;
    int fd_;
    void* buffer_;
    std::size_t byte_count_;
    std::uint64_t offset_;
    CompletionRequest request_;
    int immediate_result_{0};
    bool has_immediate_result_{false};
};

}  // namespace asyncdataloader::coroutine
