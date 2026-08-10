#include "benchmark/end_to_end_baseline.h"

#include "backend/io_backend.h"
#include "buffer/aligned_buffer.h"
#include "metrics/metrics_registry.h"
#include "pipeline/builtin_stages.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_executor.h"
#include "util/fd_guard.h"
#include "util/file_io.h"
#include "util/timer.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <fcntl.h>

namespace asyncdataloader::benchmark {
namespace {

std::uint64_t size_to_u64(std::size_t value) {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value > static_cast<std::size_t>(
                        std::numeric_limits<std::uint64_t>::max())) {
            throw std::overflow_error("benchmark byte count exceeds uint64_t");
        }
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t checked_add(
    std::uint64_t left,
    std::uint64_t right,
    const char* message
) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error(message);
    }
    return left + right;
}

util::OpenFileResult open_comparison_input(
    const std::filesystem::path& path,
    const char* operation
) {
    util::OpenFileResult result = util::open_read_only(path.c_str());
    if (result.error_number != 0) {
        throw std::system_error(
            result.error_number,
            std::generic_category(),
            operation
        );
    }
    return result;
}

std::filesystem::path normalized_path(
    const std::filesystem::path& path
) {
    std::error_code error;
    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(path, error);
    if (error) {
        throw std::system_error(error, "normalize benchmark path");
    }
    return normalized;
}

void reject_same_input_and_output(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path
) {
    if (normalized_path(input_path) == normalized_path(output_path)) {
        throw std::invalid_argument(
            "benchmark input and output must differ"
        );
    }

    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(
        input_path,
        output_path,
        error
    );
    if (!error && equivalent) {
        throw std::invalid_argument(
            "benchmark input and output must differ"
        );
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::system_error(error, "compare benchmark paths");
    }
}

util::FdGuard open_truncated_output(
    const std::filesystem::path& path
) {
    const int raw_fd = ::open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0644
    );
    if (raw_fd < 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "open benchmark output"
        );
    }
    return util::FdGuard{raw_fd};
}

void fsync_output(int fd) {
    const util::FsyncResult result = util::fsync_fd(fd);
    if (result.error_number != 0) {
        throw std::system_error(
            result.error_number,
            std::generic_category(),
            "fsync benchmark output"
        );
    }
}

}  // namespace

SerialPipelineResult run_serial_pipeline_reference(
    int input_fd,
    int output_fd,
    const config::PipelineConfig& config,
    pipeline::Pipeline& processing_pipeline
) {
    config.validate();
    if (input_fd < 0 || output_fd < 0) {
        throw std::invalid_argument(
            "serial reference descriptors must be valid"
        );
    }
    if (input_fd == output_fd) {
        throw std::invalid_argument(
            "serial reference input and output descriptors must differ"
        );
    }
    if (processing_pipeline.stage_count() == 0) {
        throw std::invalid_argument(
            "serial reference requires at least one processing stage"
        );
    }

    buffer::AlignedBuffer block(
        config.block_size,
        config.buffer_alignment
    );
    SerialPipelineResult result;
    std::uint64_t file_offset = 0;

    while (true) {
        const util::ReadAtResult read_result = util::read_at(
            input_fd,
            block.data(),
            block.size(),
            file_offset
        );
        if (read_result.error_number != 0) {
            throw std::system_error(
                read_result.error_number,
                std::generic_category(),
                "serial reference pread"
            );
        }
        if (read_result.bytes_read == 0) {
            return result;
        }

        const std::span<std::byte> valid_data =
            block.bytes().first(read_result.bytes_read);
        processing_pipeline.process(valid_data);

        const util::WriteAtResult write_result = util::write_all_at(
            output_fd,
            valid_data.data(),
            valid_data.size(),
            file_offset
        );
        if (write_result.error_number != 0) {
            throw std::system_error(
                write_result.error_number,
                std::generic_category(),
                "serial reference pwrite"
            );
        }
        if (write_result.bytes_written != valid_data.size()) {
            throw std::runtime_error(
                "serial reference did not write the complete block"
            );
        }

        const std::uint64_t block_bytes = size_to_u64(valid_data.size());
        result.bytes_written = checked_add(
            result.bytes_written,
            block_bytes,
            "serial reference written-byte count overflow"
        );
        result.blocks_written = checked_add(
            result.blocks_written,
            1,
            "serial reference block count overflow"
        );
        file_offset = checked_add(
            file_offset,
            block_bytes,
            "serial reference file offset overflow"
        );
    }
}

TimedEndToEndResult run_timed_serial_byte_increment(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const config::PipelineConfig& config
) {
    config.validate();
    reject_same_input_and_output(input_path, output_path);

    util::OpenFileResult input = open_comparison_input(
        input_path,
        "open benchmark input"
    );
    util::FdGuard output = open_truncated_output(output_path);

    metrics::MetricsRegistry metrics;
    pipeline::Pipeline processing(metrics);
    processing.add_stage(
        std::make_unique<pipeline::ByteIncrementStage>()
    );

    util::Timer timer;
    const SerialPipelineResult result = run_serial_pipeline_reference(
        input.fd.get(),
        output.get(),
        config,
        processing
    );
    fsync_output(output.get());

    return TimedEndToEndResult{
        result.blocks_written,
        result.bytes_written,
        timer.elapsed_ms(),
    };
}

TimedEndToEndResult run_timed_pipeline_byte_increment(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    const config::PipelineConfig& config,
    backend::IOBackend& read_backend
) {
    config.validate();
    reject_same_input_and_output(input_path, output_path);

    util::OpenFileResult input = open_comparison_input(
        input_path,
        "open benchmark input"
    );
    util::FdGuard output = open_truncated_output(output_path);

    metrics::MetricsRegistry metrics;
    pipeline::Pipeline processing(metrics);
    processing.add_stage(
        std::make_unique<pipeline::ByteIncrementStage>()
    );
    pipeline::PipelineExecutor executor(
        config,
        read_backend,
        processing,
        metrics
    );

    util::Timer timer;
    const pipeline::PipelineRunResult result = executor.run(
        input.fd.get(),
        output.get()
    );
    fsync_output(output.get());

    return TimedEndToEndResult{
        result.blocks_written,
        result.bytes_written,
        timer.elapsed_ms(),
    };
}

std::uint64_t verify_files_equal_bounded(
    const std::filesystem::path& left_path,
    const std::filesystem::path& right_path,
    const config::PipelineConfig& config
) {
    config.validate();
    util::OpenFileResult left = open_comparison_input(
        left_path,
        "open left benchmark output"
    );
    util::OpenFileResult right = open_comparison_input(
        right_path,
        "open right benchmark output"
    );

    buffer::AlignedBuffer left_block(
        config.block_size,
        config.buffer_alignment
    );
    buffer::AlignedBuffer right_block(
        config.block_size,
        config.buffer_alignment
    );
    std::uint64_t file_offset = 0;

    while (true) {
        const util::ReadAtResult left_read = util::read_at(
            left.fd.get(),
            left_block.data(),
            left_block.size(),
            file_offset
        );
        if (left_read.error_number != 0) {
            throw std::system_error(
                left_read.error_number,
                std::generic_category(),
                "read left benchmark output"
            );
        }

        const util::ReadAtResult right_read = util::read_at(
            right.fd.get(),
            right_block.data(),
            right_block.size(),
            file_offset
        );
        if (right_read.error_number != 0) {
            throw std::system_error(
                right_read.error_number,
                std::generic_category(),
                "read right benchmark output"
            );
        }

        if (left_read.bytes_read != right_read.bytes_read) {
            throw std::runtime_error(
                "benchmark outputs have different lengths at byte offset " +
                std::to_string(file_offset)
            );
        }
        if (left_read.bytes_read == 0) {
            return file_offset;
        }

        const std::span<const std::byte> left_bytes =
            left_block.bytes().first(left_read.bytes_read);
        const std::span<const std::byte> right_bytes =
            right_block.bytes().first(right_read.bytes_read);
        const auto [left_mismatch, right_mismatch] = std::mismatch(
            left_bytes.begin(),
            left_bytes.end(),
            right_bytes.begin()
        );
        static_cast<void>(right_mismatch);
        if (left_mismatch != left_bytes.end()) {
            const auto local_offset = static_cast<std::uint64_t>(
                left_mismatch - left_bytes.begin()
            );
            throw std::runtime_error(
                "benchmark outputs differ at byte offset " +
                std::to_string(checked_add(
                    file_offset,
                    local_offset,
                    "benchmark mismatch offset overflow"
                ))
            );
        }

        file_offset = checked_add(
            file_offset,
            size_to_u64(left_read.bytes_read),
            "benchmark verification offset overflow"
        );
    }
}

}  // namespace asyncdataloader::benchmark
