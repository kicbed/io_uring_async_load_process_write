#pragma once

#include "backend/io_backend.h"

namespace asyncdataloader::backend {

class SyncBackend final : public IOBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override;

    [[nodiscard]] coroutine::Task<std::size_t> read_at(
        int fd,
        std::span<std::byte> buffer,
        std::uint64_t offset
    ) override;

    void wait_one() override;
};

}  // namespace asyncdataloader::backend
