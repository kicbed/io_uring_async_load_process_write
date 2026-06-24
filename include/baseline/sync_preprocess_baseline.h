#pragma once

#include <cstddef>
#include <cstdint>

namespace asyncdataloader::baseline {

struct SyncPreprocessConfig {
    const char* input_path{nullptr};
    const char* output_path{nullptr};
    std::size_t block_size{0};
};

struct SyncPreprocessResult {
    std::uint64_t bytes_read{0};
    std::uint64_t bytes_written{0};
    int error_number{0};
};

[[nodiscard]] SyncPreprocessResult run_sync_preprocess_baseline(
    const SyncPreprocessConfig& config
);

}  // namespace asyncdataloader::baseline
