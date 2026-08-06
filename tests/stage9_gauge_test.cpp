#include "metrics/gauge.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage9 Gauge test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::metrics::Gauge;

    static_assert(std::is_signed_v<Gauge::Value>);
    static_assert(!std::is_copy_constructible_v<Gauge>);
    static_assert(!std::is_copy_assignable_v<Gauge>);
    static_assert(!std::is_move_constructible_v<Gauge>);
    static_assert(!std::is_move_assignable_v<Gauge>);

    Gauge gauge;
    if (gauge.value() != 0 || gauge.high_watermark() != 0) {
        return fail("a new gauge did not start at zero");
    }

    gauge.increment();
    gauge.increment();
    gauge.decrement();
    if (gauge.value() != 1 || gauge.high_watermark() != 2) {
        return fail("increment/decrement did not preserve the peak");
    }

    gauge.set(7);
    if (gauge.value() != 7 || gauge.high_watermark() != 7) {
        return fail("set did not raise the current value and peak");
    }

    gauge.set(3);
    if (gauge.value() != 3 || gauge.high_watermark() != 7) {
        return fail("lowering the current value also lowered the peak");
    }

    Gauge concurrent_gauge;
    constexpr std::size_t worker_count = 4;
    constexpr std::size_t operations_per_worker = 25'000;
    constexpr auto expected_peak = static_cast<Gauge::Value>(
        worker_count * operations_per_worker
    );

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&concurrent_gauge] {
                for (std::size_t operation = 0;
                     operation < operations_per_worker;
                     ++operation) {
                    concurrent_gauge.increment();
                }
            });
        }
    }

    if (concurrent_gauge.value() != expected_peak ||
        concurrent_gauge.high_watermark() != expected_peak) {
        return fail("concurrent increments lost the current value or peak");
    }

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&concurrent_gauge] {
                for (std::size_t operation = 0;
                     operation < operations_per_worker;
                     ++operation) {
                    concurrent_gauge.decrement();
                }
            });
        }
    }

    if (concurrent_gauge.value() != 0 ||
        concurrent_gauge.high_watermark() != expected_peak) {
        return fail("concurrent decrements changed the historical peak");
    }

    return 0;
}
