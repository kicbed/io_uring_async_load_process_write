#include "metrics/histogram.h"
#include "metrics/metrics_registry.h"
#include "pipeline/pipeline.h"
#include "pipeline/stage.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>

namespace {

class AddStage final : public asyncdataloader::pipeline::Stage {
public:
    explicit AddStage(unsigned int amount) noexcept : amount_(amount) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "add";
    }

    void process(std::span<std::byte> block) override {
        for (std::byte& value : block) {
            const unsigned int result =
                (std::to_integer<unsigned int>(value) + amount_) & 0xFFU;
            value = static_cast<std::byte>(result);
        }
    }

private:
    unsigned int amount_;
};

class ThrowingStage final : public asyncdataloader::pipeline::Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "throwing";
    }

    void process(std::span<std::byte>) override {
        throw std::runtime_error("expected stage timing failure");
    }
};

int fail(std::string_view message) {
    std::cerr << "stage9 Stage timing test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::metrics::Histogram;
    using asyncdataloader::metrics::MetricsRegistry;
    using asyncdataloader::pipeline::Pipeline;

    MetricsRegistry metrics;
    Pipeline pipeline(metrics);
    pipeline.add_stage(std::make_unique<AddStage>(1U));
    pipeline.add_stage(std::make_unique<AddStage>(2U));

    Histogram* const first_latency =
        metrics.find_histogram("stage.0.add.latency_ns");
    Histogram* const second_latency =
        metrics.find_histogram("stage.1.add.latency_ns");
    if (first_latency == nullptr || second_latency == nullptr ||
        first_latency == second_latency || pipeline.stage_count() != 2 ||
        metrics.size() != 2) {
        return fail("Stage registration did not create independent metrics");
    }

    std::array<std::byte, 1> block{static_cast<std::byte>(1)};
    pipeline.process(block);
    pipeline.process(block);

    if (std::to_integer<unsigned int>(block[0]) != 7 ||
        first_latency->snapshot().sample_count != 2 ||
        second_latency->snapshot().sample_count != 2) {
        return fail("automatic timing changed processing or missed a call");
    }

    MetricsRegistry failure_metrics;
    Pipeline failing_pipeline(failure_metrics);
    failing_pipeline.add_stage(std::make_unique<ThrowingStage>());
    failing_pipeline.add_stage(std::make_unique<AddStage>(1U));

    bool exception_propagated = false;
    try {
        failing_pipeline.process(block);
    } catch (const std::runtime_error& error) {
        exception_propagated =
            std::string_view{error.what()} == "expected stage timing failure";
    }

    const Histogram* const throwing_latency =
        failure_metrics.find_histogram("stage.0.throwing.latency_ns");
    const Histogram* const skipped_latency =
        failure_metrics.find_histogram("stage.1.add.latency_ns");
    if (!exception_propagated || throwing_latency == nullptr ||
        skipped_latency == nullptr ||
        throwing_latency->snapshot().sample_count != 1 ||
        skipped_latency->snapshot().sample_count != 0) {
        return fail("exception timing or later-Stage short-circuit is incorrect");
    }

    MetricsRegistry collision_metrics;
    constexpr std::array<Histogram::Value, 1> collision_bounds{1};
    collision_metrics.add_histogram(
        "stage.0.add.latency_ns",
        collision_bounds
    );
    Pipeline collision_pipeline(collision_metrics);

    bool collision_rejected = false;
    try {
        collision_pipeline.add_stage(std::make_unique<AddStage>(1U));
    } catch (const std::invalid_argument&) {
        collision_rejected = true;
    }
    if (!collision_rejected || collision_pipeline.stage_count() != 0 ||
        collision_metrics.size() != 1) {
        return fail("failed metric registration did not roll back the Stage");
    }

    return 0;
}
