#include "buffer/aligned_buffer.h"
#include "config/pipeline_config.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage8 AlignedBuffer test failed: " << message << '\n';
    return 1;
}

bool rejects_invalid_allocation(std::size_t size, std::size_t alignment) {
    try {
        const asyncdataloader::buffer::AlignedBuffer buffer(size, alignment);
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace

int main() {
    using asyncdataloader::buffer::AlignedBuffer;
    using asyncdataloader::config::PipelineConfig;

    static_assert(!std::is_copy_constructible_v<AlignedBuffer>);
    static_assert(!std::is_copy_assignable_v<AlignedBuffer>);
    static_assert(std::is_nothrow_move_constructible_v<AlignedBuffer>);
    static_assert(std::is_nothrow_move_assignable_v<AlignedBuffer>);

    PipelineConfig config;
    config.block_size = 8192;
    config.max_inflight_buffers = 2;
    config.queue_depth = 1;
    config.buffer_alignment = 4096;
    config.validate();

    AlignedBuffer original(config.block_size, config.buffer_alignment);
    if (original.data() == nullptr || original.size() != config.block_size ||
        original.alignment() != config.buffer_alignment) {
        return fail("constructed buffer has incorrect metadata");
    }

    const auto address = reinterpret_cast<std::uintptr_t>(original.data());
    if (address % original.alignment() != 0) {
        return fail("allocated address does not satisfy alignment");
    }

    original.bytes().front() = std::byte{0x2A};
    original.bytes().back() = std::byte{0x7F};
    const AlignedBuffer& const_view = original;
    if (const_view.bytes().front() != std::byte{0x2A} ||
        const_view.bytes().back() != std::byte{0x7F}) {
        return fail("buffer byte view did not preserve written data");
    }

    std::byte* const original_address = original.data();
    AlignedBuffer moved(std::move(original));
    if (moved.data() != original_address || moved.size() != config.block_size) {
        return fail("move construction did not transfer allocation ownership");
    }
    if (original.data() != nullptr || original.size() != 0 ||
        !original.bytes().empty()) {
        return fail("move construction did not empty the source buffer");
    }

    AlignedBuffer assigned(4096, 4096);
    assigned = std::move(moved);
    if (assigned.data() != original_address ||
        assigned.bytes().front() != std::byte{0x2A}) {
        return fail("move assignment did not transfer allocation ownership");
    }
    if (moved.data() != nullptr || moved.size() != 0 ||
        !moved.bytes().empty()) {
        return fail("move assignment did not empty the source buffer");
    }

    if (!rejects_invalid_allocation(0, 4096)) {
        return fail("zero-sized allocation was accepted");
    }
    if (!rejects_invalid_allocation(4096, 24)) {
        return fail("non-power-of-two alignment was accepted");
    }
    if (!rejects_invalid_allocation(4096, 1)) {
        return fail("alignment smaller than sizeof(void*) was accepted");
    }
    if (!rejects_invalid_allocation(4097, 4096)) {
        return fail("size that is not alignment-multiple was accepted");
    }

    return 0;
}
