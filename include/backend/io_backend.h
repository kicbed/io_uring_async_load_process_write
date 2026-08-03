#pragma once

#include "coroutine/task.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace asyncdataloader::backend {

class IOBackend {
public:
    IOBackend() = default;
    virtual ~IOBackend() = default;

    IOBackend(const IOBackend&) = delete;
    IOBackend& operator=(const IOBackend&) = delete;
    IOBackend(IOBackend&&) = delete;
    IOBackend& operator=(IOBackend&&) = delete;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // The caller owns fd, buffer, and the returned Task. They must all remain
    // valid until the Task completes. A successful result may be smaller than
    // buffer.size(); zero bytes means EOF rather than an error.
    [[nodiscard]] virtual coroutine::Task<std::size_t> read_at(
        int fd,
        std::span<std::byte> buffer,
        std::uint64_t offset
    ) = 0;

    // Advance one pending asynchronous completion. Callers only need this
    // when a started Task has not completed immediately.
    virtual void wait_one() = 0;
};

}  // namespace asyncdataloader::backend
