#pragma once

#include <cstddef>
#include <span>

namespace asyncdataloader::buffer {

class AlignedBufferPool;

class BufferHandle {
public:
    ~BufferHandle() noexcept;

    BufferHandle(const BufferHandle&) = delete;
    BufferHandle& operator=(const BufferHandle&) = delete;

    BufferHandle(BufferHandle&& other) noexcept;
    BufferHandle& operator=(BufferHandle&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::byte* data() noexcept;
    [[nodiscard]] const std::byte* data() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t alignment() const noexcept;
    [[nodiscard]] std::span<std::byte> bytes() noexcept;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;

private:
    friend class AlignedBufferPool;

    BufferHandle(AlignedBufferPool* pool, std::size_t index) noexcept;
    void release() noexcept;

    AlignedBufferPool* pool_{nullptr};
    std::size_t index_{0};
};

}  // namespace asyncdataloader::buffer
