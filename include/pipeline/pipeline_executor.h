#pragma once

#include "config/pipeline_config.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace asyncdataloader::backend {
class IOBackend;
}  // namespace asyncdataloader::backend

namespace asyncdataloader::metrics {
class Counter;
class Gauge;
class Histogram;
class MetricsRegistry;
}  // namespace asyncdataloader::metrics

namespace asyncdataloader::pipeline {

class Pipeline;

struct PipelineRunResult {
    std::uint64_t blocks_written{0};
    std::uint64_t bytes_written{0};
};

// Stable names let the executor, tests, and simple Stage 10 terminal reporter
// refer to the same metrics without duplicating string literals.
struct PipelineMetricNames {
    static constexpr std::string_view read_blocks{
        "pipeline.read.blocks"
    };
    static constexpr std::string_view read_bytes{
        "pipeline.read.bytes"
    };
    static constexpr std::string_view processed_blocks{
        "pipeline.process.blocks"
    };
    static constexpr std::string_view processed_bytes{
        "pipeline.process.bytes"
    };
    static constexpr std::string_view written_blocks{
        "pipeline.write.blocks"
    };
    static constexpr std::string_view written_bytes{
        "pipeline.write.bytes"
    };
    static constexpr std::string_view inflight_buffers{
        "pipeline.buffer_pool.inflight"
    };
    static constexpr std::string_view read_process_queue_depth{
        "pipeline.queue.read_process.depth"
    };
    static constexpr std::string_view process_write_queue_depth{
        "pipeline.queue.process_write.depth"
    };
    static constexpr std::string_view read_latency_ns{
        "pipeline.read.latency_ns"
    };
    static constexpr std::string_view process_latency_ns{
        "pipeline.process.latency_ns"
    };
    static constexpr std::string_view write_latency_ns{
        "pipeline.write.latency_ns"
    };
};

// Coordinates one reader, one processor, and one writer. The configuration is
// copied so its memory bounds cannot change during a run. The backend and
// processing Pipeline are borrowed and must outlive this executor.
class PipelineExecutor {
public:
    PipelineExecutor(
        config::PipelineConfig config,
        backend::IOBackend& read_backend,
        Pipeline& processing_pipeline
    );
    // Registers all runtime metrics before workers start. The registry is
    // borrowed and must outlive this executor and its single run.
    PipelineExecutor(
        config::PipelineConfig config,
        backend::IOBackend& read_backend,
        Pipeline& processing_pipeline,
        metrics::MetricsRegistry& metrics
    );
    ~PipelineExecutor() = default;

    PipelineExecutor(const PipelineExecutor&) = delete;
    PipelineExecutor& operator=(const PipelineExecutor&) = delete;
    PipelineExecutor(PipelineExecutor&&) = delete;
    PipelineExecutor& operator=(PipelineExecutor&&) = delete;

    // Runs synchronously until every worker has stopped. The caller owns both
    // descriptors and must keep them open. output_fd must refer to a separate,
    // already empty/truncated file; prefer run_file() when publishing a final
    // output path. An executor is single-use; construct a new executor for a
    // second file so each run has dedicated zero-based Gauge high watermarks.
    [[nodiscard]] PipelineRunResult run(int input_fd, int output_fd);

    // Opens input_path, streams into a temporary file beside final_output_path,
    // fsyncs the completed temporary file, atomically renames it over the
    // final path, and fsyncs the parent directory. Failures before rename
    // leave an existing final output unchanged; a directory-fsync failure is
    // reported after the new name may already be visible.
    [[nodiscard]] PipelineRunResult run_file(
        const std::filesystem::path& input_path,
        const std::filesystem::path& final_output_path
    );

private:
    struct RuntimeMetrics {
        metrics::Counter* read_blocks{nullptr};
        metrics::Counter* read_bytes{nullptr};
        metrics::Counter* processed_blocks{nullptr};
        metrics::Counter* processed_bytes{nullptr};
        metrics::Counter* written_blocks{nullptr};
        metrics::Counter* written_bytes{nullptr};
        metrics::Gauge* inflight_buffers{nullptr};
        metrics::Gauge* read_process_queue_depth{nullptr};
        metrics::Gauge* process_write_queue_depth{nullptr};
        metrics::Histogram* read_latency_ns{nullptr};
        metrics::Histogram* process_latency_ns{nullptr};
        metrics::Histogram* write_latency_ns{nullptr};
    };

    void register_runtime_metrics(metrics::MetricsRegistry& metrics);

    config::PipelineConfig config_;
    backend::IOBackend& read_backend_;
    Pipeline& processing_pipeline_;
    RuntimeMetrics metrics_;
    std::atomic<bool> started_{false};
};

}  // namespace asyncdataloader::pipeline
