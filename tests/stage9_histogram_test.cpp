#include "metrics/histogram.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage9 Histogram test failed: " << message << '\n';
    return 1;
}

bool rejects_bounds(
    std::span<const asyncdataloader::metrics::Histogram::Value> bounds
) {
    try {
        const asyncdataloader::metrics::Histogram histogram(bounds);
    } catch (const std::invalid_argument&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace

int main() {
    using asyncdataloader::metrics::Histogram;

    static_assert(!std::is_copy_constructible_v<Histogram>);
    static_assert(!std::is_copy_assignable_v<Histogram>);
    static_assert(!std::is_move_constructible_v<Histogram>);
    static_assert(!std::is_move_assignable_v<Histogram>);

    const std::array<Histogram::Value, 0> empty_bounds{};
    if (!rejects_bounds(empty_bounds)) {
        return fail("empty bounds were accepted");
    }

    const std::array<Histogram::Value, 2> duplicate_bounds{10, 10};
    if (!rejects_bounds(duplicate_bounds)) {
        return fail("duplicate bounds were accepted");
    }

    const std::array<Histogram::Value, 3> decreasing_bounds{10, 30, 20};
    if (!rejects_bounds(decreasing_bounds)) {
        return fail("non-increasing bounds were accepted");
    }

    std::array<
        Histogram::Value,
        Histogram::kMaxFiniteBuckets + 1
    > too_many_bounds{};
    for (std::size_t index = 0; index < too_many_bounds.size(); ++index) {
        too_many_bounds[index] = static_cast<Histogram::Value>(index + 1);
    }
    if (!rejects_bounds(too_many_bounds)) {
        return fail("too many bounds were accepted");
    }

    std::array<Histogram::Value, 3> source_bounds{10, 20, 50};
    Histogram histogram(source_bounds);
    source_bounds.fill(1);

    const auto owned_bounds = histogram.upper_bounds();
    if (owned_bounds.size() != 3 || owned_bounds[0] != 10 ||
        owned_bounds[1] != 20 || owned_bounds[2] != 50) {
        return fail("Histogram did not own a copy of its bounds");
    }

    constexpr std::array<Histogram::Value, 7> samples{
        0,
        10,
        11,
        20,
        21,
        50,
        51,
    };
    for (const Histogram::Value sample : samples) {
        histogram.observe(sample);
    }

    const Histogram::Snapshot snapshot = histogram.snapshot();
    if (snapshot.sample_count != 7 || snapshot.sample_sum != 163 ||
        snapshot.bucket_count != 4) {
        return fail("snapshot totals or active bucket count are incorrect");
    }
    const std::array<Histogram::Count, 4> expected_counts{2, 2, 2, 1};
    for (std::size_t index = 0; index < expected_counts.size(); ++index) {
        if (snapshot.bucket_counts[index] != expected_counts[index]) {
            return fail("a boundary sample was assigned to the wrong bucket");
        }
    }

    constexpr std::array<Histogram::Value, 3> concurrent_bounds{10, 20, 50};
    constexpr std::array<Histogram::Value, 4> worker_samples{5, 15, 30, 100};
    constexpr std::size_t operations_per_worker = 25'000;
    Histogram concurrent_histogram(concurrent_bounds);

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_samples.size());
        for (std::size_t worker = 0; worker < worker_samples.size(); ++worker) {
            const Histogram::Value sample = worker_samples[worker];
            workers.emplace_back([&concurrent_histogram, sample] {
                for (std::size_t operation = 0;
                     operation < operations_per_worker;
                     ++operation) {
                    concurrent_histogram.observe(sample);
                }
            });
        }
    }

    const Histogram::Snapshot concurrent_snapshot =
        concurrent_histogram.snapshot();
    constexpr auto expected_per_bucket =
        static_cast<Histogram::Count>(operations_per_worker);
    constexpr auto expected_total = static_cast<Histogram::Count>(
        worker_samples.size() * operations_per_worker
    );
    constexpr auto expected_sum = static_cast<Histogram::Value>(
        (5U + 15U + 30U + 100U) * operations_per_worker
    );

    if (concurrent_snapshot.sample_count != expected_total ||
        concurrent_snapshot.sample_sum != expected_sum ||
        concurrent_snapshot.bucket_count != 4) {
        return fail("concurrent snapshot totals are incorrect");
    }
    for (std::size_t index = 0; index < worker_samples.size(); ++index) {
        if (concurrent_snapshot.bucket_counts[index] != expected_per_bucket) {
            return fail("concurrent observations were lost");
        }
    }

    return 0;
}
