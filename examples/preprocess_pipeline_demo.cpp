#include "backend/backend_factory.h"
#include "backend/io_backend.h"
#include "config/pipeline_config.h"
#include "metrics/metrics_registry.h"
#include "pipeline/builtin_stages.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_executor.h"
#include "reporting/pipeline_reporter.h"
#include "util/file_io.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {

using asyncdataloader::backend::BackendConfig;
using asyncdataloader::backend::BackendFactory;
using asyncdataloader::backend::BackendKind;
using asyncdataloader::config::PipelineConfig;
using asyncdataloader::metrics::MetricsRegistry;
using asyncdataloader::pipeline::ByteIncrementStage;
using asyncdataloader::pipeline::Pipeline;
using asyncdataloader::pipeline::PipelineExecutor;
using asyncdataloader::reporting::LiveTerminalReporter;
using asyncdataloader::reporting::PipelineRunReport;

using Clock = std::chrono::steady_clock;

struct Options {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    PipelineConfig pipeline;
    BackendKind backend_kind{BackendKind::Auto};
    std::size_t thread_workers{2};
    std::size_t report_interval_ms{250};
    std::optional<std::filesystem::path> metrics_json_path;
    bool disable_uring{false};
    bool disable_thread_pool{false};
};

void print_usage(std::ostream& output, std::string_view program) {
    output
        << "Usage: " << program
        << " INPUT OUTPUT [options]\n"
        << "Options:\n"
        << "  --backend=auto|uring|threadpool|sync\n"
        << "  --block-size=BYTES\n"
        << "  --buffers=COUNT\n"
        << "  --queue-depth=COUNT\n"
        << "  --alignment=BYTES\n"
        << "  --thread-workers=COUNT\n"
        << "  --report-ms=MILLISECONDS  (0 disables live lines)\n"
        << "  --metrics-json=PATH        (optional final snapshot)\n"
        << "  --disable-uring            (Auto mode only)\n"
        << "  --disable-threadpool       (Auto mode only)\n";
}

std::size_t parse_size(
    std::string_view text,
    std::string_view option,
    bool allow_zero = false
) {
    std::size_t value = 0;
    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value
    );
    if (error != std::errc{} || end != text.data() + text.size() ||
        (!allow_zero && value == 0)) {
        throw std::invalid_argument(
            std::string(option) + " requires " +
            (allow_zero ? "a non-negative" : "a positive") +
            " integer"
        );
    }
    return value;
}

BackendKind parse_backend(std::string_view value) {
    if (value == "auto") {
        return BackendKind::Auto;
    }
    if (value == "uring") {
        return BackendKind::Uring;
    }
    if (value == "threadpool") {
        return BackendKind::ThreadPool;
    }
    if (value == "sync") {
        return BackendKind::Sync;
    }
    throw std::invalid_argument(
        "--backend must be auto, uring, threadpool, or sync"
    );
}

std::string_view backend_name(BackendKind kind) {
    switch (kind) {
    case BackendKind::Auto:
        return "auto";
    case BackendKind::Uring:
        return "uring";
    case BackendKind::ThreadPool:
        return "threadpool";
    case BackendKind::Sync:
        return "sync";
    }
    return "unknown";
}

Options parse_options(int argc, char** argv) {
    if (argc < 3) {
        throw std::invalid_argument("INPUT and OUTPUT paths are required");
    }

    Options options;
    options.input_path = argv[1];
    options.output_path = argv[2];

    for (int index = 3; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument.starts_with("--backend=")) {
            options.backend_kind =
                parse_backend(argument.substr(std::string_view{"--backend="}.size()));
        } else if (argument.starts_with("--block-size=")) {
            options.pipeline.block_size = parse_size(
                argument.substr(std::string_view{"--block-size="}.size()),
                "--block-size"
            );
        } else if (argument.starts_with("--buffers=")) {
            options.pipeline.max_inflight_buffers = parse_size(
                argument.substr(std::string_view{"--buffers="}.size()),
                "--buffers"
            );
        } else if (argument.starts_with("--queue-depth=")) {
            options.pipeline.queue_depth = parse_size(
                argument.substr(std::string_view{"--queue-depth="}.size()),
                "--queue-depth"
            );
        } else if (argument.starts_with("--alignment=")) {
            options.pipeline.buffer_alignment = parse_size(
                argument.substr(std::string_view{"--alignment="}.size()),
                "--alignment"
            );
        } else if (argument.starts_with("--thread-workers=")) {
            options.thread_workers = parse_size(
                argument.substr(std::string_view{"--thread-workers="}.size()),
                "--thread-workers"
            );
        } else if (argument.starts_with("--report-ms=")) {
            options.report_interval_ms = parse_size(
                argument.substr(std::string_view{"--report-ms="}.size()),
                "--report-ms",
                true
            );
        } else if (argument.starts_with("--metrics-json=")) {
            const std::string_view path = argument.substr(
                std::string_view{"--metrics-json="}.size()
            );
            if (path.empty()) {
                throw std::invalid_argument(
                    "--metrics-json requires a non-empty path"
                );
            }
            options.metrics_json_path = std::filesystem::path{path};
        } else if (argument == "--disable-uring") {
            options.disable_uring = true;
        } else if (argument == "--disable-threadpool") {
            options.disable_thread_pool = true;
        } else {
            throw std::invalid_argument(
                "unknown option: " + std::string(argument)
            );
        }
    }

    options.pipeline.validate();
    if (options.backend_kind != BackendKind::Auto &&
        (options.disable_uring || options.disable_thread_pool)) {
        throw std::invalid_argument(
            "backend disable switches are only valid with --backend=auto"
        );
    }
    if (options.pipeline.max_inflight_buffers >
        std::numeric_limits<unsigned>::max()) {
        throw std::invalid_argument(
            "--buffers exceeds the io_uring queue-depth range"
        );
    }
    if (options.report_interval_ms > static_cast<std::size_t>(
            std::chrono::milliseconds::max().count()
        )) {
        throw std::invalid_argument("--report-ms is too large");
    }
    return options;
}

std::filesystem::path normalized_path(
    const std::filesystem::path& path
) {
    std::error_code error;
    std::filesystem::path normalized =
        std::filesystem::weakly_canonical(path, error);
    if (error) {
        throw std::system_error(error, "normalize metrics JSON path");
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
        throw std::system_error(error, "compare metrics JSON paths");
    }
}

void validate_metrics_json_path(const Options& options) {
    if (!options.metrics_json_path.has_value()) {
        return;
    }
    const std::filesystem::path& path = *options.metrics_json_path;
    const std::filesystem::path filename = path.filename();
    if (filename.empty() || filename == "." || filename == "..") {
        throw std::invalid_argument("--metrics-json must name a file");
    }

    std::filesystem::path parent = path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::error_code error;
    const bool parent_is_directory =
        std::filesystem::is_directory(parent, error);
    if (error) {
        throw std::system_error(error, "inspect metrics JSON directory");
    }
    if (!parent_is_directory) {
        throw std::invalid_argument(
            "--metrics-json parent directory must already exist"
        );
    }

    error.clear();
    const bool target_is_directory =
        std::filesystem::is_directory(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
    }
    if (error) {
        throw std::system_error(error, "inspect metrics JSON target");
    }
    if (target_is_directory) {
        throw std::invalid_argument("--metrics-json must not be a directory");
    }

    reject_same_file(
        options.input_path,
        path,
        "metrics JSON path must differ from input"
    );
    reject_same_file(
        options.output_path,
        path,
        "metrics JSON path must differ from pipeline output"
    );
}

std::uint64_t checked_file_size(const std::filesystem::path& path) {
    const std::uintmax_t size = std::filesystem::file_size(path);
    if (size > std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("input file size exceeds uint64_t");
    }
    return static_cast<std::uint64_t>(size);
}

void verify_incremented_output(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::size_t block_size
) {
    auto input = asyncdataloader::util::open_read_only(input_path.c_str());
    if (input.error_number != 0) {
        throw std::system_error(
            input.error_number,
            std::generic_category(),
            "open input for verification"
        );
    }
    auto output = asyncdataloader::util::open_read_only(output_path.c_str());
    if (output.error_number != 0) {
        throw std::system_error(
            output.error_number,
            std::generic_category(),
            "open output for verification"
        );
    }

    std::vector<std::byte> input_block(block_size);
    std::vector<std::byte> output_block(block_size);
    std::uint64_t offset = 0;

    while (true) {
        const auto input_read = asyncdataloader::util::read_at(
            input.fd.get(),
            input_block.data(),
            input_block.size(),
            offset
        );
        if (input_read.error_number != 0) {
            throw std::system_error(
                input_read.error_number,
                std::generic_category(),
                "read input during verification"
            );
        }

        if (input_read.bytes_read == 0) {
            std::byte trailing{};
            const auto trailing_read = asyncdataloader::util::read_at(
                output.fd.get(),
                &trailing,
                1,
                offset
            );
            if (trailing_read.error_number != 0) {
                throw std::system_error(
                    trailing_read.error_number,
                    std::generic_category(),
                    "check output EOF during verification"
                );
            }
            if (trailing_read.bytes_read != 0) {
                throw std::runtime_error(
                    "output verification found trailing bytes"
                );
            }
            return;
        }

        const auto output_read = asyncdataloader::util::read_at(
            output.fd.get(),
            output_block.data(),
            input_read.bytes_read,
            offset
        );
        if (output_read.error_number != 0) {
            throw std::system_error(
                output_read.error_number,
                std::generic_category(),
                "read output during verification"
            );
        }
        if (output_read.bytes_read != input_read.bytes_read) {
            throw std::runtime_error(
                "output verification found a size mismatch"
            );
        }

        for (std::size_t index = 0; index < input_read.bytes_read; ++index) {
            const unsigned int expected =
                std::to_integer<unsigned int>(input_block[index]) + 1U;
            if (output_block[index] !=
                std::byte{static_cast<unsigned char>(expected)}) {
                throw std::runtime_error(
                    "output verification found transformed-byte mismatch"
                );
            }
        }

        if (input_read.bytes_read >
            std::numeric_limits<std::uint64_t>::max() - offset) {
            throw std::overflow_error("verification file offset overflow");
        }
        offset += static_cast<std::uint64_t>(input_read.bytes_read);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        print_usage(std::cout, argv[0]);
        return 0;
    }

    try {
        const Options options = parse_options(argc, argv);
        validate_metrics_json_path(options);
        const std::uint64_t input_bytes =
            checked_file_size(options.input_path);

        BackendConfig backend_config;
        backend_config.kind = options.backend_kind;
        backend_config.uring_queue_depth = static_cast<unsigned>(
            options.pipeline.max_inflight_buffers
        );
        backend_config.thread_pool_worker_count = options.thread_workers;
        backend_config.max_inflight =
            options.pipeline.max_inflight_buffers;
        backend_config.auto_try_uring = !options.disable_uring;
        backend_config.auto_try_thread_pool = !options.disable_thread_pool;

        std::unique_ptr<asyncdataloader::backend::IOBackend> backend =
            BackendFactory::create(backend_config);

        MetricsRegistry metrics;
        Pipeline processing(metrics);
        processing.add_stage(std::make_unique<ByteIncrementStage>());
        PipelineExecutor executor(
            options.pipeline,
            *backend,
            processing,
            metrics
        );

        PipelineRunReport report;
        report.input_path = options.input_path;
        report.output_path = options.output_path;
        report.requested_backend = backend_name(options.backend_kind);
        report.selected_backend = backend->name();
        report.stage_name = "byte_increment";
        report.pipeline_config = options.pipeline;
        report.input_bytes = input_bytes;
        asyncdataloader::reporting::write_terminal_header(
            std::cout,
            report
        );

        const Clock::time_point start = Clock::now();
        LiveTerminalReporter reporter(
            metrics,
            input_bytes,
            options.pipeline.max_inflight_buffers,
            options.pipeline.queue_depth,
            std::chrono::milliseconds(options.report_interval_ms),
            start,
            std::cout,
            ::isatty(STDOUT_FILENO) == 1
        );
        const auto result = executor.run_file(
            options.input_path,
            options.output_path
        );
        const Clock::time_point completed = Clock::now();
        reporter.stop();
        report.blocks_written = result.blocks_written;
        report.bytes_written = result.bytes_written;
        report.elapsed_seconds =
            std::chrono::duration<double>(completed - start).count();
        report.output_committed = true;

        verify_incremented_output(
            options.input_path,
            options.output_path,
            options.pipeline.block_size
        );
        report.verification_passed = true;

        const MetricsRegistry::Snapshot snapshot = metrics.snapshot();
        if (options.metrics_json_path.has_value()) {
            asyncdataloader::reporting::write_metrics_json_atomic(
                *options.metrics_json_path,
                snapshot,
                report
            );
        }

        asyncdataloader::reporting::write_terminal_summary(
            std::cout,
            snapshot,
            report
        );
        if (options.metrics_json_path.has_value()) {
            std::cout << "Metrics JSON     : "
                      << options.metrics_json_path->string() << "\n\n";
        }
        std::cout << "Machine-readable metrics\n";
        asyncdataloader::reporting::write_key_value_configuration(
            std::cout,
            report
        );
        asyncdataloader::reporting::write_key_value_result(
            std::cout,
            snapshot,
            report
        );
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "argument error: " << error.what() << '\n';
        print_usage(std::cerr, argc > 0 ? argv[0] : "preprocess_pipeline_demo");
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "preprocess pipeline failed: " << error.what() << '\n';
        return 1;
    }
}
