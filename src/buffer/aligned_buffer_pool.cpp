#include "buffer/aligned_buffer_pool.h"

#include <cassert>

namespace asyncdataloader::buffer {

AlignedBufferPool::AlignedBufferPool(const config::PipelineConfig& config) {
    config.validate();

    buffers_.reserve(config.max_inflight_buffers);
    available_indices_.reserve(config.max_inflight_buffers);
    in_use_.resize(config.max_inflight_buffers, 0);

    for (std::size_t index = 0; index < config.max_inflight_buffers; ++index) {
        buffers_.emplace_back(config.block_size, config.buffer_alignment);
    }

    for (std::size_t index = config.max_inflight_buffers; index > 0; --index) {
        available_indices_.push_back(index - 1);
    }
}

AlignedBufferPool::~AlignedBufferPool() noexcept {
    const std::lock_guard lock(mutex_);
    assert(
        available_indices_.size() == buffers_.size() &&
        "AlignedBufferPool destroyed with outstanding BufferHandle"
    );
}

std::optional<BufferHandle> AlignedBufferPool::try_acquire() {
    const std::lock_guard lock(mutex_);
    if (available_indices_.empty()) {
        return std::nullopt;
    }

    return take_available_locked();
}

BufferHandle AlignedBufferPool::acquire() {
    std::unique_lock lock(mutex_);
    available_cv_.wait(lock, [this] {
        return !available_indices_.empty();
    });

    return take_available_locked();
}

BufferHandle AlignedBufferPool::take_available_locked() noexcept {
    assert(!available_indices_.empty());
    const std::size_t index = available_indices_.back();
    available_indices_.pop_back();
    assert(index < in_use_.size());
    assert(in_use_[index] == 0);
    in_use_[index] = 1;

    return BufferHandle{this, index};
}

std::size_t AlignedBufferPool::capacity() const noexcept {
    return buffers_.size();
}

std::size_t AlignedBufferPool::available() const {
    const std::lock_guard lock(mutex_);
    return available_indices_.size();
}

AlignedBuffer& AlignedBufferPool::buffer_at(std::size_t index) noexcept {
    assert(index < buffers_.size());
    return buffers_[index];
}

const AlignedBuffer& AlignedBufferPool::buffer_at(
    std::size_t index
) const noexcept {
    assert(index < buffers_.size());
    return buffers_[index];
}

void AlignedBufferPool::release(std::size_t index) noexcept {
    {
        const std::lock_guard lock(mutex_);
        assert(index < in_use_.size());
        assert(in_use_[index] == 1);

        in_use_[index] = 0;
        available_indices_.push_back(index);
    }
    available_cv_.notify_one();
}

}  // namespace asyncdataloader::buffer
