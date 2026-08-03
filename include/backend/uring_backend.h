#pragma once

#include "backend/io_backend.h"
#include "coroutine/uring_read_awaiter.h"

namespace asyncdataloader::backend {

class UringBackend final : public IOBackend {
public:
    explicit UringBackend(unsigned queue_depth);

    [[nodiscard]] std::string_view name() const noexcept override;

    [[nodiscard]] coroutine::Task<std::size_t> read_at(
        int fd,
        std::span<std::byte> buffer,
        std::uint64_t offset
    ) override;

    void wait_one() override;

private:
    coroutine::UringContext context_;
};

}  // namespace asyncdataloader::backend
