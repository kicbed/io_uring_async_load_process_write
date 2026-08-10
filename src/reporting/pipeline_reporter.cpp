#include "reporting/pipeline_reporter.h"

#include "pipeline/pipeline_executor.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fcntl.h>
#include <iomanip>
#include <ios>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace asyncdataloader::reporting {
namespace {

using metrics::MetricsRegistry;
using pipeline::PipelineMetricNames;

const metrics::Counter& require_counter(
    const MetricsRegistry& metrics,
    std::string_view name
) {
    const auto* const metric = metrics.find_counter(name);
    if (metric == nullptr) {
        throw std::logic_error("required pipeline Counter is missing");
    }
    return *metric;
}

const metrics::Gauge& require_live_gauge(
    const MetricsRegistry& metrics,
    std::string_view name
) {
    const auto* const metric = metrics.find_gauge(name);
    if (metric == nullptr) {
        throw std::logic_error("required pipeline Gauge is missing");
    }
    return *metric;
}

class StreamStateGuard {
public:
    explicit StreamStateGuard(std::ostream& output) noexcept
        : output_(output),
          flags_(output.flags()),
          precision_(output.precision()),
          fill_(output.fill()) {}

    ~StreamStateGuard() {
        output_.flags(flags_);
        output_.precision(precision_);
        output_.fill(fill_);
    }

    StreamStateGuard(const StreamStateGuard&) = delete;
    StreamStateGuard& operator=(const StreamStateGuard&) = delete;

private:
    std::ostream& output_;
    std::ios::fmtflags flags_;
    std::streamsize precision_;
    char fill_;
};

class ScopedFd {
public:
    ScopedFd() noexcept = default;
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}

    ~ScopedFd() noexcept {
        if (fd_ >= 0) {
            static_cast<void>(::close(fd_));
        }
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    ScopedFd(ScopedFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}

    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                static_cast<void>(::close(fd_));
            }
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

private:
    int fd_{-1};
};

void fsync_checked(int fd, const char* operation) {
    int result = ::fsync(fd);
    while (result < 0 && errno == EINTR) {
        result = ::fsync(fd);
    }
    if (result < 0) {
        throw std::system_error(errno, std::generic_category(), operation);
    }
}

void write_all_checked(int fd, std::string_view text) {
    std::size_t written = 0;
    while (written < text.size()) {
        const std::size_t remaining = text.size() - written;
        const std::size_t request_size = std::min(
            remaining,
            static_cast<std::size_t>(
                std::numeric_limits<ssize_t>::max()
            )
        );
        ssize_t result = ::write(fd, text.data() + written, request_size);
        while (result < 0 && errno == EINTR) {
            result = ::write(fd, text.data() + written, request_size);
        }
        if (result < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "write metrics JSON temporary file"
            );
        }
        if (result == 0) {
            throw std::system_error(
                EIO,
                std::generic_category(),
                "write metrics JSON temporary file"
            );
        }
        written += static_cast<std::size_t>(result);
    }
}

void validate_json_path(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("metrics JSON path must not be empty");
    }
    if (path.native().find('\0') != std::string::npos) {
        throw std::invalid_argument(
            "metrics JSON path must not contain a null byte"
        );
    }
    const std::filesystem::path filename = path.filename();
    if (filename.empty() || filename == "." || filename == "..") {
        throw std::invalid_argument("metrics JSON path must name a file");
    }
}

class AtomicJsonFile {
public:
    explicit AtomicJsonFile(std::filesystem::path final_path)
        : final_path_(std::move(final_path)) {
        validate_json_path(final_path_);

        std::filesystem::path parent = final_path_.parent_path();
        if (parent.empty()) {
            parent = ".";
        }
        const int parent_fd = ::open(
            parent.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC
        );
        if (parent_fd < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "open metrics JSON directory"
            );
        }
        parent_directory_fd_ = ScopedFd{parent_fd};

        const std::filesystem::path template_path = parent /
            ("." + final_path_.filename().string() + ".tmp.XXXXXX");
        std::string mutable_template = template_path.string();
        mutable_template.push_back('\0');
        const int temporary_fd = ::mkstemp(mutable_template.data());
        if (temporary_fd < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "create metrics JSON temporary file"
            );
        }

        ScopedFd created_fd{temporary_fd};
        try {
            temporary_path_ = std::filesystem::path{mutable_template.data()};
        } catch (...) {
            static_cast<void>(::unlink(mutable_template.data()));
            throw;
        }
        fd_ = std::move(created_fd);
    }

    ~AtomicJsonFile() noexcept {
        if (!committed_ && !temporary_path_.empty()) {
            static_cast<void>(::unlink(temporary_path_.c_str()));
        }
    }

    AtomicJsonFile(const AtomicJsonFile&) = delete;
    AtomicJsonFile& operator=(const AtomicJsonFile&) = delete;

    void publish(std::string_view contents) {
        write_all_checked(fd_.get(), contents);
        fsync_checked(fd_.get(), "fsync metrics JSON temporary file");

        if (::rename(temporary_path_.c_str(), final_path_.c_str()) != 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "rename metrics JSON temporary file"
            );
        }
        committed_ = true;
        fsync_checked(
            parent_directory_fd_.get(),
            "fsync metrics JSON directory"
        );
    }

private:
    std::filesystem::path final_path_;
    std::filesystem::path temporary_path_;
    ScopedFd fd_;
    ScopedFd parent_directory_fd_;
    bool committed_{false};
};

std::string human_bytes(std::uint64_t bytes) {
    constexpr std::array<std::string_view, 5> units{
        "B", "KiB", "MiB", "GiB", "TiB"
    };

    double value = static_cast<double>(bytes);
    std::size_t unit_index = 0;
    while (value >= 1024.0 && unit_index + 1 < units.size()) {
        value /= 1024.0;
        ++unit_index;
    }

    std::ostringstream output;
    output << std::fixed << std::setprecision(unit_index == 0 ? 0 : 2)
           << value << ' ' << units[unit_index];
    return output.str();
}

const MetricsRegistry::GaugeSnapshot& require_gauge(
    const MetricsRegistry::Snapshot& snapshot,
    std::string_view name
) {
    for (const auto& gauge : snapshot.gauges) {
        if (gauge.name == name) {
            return gauge;
        }
    }
    throw std::logic_error("required pipeline Gauge snapshot is missing");
}

const MetricsRegistry::HistogramSnapshot& require_histogram(
    const MetricsRegistry::Snapshot& snapshot,
    std::string_view name
) {
    for (const auto& histogram : snapshot.histograms) {
        if (histogram.name == name) {
            return histogram;
        }
    }
    throw std::logic_error(
        "required pipeline Histogram snapshot is missing"
    );
}

double average_us(
    const MetricsRegistry::HistogramSnapshot& histogram
) noexcept {
    if (histogram.data.sample_count == 0) {
        return 0.0;
    }
    return static_cast<double>(histogram.data.sample_sum) /
           static_cast<double>(histogram.data.sample_count) / 1000.0;
}

std::string_view yes_no(bool value) noexcept {
    return value ? "yes" : "no";
}

void write_json_string(std::ostream& output, std::string_view value) {
    constexpr char hex_digits[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
        case '"':
            output << "\\\"";
            break;
        case '\\':
            output << "\\\\";
            break;
        case '\b':
            output << "\\b";
            break;
        case '\f':
            output << "\\f";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                output << "\\u00" << hex_digits[byte >> 4U]
                       << hex_digits[byte & 0x0FU];
            } else {
                output << static_cast<char>(byte);
            }
            break;
        }
    }
    output << '"';
}

void write_json_named_string(
    std::ostream& output,
    std::string_view indentation,
    std::string_view name,
    std::string_view value,
    bool comma = true
) {
    output << indentation;
    write_json_string(output, name);
    output << ": ";
    write_json_string(output, value);
    output << (comma ? ",\n" : "\n");
}

void require_finite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(
            std::string(name) + " must be a finite number"
        );
    }
}

}  // namespace

double throughput_mib_s(const PipelineRunReport& report) noexcept {
    if (report.elapsed_seconds <= 0.0) {
        return 0.0;
    }
    return static_cast<double>(report.bytes_written) /
           (1024.0 * 1024.0 * report.elapsed_seconds);
}

std::string render_progress_line(const PipelineProgress& progress) {
    constexpr std::size_t bar_width = 24;
    const double percent = progress.input_bytes == 0
        ? 100.0
        : std::clamp(
              100.0 * static_cast<double>(progress.completed_bytes) /
                  static_cast<double>(progress.input_bytes),
              0.0,
              100.0
          );
    const std::size_t filled = static_cast<std::size_t>(
        percent * static_cast<double>(bar_width) / 100.0
    );
    const double current_throughput = progress.elapsed_seconds > 0.0
        ? static_cast<double>(progress.completed_bytes) /
              (1024.0 * 1024.0 * progress.elapsed_seconds)
        : 0.0;

    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < bar_width; ++index) {
        output << (index < filled ? '#' : '-');
    }
    output << "] " << std::fixed << std::setprecision(1)
           << std::setw(5) << percent << "% | "
           << human_bytes(progress.completed_bytes) << " / "
           << human_bytes(progress.input_bytes) << " | "
           << std::setprecision(2) << current_throughput << " MiB/s"
           << " | q " << progress.read_process_queue_depth << '/'
           << progress.queue_capacity << ','
           << progress.process_write_queue_depth << '/'
           << progress.queue_capacity << " | buffers "
           << progress.inflight_buffers << '/'
           << progress.max_inflight_buffers;
    return output.str();
}

LiveTerminalReporter::LiveTerminalReporter(
    const MetricsRegistry& metrics,
    std::uint64_t input_bytes,
    std::size_t max_inflight_buffers,
    std::size_t queue_capacity,
    std::chrono::milliseconds interval,
    Clock::time_point start,
    std::ostream& output,
    bool interactive
)
    : written_bytes_(require_counter(
          metrics,
          PipelineMetricNames::written_bytes
      )),
      inflight_(require_live_gauge(
          metrics,
          PipelineMetricNames::inflight_buffers
      )),
      read_process_depth_(require_live_gauge(
          metrics,
          PipelineMetricNames::read_process_queue_depth
      )),
      process_write_depth_(require_live_gauge(
          metrics,
          PipelineMetricNames::process_write_queue_depth
      )),
      input_bytes_(input_bytes),
      max_inflight_buffers_(max_inflight_buffers),
      queue_capacity_(queue_capacity),
      interval_(interval),
      start_(start),
      output_(output),
      interactive_(interactive) {
    if (interval_.count() > 0) {
        worker_ = std::jthread([this](std::stop_token stop_token) {
            run(stop_token);
        });
    }
}

LiveTerminalReporter::~LiveTerminalReporter() noexcept {
    stop();
}

void LiveTerminalReporter::stop() noexcept {
    if (!worker_.joinable()) {
        return;
    }
    worker_.request_stop();
    wakeup_.notify_all();
    worker_.join();
    if (interactive_ && printed_) {
        try {
            output_ << '\n' << std::flush;
        } catch (...) {
            // Reporting must never turn cleanup into process termination.
        }
    }
}

void LiveTerminalReporter::run(std::stop_token stop_token) noexcept {
    try {
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
    } catch (...) {
        // The observer is best-effort; pipeline work must keep running.
    }
}

void LiveTerminalReporter::print_once() {
    const PipelineProgress progress{
        written_bytes_.value(),
        input_bytes_,
        std::chrono::duration<double>(Clock::now() - start_).count(),
        inflight_.value(),
        read_process_depth_.value(),
        process_write_depth_.value(),
        max_inflight_buffers_,
        queue_capacity_,
    };
    const std::string line = render_progress_line(progress);
    if (interactive_) {
        output_ << "\r\033[2K" << line << std::flush;
    } else {
        output_ << "live " << line << '\n' << std::flush;
    }
    printed_ = true;
}

void write_terminal_header(
    std::ostream& output,
    const PipelineRunReport& report
) {
    output << "AsyncDataLoader\n"
           << "  Input          : " << report.input_path.string() << " ("
           << human_bytes(report.input_bytes) << ")\n"
           << "  Output         : " << report.output_path.string() << '\n'
           << "  Backend        : " << report.requested_backend << " -> "
           << report.selected_backend << '\n'
           << "  CPU stage      : " << report.stage_name << '\n'
           << "  Block / buffers: "
           << human_bytes(report.pipeline_config.block_size) << " / "
           << report.pipeline_config.max_inflight_buffers << '\n'
           << "  Queue capacity : " << report.pipeline_config.queue_depth
           << " per handoff\n\n";
}

void write_terminal_summary(
    std::ostream& output,
    const MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
) {
    const auto& inflight = require_gauge(
        snapshot,
        PipelineMetricNames::inflight_buffers
    );
    const auto& read_process = require_gauge(
        snapshot,
        PipelineMetricNames::read_process_queue_depth
    );
    const auto& process_write = require_gauge(
        snapshot,
        PipelineMetricNames::process_write_queue_depth
    );
    const auto& read_latency = require_histogram(
        snapshot,
        PipelineMetricNames::read_latency_ns
    );
    const auto& process_latency = require_histogram(
        snapshot,
        PipelineMetricNames::process_latency_ns
    );
    const auto& write_latency = require_histogram(
        snapshot,
        PipelineMetricNames::write_latency_ns
    );

    const StreamStateGuard guard(output);
    output << "Pipeline completed\n"
           << "  Processed      : " << human_bytes(report.bytes_written)
           << " in " << report.blocks_written << " blocks\n"
           << "  Elapsed        : " << std::fixed << std::setprecision(3)
           << report.elapsed_seconds * 1000.0 << " ms\n"
           << "  Throughput     : " << std::setprecision(2)
           << throughput_mib_s(report) << " MiB/s\n"
           << "  Buffer leases  : current " << inflight.value << ", peak "
           << inflight.high_watermark << " / "
           << report.pipeline_config.max_inflight_buffers << '\n'
           << "  Queue peaks    : read->process "
           << read_process.high_watermark << ", process->write "
           << process_write.high_watermark << " / "
           << report.pipeline_config.queue_depth << '\n'
           << "  Average latency: read " << average_us(read_latency)
           << " us, process " << average_us(process_latency)
           << " us, write " << average_us(write_latency) << " us\n"
           << "  Output commit  : " << yes_no(report.output_committed)
           << "\n  Verification   : "
           << yes_no(report.verification_passed) << "\n\n";
}

void write_key_value_configuration(
    std::ostream& output,
    const PipelineRunReport& report
) {
    output << "requested_backend=" << report.requested_backend << '\n'
           << "selected_backend=" << report.selected_backend << '\n'
           << "stage=" << report.stage_name << '\n'
           << "input_bytes=" << report.input_bytes << '\n'
           << "buffer_pool_bytes="
           << report.pipeline_config.buffer_pool_bytes() << '\n'
           << "max_inflight_buffers="
           << report.pipeline_config.max_inflight_buffers << '\n'
           << "queue_depth=" << report.pipeline_config.queue_depth << '\n';
}

void write_key_value_result(
    std::ostream& output,
    const MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
) {
    const StreamStateGuard guard(output);
    output << "status=complete\n"
           << "blocks_written=" << report.blocks_written << '\n'
           << "bytes_written=" << report.bytes_written << '\n'
           << "output_committed="
           << (report.output_committed ? "true" : "false") << '\n'
           << "verification="
           << (report.verification_passed ? "passed" : "failed") << '\n';

    for (const auto& counter : snapshot.counters) {
        output << counter.name << '=' << counter.value << '\n';
    }
    for (const auto& gauge : snapshot.gauges) {
        output << gauge.name << ".current=" << gauge.value << '\n'
               << gauge.name << ".peak=" << gauge.high_watermark << '\n';
    }
    for (const auto& histogram : snapshot.histograms) {
        output << histogram.name << ".samples="
               << histogram.data.sample_count << '\n'
               << histogram.name << ".average_us=" << std::fixed
               << std::setprecision(3) << average_us(histogram) << '\n';
    }

    output << "elapsed_ms=" << std::fixed << std::setprecision(3)
           << report.elapsed_seconds * 1000.0 << '\n'
           << "throughput_mib_s=" << throughput_mib_s(report) << '\n';
}

std::string render_metrics_json(
    const MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
) {
    require_finite(report.elapsed_seconds, "elapsed_seconds");
    const double throughput = throughput_mib_s(report);
    require_finite(throughput, "throughput_mib_s");

    std::ostringstream output;
    output << "{\n  \"schema_version\": 1,\n"
           << "  \"status\": \"complete\",\n"
           << "  \"run\": {\n";
    write_json_named_string(output, "    ", "input_path", report.input_path.string());
    write_json_named_string(output, "    ", "output_path", report.output_path.string());
    write_json_named_string(output, "    ", "requested_backend", report.requested_backend);
    write_json_named_string(output, "    ", "selected_backend", report.selected_backend);
    write_json_named_string(output, "    ", "stage", report.stage_name);
    output << "    \"input_bytes\": " << report.input_bytes << "\n"
           << "  },\n  \"pipeline_config\": {\n"
           << "    \"block_size\": "
           << report.pipeline_config.block_size << ",\n"
           << "    \"max_inflight_buffers\": "
           << report.pipeline_config.max_inflight_buffers << ",\n"
           << "    \"queue_depth\": "
           << report.pipeline_config.queue_depth << ",\n"
           << "    \"buffer_alignment\": "
           << report.pipeline_config.buffer_alignment << ",\n"
           << "    \"buffer_pool_bytes\": "
           << report.pipeline_config.buffer_pool_bytes() << "\n"
           << "  },\n  \"result\": {\n"
           << "    \"blocks_written\": " << report.blocks_written
           << ",\n    \"bytes_written\": " << report.bytes_written
           << ",\n    \"elapsed_ms\": " << std::fixed
           << std::setprecision(3) << report.elapsed_seconds * 1000.0
           << ",\n    \"throughput_mib_s\": " << throughput
           << ",\n    \"output_committed\": "
           << (report.output_committed ? "true" : "false")
           << ",\n    \"verification\": "
           << (report.verification_passed ? "\"passed\"" : "\"failed\"")
           << "\n  },\n  \"metrics\": {\n    \"counters\": [";

    for (std::size_t index = 0; index < snapshot.counters.size(); ++index) {
        const auto& counter = snapshot.counters[index];
        output << (index == 0 ? "\n" : ",\n") << "      {\"name\": ";
        write_json_string(output, counter.name);
        output << ", \"value\": " << counter.value << '}';
    }
    if (!snapshot.counters.empty()) {
        output << '\n';
    }
    output << "    ],\n    \"gauges\": [";

    for (std::size_t index = 0; index < snapshot.gauges.size(); ++index) {
        const auto& gauge = snapshot.gauges[index];
        output << (index == 0 ? "\n" : ",\n") << "      {\"name\": ";
        write_json_string(output, gauge.name);
        output << ", \"current\": " << gauge.value
               << ", \"high_watermark\": " << gauge.high_watermark << '}';
    }
    if (!snapshot.gauges.empty()) {
        output << '\n';
    }
    output << "    ],\n    \"histograms\": [";

    for (std::size_t index = 0; index < snapshot.histograms.size(); ++index) {
        const auto& histogram = snapshot.histograms[index];
        output << (index == 0 ? "\n" : ",\n")
               << "      {\"name\": ";
        write_json_string(output, histogram.name);
        output << ", \"sample_count\": " << histogram.data.sample_count
               << ", \"sample_sum_ns\": " << histogram.data.sample_sum
               << ", \"average_us\": " << average_us(histogram)
               << ", \"upper_bounds_ns\": [";
        for (std::size_t bound = 0;
             bound < histogram.upper_bound_count;
             ++bound) {
            output << (bound == 0 ? "" : ", ")
                   << histogram.upper_bounds[bound];
        }
        output << "], \"bucket_counts\": [";
        for (std::size_t bucket = 0;
             bucket < histogram.data.bucket_count;
             ++bucket) {
            output << (bucket == 0 ? "" : ", ")
                   << histogram.data.bucket_counts[bucket];
        }
        output << "]}";
    }
    if (!snapshot.histograms.empty()) {
        output << '\n';
    }
    output << "    ]\n  }\n}\n";
    return output.str();
}

void write_metrics_json_atomic(
    const std::filesystem::path& path,
    const MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
) {
    const std::string contents = render_metrics_json(snapshot, report);
    AtomicJsonFile output(path);
    output.publish(contents);
}

}  // namespace asyncdataloader::reporting
