#pragma once

#include "buffer/buffer_handle.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace asyncdataloader::pipeline {

// Owns one exclusive buffer lease together with the metadata required to move
// that block through reader, processor, and writer stages.
class BlockWorkItem {
public:
    BlockWorkItem(
        std::uint64_t block_index,
        std::uint64_t file_offset,
        std::size_t valid_bytes,
        buffer::BufferHandle buffer
    );
    ~BlockWorkItem() = default;

    BlockWorkItem(const BlockWorkItem&) = delete;
    BlockWorkItem& operator=(const BlockWorkItem&) = delete;

    BlockWorkItem(BlockWorkItem&& other) noexcept;
    BlockWorkItem& operator=(BlockWorkItem&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint64_t block_index() const noexcept;
    [[nodiscard]] std::uint64_t file_offset() const noexcept;
    [[nodiscard]] std::size_t valid_bytes() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;

    // Exposes only bytes produced by the reader. Buffer capacity beyond this
    // prefix must not be processed or written.
    [[nodiscard]] std::span<std::byte> valid_data() noexcept;
    [[nodiscard]] std::span<const std::byte> valid_data() const noexcept;

private:
    std::uint64_t block_index_{0};
    std::uint64_t file_offset_{0};
    std::size_t valid_bytes_{0};
    buffer::BufferHandle buffer_;
};

}  // namespace asyncdataloader::pipeline
