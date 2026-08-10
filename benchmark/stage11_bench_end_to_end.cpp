#include "backend/sync_backend.h"
#include "benchmark/benchmark_cli.h"
#include "benchmark/benchmark_report.h"
#include "benchmark/end_to_end_baseline.h"
#include "config/pipeline_config.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using asyncdataloader::benchmark::TimedEndToEndResult;
using asyncdataloader::config::PipelineConfig;

constexpr std::size_t kMaxBenchmarkCapacity = 65'536;
constexpr std::size_t kBenchmarkAlignment = 4096;
constexpr std::size_t kMinimumThreeStageOverlapBuffers = 3;

struct BenchmarkOptions {
    std::filesystem::path input_path;
    std::filesystem::path serial_output_path;
    std::filesystem::path no_overlap_output_path;
    std::filesystem::path overlap_output_path;
    PipelineConfig overlap_config;
    std::size_t iterations{1};
};

void print_usage() {
    std::cerr
        << "usage: stage11_bench_end_to_end <input> <serial_output> "
           "<no_overlap_output> <overlap_output> <block_size> "
           "<overlap_max_inflight_buffers> <queue_depth> [iterations]\n";
}

bool parse_bounded_size(
    const char* text,
    std::size_t maximum,
    std::string_view label,
    std::size_t& value
) {
    if (asyncdataloader::benchmark::parse_positive_size(
            text,
            maximum,
            value
        )) {
        return true;
    }

    std::cerr << "invalid " << label << ": " << text << '\n';
    return false;
}

bool parse_options(
    int argc,
    char** argv,
    BenchmarkOptions& options
) {
    if (argc != 8 && argc != 9) {
        print_usage();
        return false;
    }

    options.input_path = argv[1];
    options.serial_output_path = argv[2];
    options.no_overlap_output_path = argv[3];
    options.overlap_output_path = argv[4];
    if (options.input_path.empty() ||
        options.serial_output_path.empty() ||
        options.no_overlap_output_path.empty() ||
        options.overlap_output_path.empty()) {
        std::cerr << "input and output paths must not be empty\n";
        return false;
    }

    if (!parse_bounded_size(
            argv[5],
            asyncdataloader::benchmark::kMaxBenchmarkBlockSize,
            "block_size",
            options.overlap_config.block_size
        ) ||
        !parse_bounded_size(
            argv[6],
            kMaxBenchmarkCapacity,
            "overlap_max_inflight_buffers",
            options.overlap_config.max_inflight_buffers
        ) ||
        !parse_bounded_size(
            argv[7],
            kMaxBenchmarkCapacity,
            "queue_depth",
            options.overlap_config.queue_depth
        )) {
        return false;
    }

    options.overlap_config.buffer_alignment = kBenchmarkAlignment;
    if (argc == 9 &&
        !parse_bounded_size(
            argv[8],
            asyncdataloader::benchmark::kMaxBenchmarkIterations,
            "iterations",
            options.iterations
        )) {
        return false;
    }
    return true;
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

void reject_same_file(
    const std::filesystem::path& left,
    const std::filesystem::path& right,
    std::string_view message
) {
    if (normalized_path(left) == normalized_path(right)) {
        throw std::invalid_argument(std::string{message});
    }

    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(left, right, error);
    if (!error && equivalent) {
        throw std::invalid_argument(std::string{message});
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::system_error(error, "compare benchmark paths");
    }
}

void validate_distinct_paths(const BenchmarkOptions& options) {
    reject_same_file(
        options.input_path,
        options.serial_output_path,
        "benchmark input and serial output must differ"
    );
    reject_same_file(
        options.input_path,
        options.no_overlap_output_path,
        "benchmark input and no-overlap output must differ"
    );
    reject_same_file(
        options.input_path,
        options.overlap_output_path,
        "benchmark input and overlap output must differ"
    );
    reject_same_file(
        options.serial_output_path,
        options.no_overlap_output_path,
        "serial and no-overlap outputs must differ"
    );
    reject_same_file(
        options.serial_output_path,
        options.overlap_output_path,
        "serial and overlap outputs must differ"
    );
    reject_same_file(
        options.no_overlap_output_path,
        options.overlap_output_path,
        "no-overlap and overlap outputs must differ"
    );
}

TimedEndToEndResult run_serial_sample(
    const BenchmarkOptions& options
) {
    return asyncdataloader::benchmark::run_timed_serial_byte_increment(
        options.input_path,
        options.serial_output_path,
        options.overlap_config
    );
}

TimedEndToEndResult run_pipeline_sample(
    const BenchmarkOptions& options,
    const std::filesystem::path& output_path,
    const PipelineConfig& config
) {
    asyncdataloader::backend::SyncBackend backend;
    return asyncdataloader::benchmark::run_timed_pipeline_byte_increment(
        options.input_path,
        output_path,
        config,
        backend
    );
}

void validate_against_serial(
    const TimedEndToEndResult& serial,
    const TimedEndToEndResult& candidate,
    std::uint64_t verified_bytes,
    std::string_view candidate_name
) {
    if (serial.blocks_written != candidate.blocks_written ||
        serial.bytes_written != candidate.bytes_written) {
        throw std::runtime_error(
            std::string{candidate_name} +
            " byte/block counts differ from serial"
        );
    }
    if (verified_bytes != serial.bytes_written) {
        throw std::runtime_error(
            std::string{candidate_name} +
            " verified length differs from processed bytes"
        );
    }
}

int run_benchmark(const BenchmarkOptions& options) {
    options.overlap_config.validate();
    if (options.overlap_config.max_inflight_buffers <
        kMinimumThreeStageOverlapBuffers) {
        throw std::invalid_argument(
            "overlap_max_inflight_buffers must be at least 3"
        );
    }
    validate_distinct_paths(options);

    PipelineConfig no_overlap_config = options.overlap_config;
    no_overlap_config.max_inflight_buffers = 1;
    no_overlap_config.validate();

    std::vector<double> serial_samples_ms;
    std::vector<double> no_overlap_samples_ms;
    std::vector<double> overlap_samples_ms;
    serial_samples_ms.reserve(options.iterations);
    no_overlap_samples_ms.reserve(options.iterations);
    overlap_samples_ms.reserve(options.iterations);

    std::uint64_t expected_blocks = 0;
    std::uint64_t expected_bytes = 0;

    for (std::size_t iteration = 0; iteration < options.iterations;
         ++iteration) {
        TimedEndToEndResult serial;
        TimedEndToEndResult no_overlap;
        TimedEndToEndResult overlap;

        // Rotate all three methods to reduce a fixed first-method cache/time
        // bias. The harness deliberately does not manipulate the page cache.
        switch (iteration % 3) {
        case 0:
            serial = run_serial_sample(options);
            no_overlap = run_pipeline_sample(
                options,
                options.no_overlap_output_path,
                no_overlap_config
            );
            overlap = run_pipeline_sample(
                options,
                options.overlap_output_path,
                options.overlap_config
            );
            break;
        case 1:
            no_overlap = run_pipeline_sample(
                options,
                options.no_overlap_output_path,
                no_overlap_config
            );
            overlap = run_pipeline_sample(
                options,
                options.overlap_output_path,
                options.overlap_config
            );
            serial = run_serial_sample(options);
            break;
        default:
            overlap = run_pipeline_sample(
                options,
                options.overlap_output_path,
                options.overlap_config
            );
            serial = run_serial_sample(options);
            no_overlap = run_pipeline_sample(
                options,
                options.no_overlap_output_path,
                no_overlap_config
            );
            break;
        }

        const std::uint64_t no_overlap_verified =
            asyncdataloader::benchmark::verify_files_equal_bounded(
                options.serial_output_path,
                options.no_overlap_output_path,
                options.overlap_config
            );
        const std::uint64_t overlap_verified =
            asyncdataloader::benchmark::verify_files_equal_bounded(
                options.serial_output_path,
                options.overlap_output_path,
                options.overlap_config
            );
        validate_against_serial(
            serial,
            no_overlap,
            no_overlap_verified,
            "no-overlap pipeline"
        );
        validate_against_serial(
            serial,
            overlap,
            overlap_verified,
            "overlap pipeline"
        );

        if (iteration == 0) {
            expected_blocks = serial.blocks_written;
            expected_bytes = serial.bytes_written;
        } else if (serial.blocks_written != expected_blocks ||
                   serial.bytes_written != expected_bytes) {
            throw std::runtime_error(
                "benchmark input size changed between iterations"
            );
        }

        serial_samples_ms.push_back(serial.elapsed_ms);
        no_overlap_samples_ms.push_back(no_overlap.elapsed_ms);
        overlap_samples_ms.push_back(overlap.elapsed_ms);
    }

    const auto serial_report =
        asyncdataloader::benchmark::make_benchmark_report(
            "serial_sync_byte_increment",
            expected_bytes,
            expected_bytes,
            serial_samples_ms
        );
    const auto no_overlap_report =
        asyncdataloader::benchmark::make_benchmark_report(
            "pipeline_sync_no_overlap_byte_increment",
            expected_bytes,
            expected_bytes,
            no_overlap_samples_ms
        );
    const auto overlap_report =
        asyncdataloader::benchmark::make_benchmark_report(
            "pipeline_sync_overlap_byte_increment",
            expected_bytes,
            expected_bytes,
            overlap_samples_ms
        );

    asyncdataloader::benchmark::write_benchmark_csv_header(std::cout);
    asyncdataloader::benchmark::write_benchmark_csv_row(
        std::cout,
        serial_report
    );
    asyncdataloader::benchmark::write_benchmark_csv_row(
        std::cout,
        no_overlap_report
    );
    asyncdataloader::benchmark::write_benchmark_csv_row(
        std::cout,
        overlap_report
    );

    std::cerr
        << "comparison_design=serial_oracle_plus_overlap_ablation\n"
        << "processing_stage=byte_increment\n"
        << "serial_execution=one_thread_blocking_pread_process_pwrite\n"
        << "pipeline_execution=reader_processor_writer_jthreads\n"
        << "read_backend=sync\n"
        << "coroutine_behavior=sync_task_completes_in_reader_thread\n"
        << "writer_io=blocking_pwrite\n"
        << "no_overlap_max_inflight_buffers=1\n"
        << "overlap_max_inflight_buffers="
        << options.overlap_config.max_inflight_buffers << '\n'
        << "queue_depth=" << options.overlap_config.queue_depth << '\n'
        << "timing_boundary=execution_plus_output_fsync\n"
        << "excluded_from_timing=open_close_and_output_verification\n"
        << "cache_policy=not_modified_by_harness\n"
        << "execution_order=rotating_three_methods\n"
        << "pipeline_metrics=enabled\n"
        << "output_verified=true\n"
        << "verified_bytes=" << expected_bytes << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        BenchmarkOptions options;
        if (!parse_options(argc, argv, options)) {
            return 1;
        }
        return run_benchmark(options);
    } catch (const std::exception& error) {
        std::cerr << "stage11 end-to-end benchmark failed: "
                  << error.what() << '\n';
        return 2;
    }
}
