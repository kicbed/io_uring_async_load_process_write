#include "config/pipeline_config.h"

#include <limits>
#include <stdexcept>

namespace asyncdataloader::config {
namespace {

[[nodiscard]] bool is_power_of_two(std::size_t value) noexcept {
    return value != 0 && (value & (value - 1)) == 0;
}

}  // namespace

void PipelineConfig::validate() const {
    if (block_size == 0) {
        throw std::invalid_argument("block_size must be greater than zero");
    }
    if (max_inflight_buffers == 0) {
        throw std::invalid_argument(
            "max_inflight_buffers must be greater than zero"
        );
    }
    if (queue_depth == 0) {
        throw std::invalid_argument("queue_depth must be greater than zero");
    }
    if (!is_power_of_two(buffer_alignment) ||
        buffer_alignment % sizeof(void*) != 0) {
        throw std::invalid_argument(
            "buffer_alignment must be a power of two and a multiple of "
            "sizeof(void*)"
        );
    }
    if (block_size % buffer_alignment != 0) {
        throw std::invalid_argument(
            "block_size must be a multiple of buffer_alignment"
        );
    }
    if (block_size >
        std::numeric_limits<std::size_t>::max() / max_inflight_buffers) {
        throw std::overflow_error(
            "block_size * max_inflight_buffers exceeds size_t"
        );
    }
}

std::size_t PipelineConfig::buffer_pool_bytes() const {
    validate();
    return block_size * max_inflight_buffers;
}

}  // namespace asyncdataloader::config
