#include "metrics/metrics_registry.h"
#include "pipeline/pipeline_executor.h"
#include "reporting/pipeline_reporter.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

using asyncdataloader::metrics::Histogram;
using asyncdataloader::metrics::MetricsRegistry;
using asyncdataloader::pipeline::PipelineMetricNames;
using asyncdataloader::reporting::PipelineRunReport;

int fail(const char* message) {
    std::cerr << "stage12 pipeline-reporter test failed: " << message << '\n';
    return 1;
}

void populate_metrics(MetricsRegistry& metrics) {
    metrics.add_counter(PipelineMetricNames::written_bytes).increment(2097152);

    auto& inflight = metrics.add_gauge(
        PipelineMetricNames::inflight_buffers
    );
    inflight.increment();
    inflight.increment();
    inflight.decrement();
    inflight.decrement();

    auto& read_process = metrics.add_gauge(
        PipelineMetricNames::read_process_queue_depth
    );
    read_process.increment();
    read_process.decrement();

    auto& process_write = metrics.add_gauge(
        PipelineMetricNames::process_write_queue_depth
    );
    process_write.increment();
    process_write.increment();
    process_write.decrement();
    process_write.decrement();

    constexpr std::array<Histogram::Value, 2> bounds{1000, 10000};
    metrics.add_histogram(
        PipelineMetricNames::read_latency_ns,
        bounds
    ).observe(2000);
    metrics.add_histogram(
        PipelineMetricNames::process_latency_ns,
        bounds
    ).observe(3000);
    metrics.add_histogram(
        PipelineMetricNames::write_latency_ns,
        bounds
    ).observe(4000);
}

PipelineRunReport make_report() {
    PipelineRunReport report;
    report.input_path = "/tmp/input.bin";
    report.output_path = "/tmp/output.bin";
    report.requested_backend = "auto";
    report.selected_backend = "io_uring";
    report.stage_name = "byte_increment";
    report.pipeline_config.block_size = 1048576;
    report.pipeline_config.max_inflight_buffers = 3;
    report.pipeline_config.queue_depth = 2;
    report.pipeline_config.buffer_alignment = 4096;
    report.input_bytes = 2097152;
    report.blocks_written = 2;
    report.bytes_written = 2097152;
    report.elapsed_seconds = 2.0;
    report.output_committed = true;
    report.verification_passed = true;
    return report;
}

int test_human_and_machine_output() {
    MetricsRegistry metrics;
    populate_metrics(metrics);
    const PipelineRunReport report = make_report();
    const auto snapshot = metrics.snapshot();

    std::ostringstream human;
    asyncdataloader::reporting::write_terminal_header(human, report);
    asyncdataloader::reporting::write_terminal_summary(
        human,
        snapshot,
        report
    );
    const std::string human_text = human.str();
    for (const char* expected : {
             "AsyncDataLoader",
             "auto -> io_uring",
             "Pipeline completed",
             "2.00 MiB in 2 blocks",
             "1.00 MiB/s",
             "current 0, peak 2 / 3",
             "read->process 1, process->write 2 / 2",
             "read 2.00 us, process 3.00 us, write 4.00 us",
             "Verification   : yes",
         }) {
        if (human_text.find(expected) == std::string::npos) {
            return fail("human output missed an expected field");
        }
    }

    std::ostringstream machine;
    asyncdataloader::reporting::write_key_value_configuration(
        machine,
        report
    );
    asyncdataloader::reporting::write_key_value_result(
        machine,
        snapshot,
        report
    );
    const std::string machine_text = machine.str();
    for (const char* expected : {
             "requested_backend=auto",
             "selected_backend=io_uring",
             "buffer_pool_bytes=3145728",
             "status=complete",
             "verification=passed",
             "pipeline.buffer_pool.inflight.peak=2",
             "pipeline.read.latency_ns.average_us=2.000",
             "throughput_mib_s=1.000",
         }) {
        if (machine_text.find(expected) == std::string::npos) {
            return fail("key-value output missed an expected field");
        }
    }
    return 0;
}

int test_missing_required_snapshot_is_rejected() {
    MetricsRegistry empty;
    bool rejected = false;
    try {
        std::ostringstream output;
        asyncdataloader::reporting::write_terminal_summary(
            output,
            empty.snapshot(),
            make_report()
        );
    } catch (const std::logic_error&) {
        rejected = true;
    }
    return rejected ? 0 : fail("missing runtime metrics were accepted");
}

int test_progress_line() {
    const asyncdataloader::reporting::PipelineProgress progress{
        1048576,
        2097152,
        0.5,
        2,
        1,
        2,
        3,
        2,
    };
    const std::string line =
        asyncdataloader::reporting::render_progress_line(progress);
    for (const char* expected : {
             "[############------------]",
             "50.0%",
             "1.00 MiB / 2.00 MiB",
             "2.00 MiB/s",
             "q 1/2,2/2",
             "buffers 2/3",
         }) {
        if (line.find(expected) == std::string::npos) {
            return fail("progress line missed an expected field");
        }
    }

    asyncdataloader::reporting::PipelineProgress empty;
    const std::string empty_line =
        asyncdataloader::reporting::render_progress_line(empty);
    if (empty_line.find("100.0%") == std::string::npos ||
        empty_line.find("0 B / 0 B") == std::string::npos) {
        return fail("empty-input progress should be complete");
    }
    return 0;
}

int test_live_reporter_stops_promptly() {
    MetricsRegistry metrics;
    populate_metrics(metrics);
    std::ostringstream output;
    const auto started = std::chrono::steady_clock::now();
    asyncdataloader::reporting::LiveTerminalReporter reporter(
        metrics,
        2097152,
        3,
        2,
        std::chrono::hours(1),
        started,
        output,
        false
    );
    reporter.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed > std::chrono::seconds(1)) {
        return fail("live reporter did not wake promptly during stop");
    }
    return 0;
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::string path = "/tmp/asyncdataloader_stage12_reporter_XXXXXX";
        path.push_back('\0');
        char* const created = ::mkdtemp(path.data());
        if (created == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        path_ = created;
    }

    ~TemporaryDirectory() noexcept {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

int test_json_render_and_atomic_publish() {
    MetricsRegistry metrics;
    populate_metrics(metrics);
    PipelineRunReport report = make_report();
    report.stage_name = "byte\"increment\\stage\n";
    const auto snapshot = metrics.snapshot();
    const std::string json =
        asyncdataloader::reporting::render_metrics_json(snapshot, report);

    for (const char* expected : {
             "\"schema_version\": 1",
             "\"status\": \"complete\"",
             "\"stage\": \"byte\\\"increment\\\\stage\\n\"",
             "\"buffer_pool_bytes\": 3145728",
             "\"throughput_mib_s\": 1.000",
             "\"output_committed\": true",
             "\"verification\": \"passed\"",
             "\"upper_bounds_ns\": [1000, 10000]",
             "\"bucket_counts\": [0, 1, 0]",
         }) {
        if (json.find(expected) == std::string::npos) {
            return fail("JSON output missed an expected field or escape");
        }
    }

    TemporaryDirectory directory;
    const std::filesystem::path json_path = directory.path() / "metrics.json";
    {
        std::ofstream old_file(json_path);
        old_file << "old";
    }
    asyncdataloader::reporting::write_metrics_json_atomic(
        json_path,
        snapshot,
        report
    );
    std::ifstream input(json_path);
    const std::string published{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    if (published != json) {
        return fail("atomically published JSON differs from rendered JSON");
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory.path())) {
        if (entry.path() != json_path) {
            return fail("metrics JSON publication left a temporary file");
        }
    }
    return 0;
}

int test_json_rejects_invalid_values_and_paths() {
    MetricsRegistry metrics;
    populate_metrics(metrics);
    PipelineRunReport report = make_report();
    report.elapsed_seconds = std::numeric_limits<double>::infinity();
    bool non_finite_rejected = false;
    try {
        static_cast<void>(asyncdataloader::reporting::render_metrics_json(
            metrics.snapshot(),
            report
        ));
    } catch (const std::invalid_argument&) {
        non_finite_rejected = true;
    }
    if (!non_finite_rejected) {
        return fail("non-finite JSON number was accepted");
    }

    bool empty_path_rejected = false;
    try {
        asyncdataloader::reporting::write_metrics_json_atomic(
            {},
            metrics.snapshot(),
            make_report()
        );
    } catch (const std::invalid_argument&) {
        empty_path_rejected = true;
    }
    return empty_path_rejected
        ? 0
        : fail("empty metrics JSON path was accepted");
}

}  // namespace

int main() {
    if (const int result = test_human_and_machine_output(); result != 0) {
        return result;
    }
    if (const int result = test_missing_required_snapshot_is_rejected();
        result != 0) {
        return result;
    }
    if (const int result = test_progress_line(); result != 0) {
        return result;
    }
    if (const int result = test_live_reporter_stops_promptly(); result != 0) {
        return result;
    }
    if (const int result = test_json_render_and_atomic_publish();
        result != 0) {
        return result;
    }
    return test_json_rejects_invalid_values_and_paths();
}
