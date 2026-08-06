#include "pipeline/pipeline.h"

#include "metrics/histogram.h"
#include "metrics/metrics_registry.h"
#include "metrics/scoped_timer.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace asyncdataloader::pipeline {
namespace {

constexpr std::array<metrics::Histogram::Value, 7>
    kStageLatencyUpperBoundsNs{
        1'000,
        10'000,
        100'000,
        1'000'000,
        10'000'000,
        100'000'000,
        1'000'000'000,
    };

std::string make_stage_latency_metric_name(
    std::size_t stage_index,
    std::string_view stage_name
) {
    std::string metric_name{"stage."};
    metric_name += std::to_string(stage_index);
    metric_name += '.';
    metric_name.append(stage_name);
    metric_name += ".latency_ns";
    return metric_name;
}

}  // namespace

Pipeline::Pipeline(metrics::MetricsRegistry& metrics) noexcept
    : metrics_(&metrics) {}

void Pipeline::add_stage(std::unique_ptr<Stage> stage) {
    if (!stage) {
        throw std::invalid_argument("pipeline stage must not be null");
    }

    if (metrics_ == nullptr) {
        stages_.push_back(StageEntry{std::move(stage), nullptr});
        return;
    }

    const std::string metric_name = make_stage_latency_metric_name(
        stages_.size(),
        stage->name()
    );
    stages_.push_back(StageEntry{std::move(stage), nullptr});

    try {
        metrics::Histogram& latency = metrics_->add_histogram(
            metric_name,
            kStageLatencyUpperBoundsNs
        );
        stages_.back().latency = &latency;
    } catch (...) {
        stages_.pop_back();
        throw;
    }
}

void Pipeline::process(std::span<std::byte> block) {
    for (StageEntry& entry : stages_) {
        if (entry.latency == nullptr) {
            entry.stage->process(block);
            continue;
        }

        metrics::ScopedTimer timer(*entry.latency);
        entry.stage->process(block);
    }
}

std::size_t Pipeline::stage_count() const noexcept {
    return stages_.size();
}

}  // namespace asyncdataloader::pipeline
