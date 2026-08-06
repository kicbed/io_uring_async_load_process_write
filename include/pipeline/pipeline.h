#pragma once

#include "pipeline/stage.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace asyncdataloader::metrics {
class Histogram;
class MetricsRegistry;
}  // namespace asyncdataloader::metrics

namespace asyncdataloader::pipeline {

class Pipeline {
public:
    Pipeline() = default;
    // The registry is borrowed and must outlive this Pipeline and all process
    // calls. When supplied, every registered Stage is timed automatically.
    explicit Pipeline(metrics::MetricsRegistry& metrics) noexcept;
    ~Pipeline() = default;

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) noexcept = default;
    Pipeline& operator=(Pipeline&&) noexcept = default;

    // Transfers unique ownership to the Pipeline. Null stages are rejected.
    // An instrumented Pipeline also registers one latency Histogram; if that
    // registration fails, the Stage is removed and the exception propagates.
    void add_stage(std::unique_ptr<Stage> stage);

    // Runs stages in registration order. If a stage throws, the exception is
    // propagated and later stages are not run. An instrumented Pipeline still
    // records the throwing Stage's elapsed time during stack unwinding.
    void process(std::span<std::byte> block);

    [[nodiscard]] std::size_t stage_count() const noexcept;

private:
    struct StageEntry {
        std::unique_ptr<Stage> stage;
        metrics::Histogram* latency{nullptr};
    };

    metrics::MetricsRegistry* metrics_{nullptr};
    std::vector<StageEntry> stages_;
};

}  // namespace asyncdataloader::pipeline
