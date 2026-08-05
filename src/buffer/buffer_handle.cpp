#include "buffer/buffer_handle.h"

#include "buffer/aligned_buffer_pool.h"

#include <utility>

namespace asyncdataloader::buffer {

BufferHandle::BufferHandle(
    AlignedBufferPool* pool,
    std::size_t index
) noexcept
    : pool_(pool), index_(index) {}

BufferHandle::~BufferHandle() noexcept {
    release();
}

BufferHandle::BufferHandle(BufferHandle&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      index_(std::exchange(other.index_, 0)) {}

BufferHandle& BufferHandle::operator=(BufferHandle&& other) noexcept {
    if (this != &other) {
        release();
        pool_ = std::exchange(other.pool_, nullptr);
        index_ = std::exchange(other.index_, 0);
    }
    return *this;
}

bool BufferHandle::valid() const noexcept {
    return pool_ != nullptr;
}

std::byte* BufferHandle::data() noexcept {
    return valid() ? pool_->buffer_at(index_).data() : nullptr;
}

const std::byte* BufferHandle::data() const noexcept {
    if (!valid()) {
        return nullptr;
    }
    const auto* const_pool = static_cast<const AlignedBufferPool*>(pool_);
    return const_pool->buffer_at(index_).data();
}

std::size_t BufferHandle::size() const noexcept {
    return valid() ? pool_->buffer_at(index_).size() : 0;
}

std::size_t BufferHandle::alignment() const noexcept {
    return valid() ? pool_->buffer_at(index_).alignment() : 0;
}

std::span<std::byte> BufferHandle::bytes() noexcept {
    return valid() ? pool_->buffer_at(index_).bytes()
                   : std::span<std::byte>{};
}

std::span<const std::byte> BufferHandle::bytes() const noexcept {
    if (!valid()) {
        return {};
    }
    const auto* const_pool = static_cast<const AlignedBufferPool*>(pool_);
    return const_pool->buffer_at(index_).bytes();
}

void BufferHandle::release() noexcept {
    AlignedBufferPool* const owner = std::exchange(pool_, nullptr);
    const std::size_t released_index = std::exchange(index_, 0);
    if (owner != nullptr) {
        owner->release(released_index);
    }
}

}  // namespace asyncdataloader::buffer
