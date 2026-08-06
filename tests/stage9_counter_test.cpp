#include "metrics/counter.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage9 Counter test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::metrics::Counter;

    static_assert(!std::is_copy_constructible_v<Counter>);
    static_assert(!std::is_copy_assignable_v<Counter>);
    static_assert(!std::is_move_constructible_v<Counter>);
    static_assert(!std::is_move_assignable_v<Counter>);

    Counter counter;
    if (counter.value() != 0) {
        return fail("a new counter did not start at zero");
    }

    counter.increment();
    counter.increment(41);
    if (counter.value() != 42) {
        return fail("single-thread increments produced the wrong value");
    }

    Counter concurrent_counter;
    constexpr std::size_t worker_count = 4;
    constexpr std::size_t increments_per_worker = 25'000;

    {
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&concurrent_counter] {
                for (std::size_t increment = 0;
                     increment < increments_per_worker;
                     ++increment) {
                    concurrent_counter.increment();
                }
            });
        }
    }

    constexpr std::uint64_t expected =
        worker_count * increments_per_worker;
    if (concurrent_counter.value() != expected) {
        return fail("concurrent increments were lost");
    }

    return 0;
}
