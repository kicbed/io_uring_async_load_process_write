#include "metrics/histogram.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace asyncdataloader::metrics {

Histogram::Histogram(std::span<const Value> upper_bounds) {
    if (upper_bounds.empty()) {
        throw std::invalid_argument(
            "Histogram requires at least one finite bucket"
        );
    }
    if (upper_bounds.size() > kMaxFiniteBuckets) {
        throw std::invalid_argument(
            "Histogram finite bucket count exceeds the fixed limit"
        );
    }
    for (std::size_t index = 1; index < upper_bounds.size(); ++index) {
        if (upper_bounds[index - 1] >= upper_bounds[index]) {
            throw std::invalid_argument(
                "Histogram upper bounds must be strictly increasing"
            );
        }
    }

    finite_bucket_count_ = upper_bounds.size();
    std::copy(
        upper_bounds.begin(),
        upper_bounds.end(),
        upper_bounds_.begin()
    );
    for (auto& bucket_count : bucket_counts_) {
        bucket_count.store(0, std::memory_order_relaxed);
    }
}

void Histogram::observe(Value sample) noexcept {
    const std::span<const Value> bounds = upper_bounds();
    const auto bucket = std::lower_bound(bounds.begin(), bounds.end(), sample);
    const auto bucket_index = static_cast<std::size_t>(
        bucket - bounds.begin()
    );

    bucket_counts_[bucket_index].fetch_add(1, std::memory_order_relaxed);
    sample_sum_.fetch_add(sample, std::memory_order_relaxed);
    sample_count_.fetch_add(1, std::memory_order_relaxed);
}

std::span<const Histogram::Value> Histogram::upper_bounds() const noexcept {
    return {upper_bounds_.data(), finite_bucket_count_};
}

Histogram::Snapshot Histogram::snapshot() const noexcept {
    Snapshot result;
    result.sample_count = sample_count_.load(std::memory_order_relaxed);
    result.sample_sum = sample_sum_.load(std::memory_order_relaxed);
    result.bucket_count = finite_bucket_count_ + 1;
    for (std::size_t index = 0; index < result.bucket_count; ++index) {
        result.bucket_counts[index] =
            bucket_counts_[index].load(std::memory_order_relaxed);
    }
    return result;
}

}  // namespace asyncdataloader::metrics
