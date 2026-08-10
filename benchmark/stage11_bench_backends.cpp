#include "backend/backend_factory.h"
#include "backend/io_backend.h"
#include "benchmark/benchmark_cli.h"
#include "benchmark/benchmark_report.h"
#include "benchmark/end_to_end_baseline.h"
#include "config/pipeline_config.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using asyncdataloader::backend::BackendConfig;
using asyncdataloader::backend::BackendFactory;
using asyncdataloader::backend::BackendKind;
using asyncdataloader::benchmark::TimedEndToEndResult;
using asyncdataloader::config::PipelineConfig;

constexpr std::size_t kMaxBenchmarkCapacity = 65'536;
constexpr std::size_t kBenchmarkAlignment = 4096;
constexpr std::size_t kMinimumThreeStageOverlapBuffers = 3;

struct BenchmarkOptions {
    std::filesystem::path input_path;
    std::filesystem::path output_directory;
    PipelineConfig pipeline_config;
    std::size_t thread_workers{2};
    std::size_t iterations{1};
    bool disable_auto_uring{false};
    bool disable_auto_thread_pool{false};
};

struct OutputPaths {
    std::filesystem::path serial_oracle;
    std::filesystem::path sync;
    std::filesystem::path thread_pool;
    std::filesystem::path io_uring;
    std::filesystem::path automatic;
};

struct BackendMethod {
    std::string requested_backend;
    std::string selected_backend;
    std::string report_name;
    std::filesystem::path output_path;
    BackendConfig backend_config;
    std::vector<double> samples_ms;
};

void print_usage() {
    std::cerr
        << "usage: stage11_bench_backends <input> <output_directory> "
           "<block_size> <max_inflight_buffers> <queue_depth> "
           "<thread_workers> [iterations] [--disable-uring] "
           "[--disable-threadpool]\n"
        << "note: disable switches affect Auto selection only\n";
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
    if (argc < 7 || argc > 10) {
        print_usage();
        return false;
    }

    options.input_path = argv[1];
    options.output_directory = argv[2];
    if (options.input_path.empty() || options.output_directory.empty()) {
        std::cerr << "input and output directory must not be empty\n";
        return false;
    }

    if (!parse_bounded_size(
            argv[3],
            asyncdataloader::benchmark::kMaxBenchmarkBlockSize,
            "block_size",
            options.pipeline_config.block_size
        ) ||
        !parse_bounded_size(
            argv[4],
            kMaxBenchmarkCapacity,
            "max_inflight_buffers",
            options.pipeline_config.max_inflight_buffers
        ) ||
        !parse_bounded_size(
            argv[5],
            kMaxBenchmarkCapacity,
            "queue_depth",
            options.pipeline_config.queue_depth
        ) ||
        !parse_bounded_size(
            argv[6],
            kMaxBenchmarkCapacity,
            "thread_workers",
            options.thread_workers
        )) {
        return false;
    }
    options.pipeline_config.buffer_alignment = kBenchmarkAlignment;

    int argument_index = 7;
    if (argument_index < argc &&
        !std::string_view{argv[argument_index]}.starts_with("--")) {
        if (!parse_bounded_size(
                argv[argument_index],
                asyncdataloader::benchmark::kMaxBenchmarkIterations,
                "iterations",
                options.iterations
            )) {
            return false;
        }
        ++argument_index;
    }

    for (; argument_index < argc; ++argument_index) {
        const std::string_view argument{argv[argument_index]};
        if (argument == "--disable-uring") {
            options.disable_auto_uring = true;
        } else if (argument == "--disable-threadpool") {
            options.disable_auto_thread_pool = true;
        } else {
            std::cerr << "unknown option: " << argument << '\n';
            return false;
        }
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
    const std::filesystem::path& right
) {
    if (normalized_path(left) == normalized_path(right)) {
        throw std::invalid_argument(
            "benchmark input and output files must all differ"
        );
    }

    std::error_code error;
    const bool equivalent = std::filesystem::equivalent(left, right, error);
    if (!error && equivalent) {
        throw std::invalid_argument(
            "benchmark input and output files must all differ"
        );
    }
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::system_error(error, "compare benchmark paths");
    }
}

void require_existing_output_directory(
    const std::filesystem::path& path
) {
    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(path, error);
    if (error) {
        throw std::system_error(error, "inspect benchmark output directory");
    }
    if (!is_directory) {
        throw std::invalid_argument(
            "benchmark output_directory must already exist"
        );
    }
}

OutputPaths make_output_paths(
    const std::filesystem::path& output_directory
) {
    return OutputPaths{
        output_directory / "serial-oracle.bin",
        output_directory / "sync.bin",
        output_directory / "thread-pool.bin",
        output_directory / "io-uring.bin",
        output_directory / "auto.bin",
    };
}

void validate_distinct_paths(
    const std::filesystem::path& input_path,
    const OutputPaths& outputs
) {
    const std::vector<std::filesystem::path> paths{
        input_path,
        outputs.serial_oracle,
        outputs.sync,
        outputs.thread_pool,
        outputs.io_uring,
        outputs.automatic,
    };

    for (std::size_t left = 0; left < paths.size(); ++left) {
        for (std::size_t right = left + 1; right < paths.size(); ++right) {
            reject_same_file(paths[left], paths[right]);
        }
    }
}

BackendConfig make_backend_config(
    const BenchmarkOptions& options,
    BackendKind kind
) {
    BackendConfig config;
    config.kind = kind;
    config.uring_queue_depth = static_cast<unsigned>(
        options.pipeline_config.max_inflight_buffers
    );
    config.thread_pool_worker_count = options.thread_workers;
    config.max_inflight = options.pipeline_config.max_inflight_buffers;
    config.auto_try_uring = !options.disable_auto_uring;
    config.auto_try_thread_pool = !options.disable_auto_thread_pool;
    return config;
}

BackendMethod make_backend_method(
    std::string requested_backend,
    std::string report_prefix,
    std::filesystem::path output_path,
    BackendConfig backend_config,
    std::size_t iterations
) {
    // Construction is a preflight check and is intentionally outside every
    // timed sample. Each sample gets a fresh backend of the same kind.
    std::unique_ptr<asyncdataloader::backend::IOBackend> backend =
        BackendFactory::create(backend_config);
    const std::string selected_backend{backend->name()};

    BackendMethod method;
    method.requested_backend = std::move(requested_backend);
    method.selected_backend = selected_backend;
    method.report_name = std::move(report_prefix) + selected_backend +
                         "_byte_increment";
    method.output_path = std::move(output_path);
    method.backend_config = backend_config;
    method.samples_ms.reserve(iterations);
    return method;
}

std::optional<BackendMethod> try_make_explicit_method(
    std::string requested_backend,
    std::filesystem::path output_path,
    BackendConfig backend_config,
    std::size_t iterations
) {
    try {
        return make_backend_method(
            requested_backend,
            "pipeline_",
            std::move(output_path),
            backend_config,
            iterations
        );
    } catch (const std::system_error& error) {
        std::cerr << "skipped_backend=" << requested_backend
                  << ",error_code=" << error.code().value()
                  << ",error_category=" << error.code().category().name()
                  << '\n';
        return std::nullopt;
    }
}

void validate_against_oracle(
    const TimedEndToEndResult& oracle,
    const TimedEndToEndResult& candidate,
    std::uint64_t verified_bytes,
    std::string_view requested_backend
) {
    if (oracle.blocks_written != candidate.blocks_written ||
        oracle.bytes_written != candidate.bytes_written) {
        throw std::runtime_error(
            std::string{requested_backend} +
            " byte/block counts differ from the serial oracle"
        );
    }
    if (verified_bytes != oracle.bytes_written) {
        throw std::runtime_error(
            std::string{requested_backend} +
            " verified length differs from the serial oracle"
        );
    }
}

int run_benchmark(const BenchmarkOptions& options) {
    options.pipeline_config.validate();
    if (options.pipeline_config.max_inflight_buffers <
        kMinimumThreeStageOverlapBuffers) {
        throw std::invalid_argument(
            "max_inflight_buffers must be at least 3 for backend comparison"
        );
    }
    if (options.pipeline_config.max_inflight_buffers >
        std::numeric_limits<unsigned>::max()) {
        throw std::invalid_argument(
            "max_inflight_buffers exceeds the io_uring queue-depth range"
        );
    }

    require_existing_output_directory(options.output_directory);
    const OutputPaths outputs = make_output_paths(options.output_directory);
    validate_distinct_paths(options.input_path, outputs);

    std::vector<BackendMethod> methods;
    methods.reserve(4);

    methods.push_back(make_backend_method(
        "sync",
        "pipeline_",
        outputs.sync,
        make_backend_config(options, BackendKind::Sync),
        options.iterations
    ));

    if (auto thread_pool = try_make_explicit_method(
            "thread_pool",
            outputs.thread_pool,
            make_backend_config(options, BackendKind::ThreadPool),
            options.iterations
        ); thread_pool.has_value()) {
        methods.push_back(std::move(*thread_pool));
    }

    if (auto io_uring = try_make_explicit_method(
            "io_uring",
            outputs.io_uring,
            make_backend_config(options, BackendKind::Uring),
            options.iterations
        ); io_uring.has_value()) {
        methods.push_back(std::move(*io_uring));
    }

    BackendMethod automatic = make_backend_method(
        "auto",
        "pipeline_auto_selected_",
        outputs.automatic,
        make_backend_config(options, BackendKind::Auto),
        options.iterations
    );
    const std::string auto_selected_backend = automatic.selected_backend;
    methods.push_back(std::move(automatic));

    const TimedEndToEndResult oracle =
        asyncdataloader::benchmark::run_timed_serial_byte_increment(
            options.input_path,
            outputs.serial_oracle,
            options.pipeline_config
        );

    for (std::size_t iteration = 0; iteration < options.iterations;
         ++iteration) {
        for (std::size_t position = 0; position < methods.size(); ++position) {
            BackendMethod& method = methods[
                (iteration + position) % methods.size()
            ];
            std::unique_ptr<asyncdataloader::backend::IOBackend> backend =
                BackendFactory::create(method.backend_config);
            if (backend->name() != method.selected_backend) {
                throw std::runtime_error(
                    method.requested_backend +
                    " selected a different backend between iterations"
                );
            }

            const TimedEndToEndResult sample =
                asyncdataloader::benchmark::
                    run_timed_pipeline_byte_increment(
                        options.input_path,
                        method.output_path,
                        options.pipeline_config,
                        *backend
                    );
            const std::uint64_t verified_bytes =
                asyncdataloader::benchmark::verify_files_equal_bounded(
                    outputs.serial_oracle,
                    method.output_path,
                    options.pipeline_config
                );
            validate_against_oracle(
                oracle,
                sample,
                verified_bytes,
                method.requested_backend
            );
            method.samples_ms.push_back(sample.elapsed_ms);
        }
    }

    asyncdataloader::benchmark::write_benchmark_csv_header(std::cout);
    for (const BackendMethod& method : methods) {
        const auto report = asyncdataloader::benchmark::make_benchmark_report(
            method.report_name,
            oracle.bytes_written,
            oracle.bytes_written,
            method.samples_ms
        );
        asyncdataloader::benchmark::write_benchmark_csv_row(
            std::cout,
            report
        );
    }

    std::cerr
        << "comparison_design=same_pipeline_read_backend_matrix\n"
        << "oracle=serial_sync_byte_increment_not_timed_in_matrix\n"
        << "processing_stage=byte_increment\n"
        << "pipeline_execution=reader_processor_writer_jthreads\n"
        << "sync_read_model=blocking_pread_in_reader_thread\n"
        << "thread_pool_read_model=coroutine_suspend_worker_pread_resume\n"
        << "io_uring_read_model=coroutine_suspend_sqe_cqe_resume\n"
        << "read_inflight_model=one_read_task_at_a_time\n"
        << "writer_io=blocking_pwrite_for_all_methods\n"
        << "auto_is_selection_policy=true\n"
        << "auto_selected_backend=" << auto_selected_backend << '\n'
        << "thread_workers=" << options.thread_workers << '\n'
        << "max_inflight_buffers="
        << options.pipeline_config.max_inflight_buffers << '\n'
        << "queue_depth=" << options.pipeline_config.queue_depth << '\n'
        << "backend_construction=outside_timing_fresh_per_sample\n"
        << "timing_boundary=execution_plus_output_fsync\n"
        << "excluded_from_timing=open_close_oracle_and_output_verification\n"
        << "cache_policy=not_modified_by_harness\n"
        << "execution_order=rotating_available_backends\n"
        << "pipeline_metrics=enabled\n"
        << "output_verified=true\n"
        << "verified_bytes=" << oracle.bytes_written << '\n';
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
        std::cerr << "stage11 backend benchmark failed: "
                  << error.what() << '\n';
        return 2;
    }
}
