#pragma once

#include "config/pipeline_config.h"
#include "metrics/metrics_registry.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace asyncdataloader::reporting {

// Owns the small, fixed metadata needed to present one completed pipeline run.
// It contains no block payload and does not participate in buffer ownership.
struct PipelineRunReport {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::string requested_backend;
    std::string selected_backend;
    std::string stage_name;
    config::PipelineConfig pipeline_config;
    std::uint64_t input_bytes{0};
    std::uint64_t blocks_written{0};
    std::uint64_t bytes_written{0};
    double elapsed_seconds{0.0};
    bool output_committed{false};
    bool verification_passed{false};
};

struct PipelineProgress {
    std::uint64_t completed_bytes{0};
    std::uint64_t input_bytes{0};
    double elapsed_seconds{0.0};
    std::int64_t inflight_buffers{0};
    std::int64_t read_process_queue_depth{0};
    std::int64_t process_write_queue_depth{0};
    std::size_t max_inflight_buffers{0};
    std::size_t queue_capacity{0};
};

[[nodiscard]] double throughput_mib_s(
    const PipelineRunReport& report
) noexcept;
[[nodiscard]] std::string render_progress_line(
    const PipelineProgress& progress
);

// Periodically observes stable metric references. It does not synchronize or
// control pipeline work. interactive=true refreshes one TTY line in place;
// false emits newline-delimited records suitable for redirected logs.
class LiveTerminalReporter {
public:
    using Clock = std::chrono::steady_clock;

    LiveTerminalReporter(
        const metrics::MetricsRegistry& metrics,
        std::uint64_t input_bytes,
        std::size_t max_inflight_buffers,
        std::size_t queue_capacity,
        std::chrono::milliseconds interval,
        Clock::time_point start,
        std::ostream& output,
        bool interactive
    );
    ~LiveTerminalReporter() noexcept;

    LiveTerminalReporter(const LiveTerminalReporter&) = delete;
    LiveTerminalReporter& operator=(const LiveTerminalReporter&) = delete;

    void stop() noexcept;

private:
    void run(std::stop_token stop_token) noexcept;
    void print_once();

    const metrics::Counter& written_bytes_;
    const metrics::Gauge& inflight_;
    const metrics::Gauge& read_process_depth_;
    const metrics::Gauge& process_write_depth_;
    std::uint64_t input_bytes_;
    std::size_t max_inflight_buffers_;
    std::size_t queue_capacity_;
    std::chrono::milliseconds interval_;
    Clock::time_point start_;
    std::ostream& output_;
    bool interactive_{false};
    bool printed_{false};
    std::mutex wait_mutex_;
    std::condition_variable_any wakeup_;
    std::jthread worker_;
};

// Human-facing output. These functions deliberately accept ostream so tests
// can use ostringstream without redirecting process-wide stdout.
void write_terminal_header(
    std::ostream& output,
    const PipelineRunReport& report
);
void write_terminal_summary(
    std::ostream& output,
    const metrics::MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
);

// Stable machine-facing output retained for Stage 11 sweep/profile parsers.
void write_key_value_configuration(
    std::ostream& output,
    const PipelineRunReport& report
);
void write_key_value_result(
    std::ostream& output,
    const metrics::MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
);

// JSON is rendered from the same final Snapshot as terminal output. The file
// writer publishes through a same-directory temporary file, fsync, and rename.
[[nodiscard]] std::string render_metrics_json(
    const metrics::MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
);
void write_metrics_json_atomic(
    const std::filesystem::path& path,
    const metrics::MetricsRegistry::Snapshot& snapshot,
    const PipelineRunReport& report
);

}  // namespace asyncdataloader::reporting
