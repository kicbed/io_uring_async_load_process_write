#include "pipeline/block_work_item.h"

#include <stdexcept>
#include <utility>

namespace asyncdataloader::pipeline {

BlockWorkItem::BlockWorkItem(
    std::uint64_t block_index,
    std::uint64_t file_offset,
    std::size_t valid_bytes,
    buffer::BufferHandle buffer
)
    : block_index_(block_index),
      file_offset_(file_offset),
      valid_bytes_(valid_bytes),
      buffer_(std::move(buffer)) {
    if (!buffer_.valid()) {
        throw std::invalid_argument(
            "BlockWorkItem requires a valid BufferHandle"
        );
    }
    if (valid_bytes_ == 0) {
        throw std::invalid_argument(
            "BlockWorkItem valid_bytes must be greater than zero"
        );
    }
    if (valid_bytes_ > buffer_.size()) {
        throw std::invalid_argument(
            "BlockWorkItem valid_bytes exceeds buffer capacity"
        );
    }
}

BlockWorkItem::BlockWorkItem(BlockWorkItem&& other) noexcept
    : block_index_(std::exchange(other.block_index_, 0)),
      file_offset_(std::exchange(other.file_offset_, 0)),
      valid_bytes_(std::exchange(other.valid_bytes_, 0)),
      buffer_(std::move(other.buffer_)) {}

BlockWorkItem& BlockWorkItem::operator=(BlockWorkItem&& other) noexcept {
    if (this != &other) {
        block_index_ = std::exchange(other.block_index_, 0);
        file_offset_ = std::exchange(other.file_offset_, 0);
        valid_bytes_ = std::exchange(other.valid_bytes_, 0);
        buffer_ = std::move(other.buffer_);
    }
    return *this;
}

bool BlockWorkItem::valid() const noexcept {
    return buffer_.valid();
}

std::uint64_t BlockWorkItem::block_index() const noexcept {
    return block_index_;
}

std::uint64_t BlockWorkItem::file_offset() const noexcept {
    return file_offset_;
}

std::size_t BlockWorkItem::valid_bytes() const noexcept {
    return valid_bytes_;
}

std::size_t BlockWorkItem::capacity() const noexcept {
    return buffer_.size();
}

std::span<std::byte> BlockWorkItem::valid_data() noexcept {
    return buffer_.bytes().first(valid_bytes_);
}

std::span<const std::byte> BlockWorkItem::valid_data() const noexcept {
    return buffer_.bytes().first(valid_bytes_);
}

}  // namespace asyncdataloader::pipeline
