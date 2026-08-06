#include "metrics/histogram.h"
#include "metrics/scoped_timer.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage9 ScopedTimer test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::metrics::Histogram;
    using asyncdataloader::metrics::ScopedTimer;

    static_assert(!std::is_copy_constructible_v<ScopedTimer>);
    static_assert(!std::is_copy_assignable_v<ScopedTimer>);
    static_assert(!std::is_move_constructible_v<ScopedTimer>);
    static_assert(!std::is_move_assignable_v<ScopedTimer>);
    static_assert(std::is_nothrow_destructible_v<ScopedTimer>);

    constexpr std::array<Histogram::Value, 1> bounds{1'000'000'000};
    Histogram histogram(bounds);

    {
        ScopedTimer timer(histogram);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto first_snapshot = histogram.snapshot();
    if (first_snapshot.sample_count != 1 || first_snapshot.sample_sum == 0) {
        return fail("normal scope exit did not record one positive duration");
    }

    bool exception_propagated = false;
    try {
        ScopedTimer timer(histogram);
        throw std::runtime_error("expected timer unwind");
    } catch (const std::runtime_error& error) {
        exception_propagated =
            std::string_view{error.what()} == "expected timer unwind";
    }

    const auto second_snapshot = histogram.snapshot();
    Histogram::Count bucket_total = 0;
    for (std::size_t index = 0;
         index < second_snapshot.bucket_count;
         ++index) {
        bucket_total += second_snapshot.bucket_counts[index];
    }
    if (!exception_propagated || second_snapshot.sample_count != 2 ||
        bucket_total != 2) {
        return fail("exceptional scope exit did not record exactly one sample");
    }

    return 0;
}
