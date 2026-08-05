#pragma once

#include <cstddef>
#include <span>

namespace asyncdataloader::buffer {

class AlignedBuffer {
public:
    AlignedBuffer(std::size_t size, std::size_t alignment);
    ~AlignedBuffer() noexcept;

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    AlignedBuffer(AlignedBuffer&& other) noexcept;
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;

    [[nodiscard]] std::byte* data() noexcept;
    [[nodiscard]] const std::byte* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t alignment() const noexcept;

    [[nodiscard]] std::span<std::byte> bytes() noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    std::byte* data_{nullptr};
    std::size_t size_{0};
    std::size_t alignment_{0};
};

}  // namespace asyncdataloader::buffer
