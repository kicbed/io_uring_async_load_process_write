#include "metrics/metrics_registry.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace asyncdataloader::metrics {

Counter& MetricsRegistry::add_counter(std::string_view name) {
    validate_new_name(name);

    auto metric = std::make_unique<Counter>();
    Counter* const result = metric.get();
    counters_.push_back(CounterEntry{std::string(name), std::move(metric)});
    return *result;
}

Gauge& MetricsRegistry::add_gauge(std::string_view name) {
    validate_new_name(name);

    auto metric = std::make_unique<Gauge>();
    Gauge* const result = metric.get();
    gauges_.push_back(GaugeEntry{std::string(name), std::move(metric)});
    return *result;
}

Histogram& MetricsRegistry::add_histogram(
    std::string_view name,
    std::span<const Histogram::Value> upper_bounds
) {
    validate_new_name(name);

    auto metric = std::make_unique<Histogram>(upper_bounds);
    Histogram* const result = metric.get();
    histograms_.push_back(
        HistogramEntry{std::string(name), std::move(metric)}
    );
    return *result;
}

Counter* MetricsRegistry::find_counter(std::string_view name) noexcept {
    for (CounterEntry& entry : counters_) {
        if (entry.name == name) {
            return entry.metric.get();
        }
    }
    return nullptr;
}

const Counter* MetricsRegistry::find_counter(
    std::string_view name
) const noexcept {
    for (const CounterEntry& entry : counters_) {
        if (entry.name == name) {
            return entry.metric.get();
        }
    }
    return nullptr;
}

Gauge* MetricsRegistry::find_gauge(std::string_view name) noexcept {
    for (GaugeEntry& entry : gauges_) {
        if (entry.name == name) {
            return entry.metric.get();
        }
    }
    return nullptr;
}

const Gauge* MetricsRegistry::find_gauge(
    std::string_view name
) const noexcept {
    for (const GaugeEntry& entry : gauges_) {
        if (entry.name == name) {
            return entry.metric.get();
        }
    }
    return nullptr;
}

Histogram* MetricsRegistry::find_histogram(std::string_view name) noexcept {
    for (HistogramEntry& entry : histograms_) {
        if (entry.name == name) {
            return entry.metric.get();
        }
    }
    return nullptr;
}

const Histogram* MetricsRegistry::find_histogram(
    std::string_view name
) const noexcept {
    for (const HistogramEntry& entry : histograms_) {
        if (entry.name == name) {
            return entry.metric.get();
        }
    }
    return nullptr;
}

std::size_t MetricsRegistry::size() const noexcept {
    return counters_.size() + gauges_.size() + histograms_.size();
}

MetricsRegistry::Snapshot MetricsRegistry::snapshot() const {
    Snapshot result;
    result.counters.reserve(counters_.size());
    result.gauges.reserve(gauges_.size());
    result.histograms.reserve(histograms_.size());

    for (const CounterEntry& entry : counters_) {
        result.counters.push_back(
            CounterSnapshot{entry.name, entry.metric->value()}
        );
    }
    for (const GaugeEntry& entry : gauges_) {
        result.gauges.push_back(GaugeSnapshot{
            entry.name,
            entry.metric->value(),
            entry.metric->high_watermark(),
        });
    }
    for (const HistogramEntry& entry : histograms_) {
        HistogramSnapshot histogram_snapshot;
        histogram_snapshot.name = entry.name;
        const std::span<const Histogram::Value> upper_bounds =
            entry.metric->upper_bounds();
        histogram_snapshot.upper_bound_count = upper_bounds.size();
        std::copy(
            upper_bounds.begin(),
            upper_bounds.end(),
            histogram_snapshot.upper_bounds.begin()
        );
        histogram_snapshot.data = entry.metric->snapshot();
        result.histograms.push_back(std::move(histogram_snapshot));
    }

    return result;
}

void MetricsRegistry::validate_new_name(std::string_view name) const {
    if (name.empty()) {
        throw std::invalid_argument("metric name must not be empty");
    }
    if (name.size() > kMaxMetricNameLength) {
        throw std::length_error("metric name exceeds the fixed length limit");
    }
    if (contains_name(name)) {
        throw std::invalid_argument("metric name is already registered");
    }
    if (size() >= kMaxMetricCount) {
        throw std::length_error("metrics registry is full");
    }
}

bool MetricsRegistry::contains_name(std::string_view name) const noexcept {
    for (const CounterEntry& entry : counters_) {
        if (entry.name == name) {
            return true;
        }
    }
    for (const GaugeEntry& entry : gauges_) {
        if (entry.name == name) {
            return true;
        }
    }
    for (const HistogramEntry& entry : histograms_) {
        if (entry.name == name) {
            return true;
        }
    }
    return false;
}

}  // namespace asyncdataloader::metrics
