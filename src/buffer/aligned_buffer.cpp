#include "buffer/aligned_buffer.h"

#include <cstdlib>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace asyncdataloader::buffer {
namespace {

[[nodiscard]] bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

void validate_allocation(std::size_t size, std::size_t alignment) {
    if (size == 0) {
        throw std::invalid_argument("aligned buffer size must be greater than zero");
    }
    if (!is_power_of_two(alignment) || alignment % sizeof(void*) != 0) {
        throw std::invalid_argument(
            "aligned buffer alignment must be a power of two and a multiple "
            "of sizeof(void*)"
        );
    }
    if (size % alignment != 0) {
        throw std::invalid_argument(
            "aligned buffer size must be a multiple of alignment"
        );
    }
}

}  // namespace

AlignedBuffer::AlignedBuffer(std::size_t size, std::size_t alignment) {
    validate_allocation(size, alignment);

    void* allocation = nullptr;
    const int error_number = ::posix_memalign(&allocation, alignment, size);
    if (error_number != 0) {
        throw std::system_error(
            error_number,
            std::generic_category(),
            "posix_memalign"
        );
    }

    data_ = static_cast<std::byte*>(allocation);
    size_ = size;
    alignment_ = alignment;
}

AlignedBuffer::~AlignedBuffer() noexcept {
    std::free(data_);
}

AlignedBuffer::AlignedBuffer(AlignedBuffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      alignment_(std::exchange(other.alignment_, 0)) {}

AlignedBuffer& AlignedBuffer::operator=(AlignedBuffer&& other) noexcept {
    if (this != &other) {
        std::free(data_);
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        alignment_ = std::exchange(other.alignment_, 0);
    }
    return *this;
}

std::byte* AlignedBuffer::data() noexcept {
    return data_;
}

const std::byte* AlignedBuffer::data() const noexcept {
    return data_;
}

std::size_t AlignedBuffer::size() const noexcept {
    return size_;
}

std::size_t AlignedBuffer::alignment() const noexcept {
    return alignment_;
}

std::span<std::byte> AlignedBuffer::bytes() noexcept {
    return {data_, size_};
}

std::span<const std::byte> AlignedBuffer::bytes() const noexcept {
    return {data_, size_};
}

}  // namespace asyncdataloader::buffer
