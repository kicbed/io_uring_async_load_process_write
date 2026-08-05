#pragma once

#include <cstddef>

namespace asyncdataloader::config {

struct PipelineConfig {
    std::size_t block_size{1024U * 1024U};
    std::size_t max_inflight_buffers{8};
    std::size_t queue_depth{4};
    std::size_t buffer_alignment{4096};

    // Rejects values that cannot form a bounded, aligned pipeline layout.
    void validate() const;

    // Returns buffer payload bytes only; allocator and queue overhead are not
    // included.
    [[nodiscard]] std::size_t buffer_pool_bytes() const;
};

}  // namespace asyncdataloader::config
