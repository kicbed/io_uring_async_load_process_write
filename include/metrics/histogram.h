#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace asyncdataloader::metrics {

// A fixed-capacity histogram with exclusive bucket counts. Each finite bucket
// includes its upper bound; the final active bucket stores overflow samples.
class Histogram {
public:
    using Value = std::uint64_t;
    using Count = std::uint64_t;

    static constexpr std::size_t kMaxFiniteBuckets = 32;
    static constexpr std::size_t kMaxBucketCount =
        kMaxFiniteBuckets + 1;

    struct Snapshot {
        Count sample_count{0};
        Value sample_sum{0};
        std::size_t bucket_count{0};
        std::array<Count, kMaxBucketCount> bucket_counts{};
    };

    explicit Histogram(std::span<const Value> upper_bounds);

    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;
    Histogram(Histogram&&) = delete;
    Histogram& operator=(Histogram&&) = delete;

    void observe(Value sample) noexcept;
    [[nodiscard]] std::span<const Value> upper_bounds() const noexcept;
    // Exact after observers quiesce; concurrent reads are non-transactional
    // because totals and bucket counts are separate atomic values.
    [[nodiscard]] Snapshot snapshot() const noexcept;

private:
    std::array<Value, kMaxFiniteBuckets> upper_bounds_{};
    std::size_t finite_bucket_count_{0};
    std::array<std::atomic<Count>, kMaxBucketCount> bucket_counts_{};
    std::atomic<Count> sample_count_{0};
    std::atomic<Value> sample_sum_{0};
};

}  // namespace asyncdataloader::metrics
