#pragma once

#include "config/pipeline_config.h"

#include <cstdint>
#include <filesystem>

namespace asyncdataloader::pipeline {
class Pipeline;
}  // namespace asyncdataloader::pipeline

namespace asyncdataloader::backend {
class IOBackend;
}  // namespace asyncdataloader::backend

namespace asyncdataloader::benchmark {

struct SerialPipelineResult {
    std::uint64_t blocks_written{0};
    std::uint64_t bytes_written{0};
};

struct TimedEndToEndResult {
    std::uint64_t blocks_written{0};
    std::uint64_t bytes_written{0};
    double elapsed_ms{0.0};
};

// Runs the registered processing stages in a serial read-process-write loop.
// This is a bounded correctness/performance reference, not the final pipeline.
// The caller owns both already-open descriptors and must truncate output_fd
// before the call. Durability is also caller-controlled so the benchmark can
// apply the same fsync timing boundary to serial and pipelined executions.
[[nodiscard]] SerialPipelineResult run_serial_pipeline_reference(
    int input_fd,
    int output_fd,
    const config::PipelineConfig& config,
    pipeline::Pipeline& processing_pipeline
);

// Runs the common ByteIncrement workload through the serial reference. File
// open/close are outside the timer; processing, writes, and output fsync are
// inside it. The input and output paths must identify different files.
[[nodiscard]] TimedEndToEndResult run_timed_serial_byte_increment(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const config::PipelineConfig& config
);

// Runs the same ByteIncrement workload through PipelineExecutor with the
// caller-selected read backend. It uses the same timing boundary as the serial
// helper so Stage 11 comparisons do not accidentally time different work.
[[nodiscard]] TimedEndToEndResult run_timed_pipeline_byte_increment(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const config::PipelineConfig& config,
    backend::IOBackend& read_backend
);

// Compares two files byte-for-byte with two fixed-size aligned buffers.
// Returns the verified byte count, or throws on an I/O or content mismatch.
[[nodiscard]] std::uint64_t verify_files_equal_bounded(
    const std::filesystem::path& left_path,
    const std::filesystem::path& right_path,
    const config::PipelineConfig& config
);

}  // namespace asyncdataloader::benchmark
