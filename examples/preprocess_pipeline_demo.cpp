#include "backend/backend_factory.h"
#include "backend/io_backend.h"
#include "config/pipeline_config.h"
#include "metrics/counter.h"
#include "metrics/gauge.h"
#include "metrics/metrics_registry.h"
#include "pipeline/builtin_stages.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_executor.h"
#include "util/file_io.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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
using asyncdataloader::pipeline::PipelineMetricNames;

using Clock = std::chrono::steady_clock;

struct Options {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    PipelineConfig pipeline;
    BackendKind backend_kind{BackendKind::Auto};
    std::size_t thread_workers{2};
    std::size_t report_interval_ms{250};
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
    return options;
}

const asyncdataloader::metrics::Counter& require_counter(
    const MetricsRegistry& metrics,
    std::string_view name
) {
    const auto* const metric = metrics.find_counter(name);
    if (metric == nullptr) {
        throw std::logic_error("required pipeline Counter is missing");
    }
    return *metric;
}

const asyncdataloader::metrics::Gauge& require_gauge(
    const MetricsRegistry& metrics,
    std::string_view name
) {
    const auto* const metric = metrics.find_gauge(name);
    if (metric == nullptr) {
        throw std::logic_error("required pipeline Gauge is missing");
    }
    return *metric;
}

class LiveReporter {
public:
    LiveReporter(
        const MetricsRegistry& metrics,
        std::uint64_t input_bytes,
        std::chrono::milliseconds interval,
        Clock::time_point start
    )
        : written_bytes_(require_counter(
              metrics,
              PipelineMetricNames::written_bytes
          )),
          inflight_(require_gauge(
              metrics,
              PipelineMetricNames::inflight_buffers
          )),
          read_process_depth_(require_gauge(
              metrics,
              PipelineMetricNames::read_process_queue_depth
          )),
          process_write_depth_(require_gauge(
              metrics,
              PipelineMetricNames::process_write_queue_depth
          )),
          input_bytes_(input_bytes),
          interval_(interval),
          start_(start) {
        if (interval_.count() > 0) {
            worker_ = std::jthread([this](std::stop_token stop_token) {
                run(stop_token);
            });
        }
    }

    ~LiveReporter() noexcept {
        stop();
    }

    LiveReporter(const LiveReporter&) = delete;
    LiveReporter& operator=(const LiveReporter&) = delete;

    void stop() noexcept {
        if (!worker_.joinable()) {
            return;
        }
        worker_.request_stop();
        wakeup_.notify_all();
        worker_.join();
    }

private:
    void run(std::stop_token stop_token) {
        std::unique_lock lock(wait_mutex_);
        while (!stop_token.stop_requested()) {
            static_cast<void>(wakeup_.wait_for(
                lock,
                stop_token,
                interval_,
                [] { return false; }
            ));
            if (stop_token.stop_requested()) {
                return;
            }

            lock.unlock();
            print_once();
            lock.lock();
        }
    }

    void print_once() const {
        const std::uint64_t completed = written_bytes_.value();
        const double elapsed_seconds =
            std::chrono::duration<double>(Clock::now() - start_).count();
        const double progress = input_bytes_ == 0
            ? 100.0
            : std::min(
                  100.0,
                  100.0 * static_cast<double>(completed) /
                      static_cast<double>(input_bytes_)
              );
        const double throughput_mib = elapsed_seconds > 0.0
            ? static_cast<double>(completed) /
                (1024.0 * 1024.0 * elapsed_seconds)
            : 0.0;

        std::cout << std::fixed << std::setprecision(2)
                  << "progress=" << progress << "%"
                  << " written_bytes=" << completed
                  << " throughput_mib_s=" << throughput_mib
                  << " read_process_q=" << read_process_depth_.value()
                  << " process_write_q=" << process_write_depth_.value()
                  << " inflight=" << inflight_.value() << '\n'
                  << std::flush;
    }

    const asyncdataloader::metrics::Counter& written_bytes_;
    const asyncdataloader::metrics::Gauge& inflight_;
    const asyncdataloader::metrics::Gauge& read_process_depth_;
    const asyncdataloader::metrics::Gauge& process_write_depth_;
    std::uint64_t input_bytes_;
    std::chrono::milliseconds interval_;
    Clock::time_point start_;
    std::mutex wait_mutex_;
    std::condition_variable_any wakeup_;
    std::jthread worker_;
};

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

void print_final_metrics(
    const MetricsRegistry& metrics,
    double elapsed_seconds
) {
    const auto snapshot = metrics.snapshot();
    for (const auto& counter : snapshot.counters) {
        std::cout << counter.name << '=' << counter.value << '\n';
    }
    for (const auto& gauge : snapshot.gauges) {
        std::cout << gauge.name << ".current=" << gauge.value << '\n'
                  << gauge.name << ".peak=" << gauge.high_watermark << '\n';
    }
    for (const auto& histogram : snapshot.histograms) {
        const double average_us = histogram.data.sample_count == 0
            ? 0.0
            : static_cast<double>(histogram.data.sample_sum) /
                static_cast<double>(histogram.data.sample_count) / 1000.0;
        std::cout << histogram.name << ".samples="
                  << histogram.data.sample_count << '\n'
                  << histogram.name << ".average_us="
                  << std::fixed << std::setprecision(3) << average_us << '\n';
    }

    const std::uint64_t written = require_counter(
        metrics,
        PipelineMetricNames::written_bytes
    ).value();
    const double throughput_mib = elapsed_seconds > 0.0
        ? static_cast<double>(written) /
            (1024.0 * 1024.0 * elapsed_seconds)
        : 0.0;
    std::cout << "elapsed_ms=" << std::fixed << std::setprecision(3)
              << elapsed_seconds * 1000.0 << '\n'
              << "throughput_mib_s=" << throughput_mib << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        print_usage(std::cout, argv[0]);
        return 0;
    }

    try {
        const Options options = parse_options(argc, argv);
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

        std::cout << "requested_backend="
                  << backend_name(options.backend_kind) << '\n'
                  << "selected_backend=" << backend->name() << '\n'
                  << "stage=byte_increment\n"
                  << "input_bytes=" << input_bytes << '\n'
                  << "buffer_pool_bytes="
                  << options.pipeline.buffer_pool_bytes() << '\n'
                  << "max_inflight_buffers="
                  << options.pipeline.max_inflight_buffers << '\n'
                  << "queue_depth=" << options.pipeline.queue_depth << '\n';

        const Clock::time_point start = Clock::now();
        LiveReporter reporter(
            metrics,
            input_bytes,
            std::chrono::milliseconds(options.report_interval_ms),
            start
        );
        const auto result = executor.run_file(
            options.input_path,
            options.output_path
        );
        const Clock::time_point completed = Clock::now();
        reporter.stop();

        verify_incremented_output(
            options.input_path,
            options.output_path,
            options.pipeline.block_size
        );

        const double elapsed_seconds =
            std::chrono::duration<double>(completed - start).count();
        std::cout << "status=complete\n"
                  << "blocks_written=" << result.blocks_written << '\n'
                  << "bytes_written=" << result.bytes_written << '\n'
                  << "output_committed=true\n"
                  << "verification=passed\n";
        print_final_metrics(metrics, elapsed_seconds);
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
