#pragma once

#include "metrics/counter.h"
#include "metrics/gauge.h"
#include "metrics/histogram.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace asyncdataloader::metrics {

// Owns a bounded set of uniquely named metrics. Complete registration before
// worker threads start; metric objects may then be updated concurrently through
// the stable references returned by add_*().
class MetricsRegistry {
public:
    static constexpr std::size_t kMaxMetricCount = 64;
    static constexpr std::size_t kMaxMetricNameLength = 128;

    struct CounterSnapshot {
        std::string name;
        std::uint64_t value{0};
    };

    struct GaugeSnapshot {
        std::string name;
        Gauge::Value value{0};
        Gauge::Value high_watermark{0};
    };

    struct HistogramSnapshot {
        std::string name;
        std::size_t upper_bound_count{0};
        std::array<Histogram::Value, Histogram::kMaxFiniteBuckets>
            upper_bounds{};
        Histogram::Snapshot data{};
    };

    struct Snapshot {
        std::vector<CounterSnapshot> counters;
        std::vector<GaugeSnapshot> gauges;
        std::vector<HistogramSnapshot> histograms;
    };

    MetricsRegistry() = default;

    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;
    MetricsRegistry(MetricsRegistry&&) = delete;
    MetricsRegistry& operator=(MetricsRegistry&&) = delete;

    Counter& add_counter(std::string_view name);
    Gauge& add_gauge(std::string_view name);
    Histogram& add_histogram(
        std::string_view name,
        std::span<const Histogram::Value> upper_bounds
    );

    [[nodiscard]] Counter* find_counter(std::string_view name) noexcept;
    [[nodiscard]] const Counter* find_counter(
        std::string_view name
    ) const noexcept;
    [[nodiscard]] Gauge* find_gauge(std::string_view name) noexcept;
    [[nodiscard]] const Gauge* find_gauge(
        std::string_view name
    ) const noexcept;
    [[nodiscard]] Histogram* find_histogram(std::string_view name) noexcept;
    [[nodiscard]] const Histogram* find_histogram(
        std::string_view name
    ) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    // Copies a bounded, non-transactional view for reporting. Registration
    // must already be complete; concurrent metric updates may span the read.
    [[nodiscard]] Snapshot snapshot() const;

private:
    struct CounterEntry {
        std::string name;
        std::unique_ptr<Counter> metric;
    };

    struct GaugeEntry {
        std::string name;
        std::unique_ptr<Gauge> metric;
    };

    struct HistogramEntry {
        std::string name;
        std::unique_ptr<Histogram> metric;
    };

    void validate_new_name(std::string_view name) const;
    [[nodiscard]] bool contains_name(std::string_view name) const noexcept;

    std::vector<CounterEntry> counters_;
    std::vector<GaugeEntry> gauges_;
    std::vector<HistogramEntry> histograms_;
};

}  // namespace asyncdataloader::metrics
