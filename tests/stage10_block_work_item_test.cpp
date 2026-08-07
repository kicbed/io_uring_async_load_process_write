#include "buffer/aligned_buffer_pool.h"
#include "pipeline/block_work_item.h"

#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage10 BlockWorkItem test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::buffer::AlignedBufferPool;
    using asyncdataloader::buffer::BufferHandle;
    using asyncdataloader::config::PipelineConfig;
    using asyncdataloader::pipeline::BlockWorkItem;

    static_assert(!std::is_copy_constructible_v<BlockWorkItem>);
    static_assert(!std::is_copy_assignable_v<BlockWorkItem>);
    static_assert(std::is_nothrow_move_constructible_v<BlockWorkItem>);
    static_assert(std::is_nothrow_move_assignable_v<BlockWorkItem>);
    static_assert(std::is_same_v<
                  decltype(std::declval<BlockWorkItem&>().valid_data()),
                  std::span<std::byte>>);
    static_assert(std::is_same_v<
                  decltype(std::declval<const BlockWorkItem&>().valid_data()),
                  std::span<const std::byte>>);

    PipelineConfig config;
    config.block_size = 4096;
    config.max_inflight_buffers = 2;
    config.queue_depth = 1;
    config.buffer_alignment = 4096;

    AlignedBufferPool pool(config);

    bool zero_length_rejected = false;
    try {
        BlockWorkItem invalid{0, 0, 0, pool.acquire()};
    } catch (const std::invalid_argument&) {
        zero_length_rejected = true;
    }
    if (!zero_length_rejected || pool.available() != pool.capacity()) {
        return fail("zero valid-byte count was accepted or leaked its lease");
    }

    bool oversized_length_rejected = false;
    try {
        BlockWorkItem invalid{
            0,
            0,
            config.block_size + 1,
            pool.acquire()
        };
    } catch (const std::invalid_argument&) {
        oversized_length_rejected = true;
    }
    if (!oversized_length_rejected || pool.available() != pool.capacity()) {
        return fail("oversized valid-byte count was accepted or leaked its lease");
    }

    {
        BufferHandle moved_from = pool.acquire();
        BufferHandle owner(std::move(moved_from));
        bool invalid_handle_rejected = false;
        try {
            BlockWorkItem invalid{0, 0, 1, std::move(moved_from)};
        } catch (const std::invalid_argument&) {
            invalid_handle_rejected = true;
        }
        if (!invalid_handle_rejected || pool.available() != 1) {
            return fail("an empty BufferHandle was accepted or ownership changed");
        }
    }

    if (pool.available() != pool.capacity()) {
        return fail("invalid-handle test did not return its live lease");
    }

    {
        BufferHandle first_lease = pool.acquire();
        std::byte* const first_address = first_lease.data();
        BlockWorkItem item{3, 12288, 3, std::move(first_lease)};

        if (first_lease.valid()) {
            return fail("construction did not consume the source lease");
        }
        if (!item.valid() || item.block_index() != 3 ||
            item.file_offset() != 12288 || item.valid_bytes() != 3 ||
            item.capacity() != config.block_size) {
            return fail("constructed work-item metadata is incorrect");
        }
        if (item.valid_data().size() != 3 ||
            item.valid_data().data() != first_address) {
            return fail("valid_data did not expose the valid buffer prefix");
        }

        item.valid_data()[0] = std::byte{0x11};
        item.valid_data()[2] = std::byte{0x33};
        const BlockWorkItem& const_item = item;
        if (const_item.valid_data()[0] != std::byte{0x11} ||
            const_item.valid_data()[2] != std::byte{0x33}) {
            return fail("const valid_data did not preserve written bytes");
        }

        BlockWorkItem moved(std::move(item));
        if (item.valid() || item.block_index() != 0 ||
            item.file_offset() != 0 || item.valid_bytes() != 0 ||
            item.capacity() != 0 || !item.valid_data().empty()) {
            return fail("move construction did not clear the source item");
        }
        if (!moved.valid() || moved.block_index() != 3 ||
            moved.file_offset() != 12288 || moved.valid_bytes() != 3 ||
            moved.valid_data().data() != first_address) {
            return fail("move construction did not preserve the work item");
        }

        BufferHandle second_lease = pool.acquire();
        std::byte* const second_address = second_lease.data();
        BlockWorkItem destination{4, 16384, 2, std::move(second_lease)};
        destination = std::move(moved);

        if (moved.valid() || moved.block_index() != 0 ||
            moved.file_offset() != 0 || moved.valid_bytes() != 0) {
            return fail("move assignment did not clear the source item");
        }
        if (!destination.valid() || destination.block_index() != 3 ||
            destination.file_offset() != 12288 ||
            destination.valid_bytes() != 3 ||
            destination.valid_data().data() != first_address) {
            return fail("move assignment did not transfer metadata and lease");
        }
        if (pool.available() != 1) {
            return fail("move assignment did not release the old destination lease");
        }

        auto recycled = pool.try_acquire();
        if (!recycled.has_value() || recycled->data() != second_address) {
            return fail("the replaced destination lease was not recycled");
        }
    }

    if (pool.available() != pool.capacity()) {
        return fail("scope exit did not return every work-item lease");
    }

    return 0;
}
