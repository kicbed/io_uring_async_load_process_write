#include "buffer/aligned_buffer_pool.h"

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage8 BufferHandle test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::buffer::AlignedBufferPool;
    using asyncdataloader::buffer::BufferHandle;
    using asyncdataloader::config::PipelineConfig;

    static_assert(!std::is_copy_constructible_v<BufferHandle>);
    static_assert(!std::is_copy_assignable_v<BufferHandle>);
    static_assert(std::is_nothrow_move_constructible_v<BufferHandle>);
    static_assert(std::is_nothrow_move_assignable_v<BufferHandle>);
    static_assert(!std::is_copy_constructible_v<AlignedBufferPool>);
    static_assert(!std::is_move_constructible_v<AlignedBufferPool>);

    PipelineConfig config;
    config.block_size = 4096;
    config.max_inflight_buffers = 2;
    config.queue_depth = 1;
    config.buffer_alignment = 4096;

    AlignedBufferPool pool(config);
    if (pool.capacity() != 2 || pool.available() != 2) {
        return fail("pool did not preallocate configured capacity");
    }

    {
        auto first = pool.try_acquire();
        auto second = pool.try_acquire();
        if (!first.has_value() || !second.has_value()) {
            return fail("pool did not provide all configured buffers");
        }
        if (pool.available() != 0 || pool.try_acquire().has_value()) {
            return fail("exhausted pool provided an extra buffer");
        }
        if (first->data() == second->data()) {
            return fail("two active handles refer to the same buffer");
        }
        if (first->size() != config.block_size ||
            first->alignment() != config.buffer_alignment) {
            return fail("handle did not expose buffer metadata");
        }

        first->bytes().front() = std::byte{0x2A};
        const BufferHandle& const_first = *first;
        if (const_first.bytes().front() != std::byte{0x2A}) {
            return fail("handle byte view did not preserve written data");
        }

        std::byte* const first_address = first->data();
        BufferHandle first_owner(std::move(*first));
        if (!first_owner.valid() || first->valid()) {
            return fail("move construction did not transfer lease ownership");
        }
        if (pool.available() != 0) {
            return fail("moving a handle returned its buffer too early");
        }

        BufferHandle second_owner(std::move(*second));
        first_owner = std::move(second_owner);
        if (second_owner.valid() || pool.available() != 1) {
            return fail("move assignment did not return the previous lease");
        }

        auto recycled = pool.try_acquire();
        if (!recycled.has_value() || recycled->data() != first_address) {
            return fail("released buffer was not made available again");
        }
        recycled.reset();
        if (pool.available() != 1) {
            return fail("destroying an optional handle did not return its buffer");
        }
    }

    if (pool.available() != pool.capacity()) {
        return fail("scope exit did not return every buffer");
    }

    try {
        auto lease = pool.try_acquire();
        if (!lease.has_value()) {
            return fail("pool had no buffer for exception-path test");
        }
        throw std::runtime_error("exercise stack unwinding");
    } catch (const std::runtime_error&) {
    }

    if (pool.available() != pool.capacity()) {
        return fail("exception unwinding did not return the leased buffer");
    }

    return 0;
}
