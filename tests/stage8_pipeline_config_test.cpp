#include "config/pipeline_config.h"

#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage8 PipelineConfig test failed: " << message << '\n';
    return 1;
}

bool rejects_as_invalid_argument(
    const asyncdataloader::config::PipelineConfig& config
) {
    try {
        config.validate();
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace

int main() {
    using asyncdataloader::config::PipelineConfig;

    const PipelineConfig default_config;
    default_config.validate();
    if (default_config.buffer_pool_bytes() != 8U * 1024U * 1024U) {
        return fail("default buffer memory budget is incorrect");
    }

    PipelineConfig custom_config;
    custom_config.block_size = 4096;
    custom_config.max_inflight_buffers = 3;
    custom_config.queue_depth = 2;
    custom_config.buffer_alignment = 4096;
    if (custom_config.buffer_pool_bytes() != 12U * 1024U) {
        return fail("custom buffer memory budget is incorrect");
    }

    PipelineConfig invalid_config = custom_config;
    invalid_config.block_size = 0;
    if (!rejects_as_invalid_argument(invalid_config)) {
        return fail("zero block_size was accepted");
    }

    invalid_config = custom_config;
    invalid_config.max_inflight_buffers = 0;
    if (!rejects_as_invalid_argument(invalid_config)) {
        return fail("zero max_inflight_buffers was accepted");
    }

    invalid_config = custom_config;
    invalid_config.queue_depth = 0;
    if (!rejects_as_invalid_argument(invalid_config)) {
        return fail("zero queue_depth was accepted");
    }

    invalid_config = custom_config;
    invalid_config.buffer_alignment = 24;
    if (!rejects_as_invalid_argument(invalid_config)) {
        return fail("non-power-of-two buffer_alignment was accepted");
    }

    invalid_config = custom_config;
    invalid_config.block_size = 4097;
    if (!rejects_as_invalid_argument(invalid_config)) {
        return fail("misaligned block_size was accepted");
    }

    PipelineConfig overflow_config = custom_config;
    const auto maximum = std::numeric_limits<std::size_t>::max();
    overflow_config.block_size = maximum - (maximum % 4096U);
    overflow_config.max_inflight_buffers = 2;
    try {
        static_cast<void>(overflow_config.buffer_pool_bytes());
        return fail("overflowing buffer memory budget was accepted");
    } catch (const std::overflow_error&) {
    } catch (...) {
        return fail("overflowing budget used the wrong exception type");
    }

    return 0;
}
