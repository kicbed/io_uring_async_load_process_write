#include "metrics/metrics_registry.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage9 MetricsRegistry test failed: " << message << '\n';
    return 1;
}

template <typename Exception, typename Function>
bool throws_as(Function&& function) {
    try {
        std::forward<Function>(function)();
    } catch (const Exception&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

}  // namespace

int main() {
    using asyncdataloader::metrics::Histogram;
    using asyncdataloader::metrics::MetricsRegistry;

    static_assert(!std::is_copy_constructible_v<MetricsRegistry>);
    static_assert(!std::is_copy_assignable_v<MetricsRegistry>);
    static_assert(!std::is_move_constructible_v<MetricsRegistry>);
    static_assert(!std::is_move_assignable_v<MetricsRegistry>);

    MetricsRegistry registry;
    if (registry.size() != 0) {
        return fail("a new registry was not empty");
    }

    std::string counter_name = "processed_bytes";
    auto& processed_bytes = registry.add_counter(counter_name);
    counter_name.assign("changed_by_caller");
    processed_bytes.increment(4096);
    if (registry.find_counter("processed_bytes") != &processed_bytes ||
        processed_bytes.value() != 4096) {
        return fail("counter ownership, name copy, or lookup is incorrect");
    }

    auto& queue_depth = registry.add_gauge("queue_depth");
    queue_depth.set(3);
    if (registry.find_gauge("queue_depth") != &queue_depth ||
        queue_depth.value() != 3 || queue_depth.high_watermark() != 3) {
        return fail("gauge registration or lookup is incorrect");
    }

    constexpr std::array<Histogram::Value, 2> read_bounds{10, 50};
    constexpr std::array<Histogram::Value, 2> process_bounds{100, 1'000};
    auto& read_latency =
        registry.add_histogram("read_latency_us", read_bounds);
    auto& process_latency =
        registry.add_histogram("process_latency_us", process_bounds);
    read_latency.observe(60);
    process_latency.observe(60);

    const auto read_snapshot = read_latency.snapshot();
    const auto process_snapshot = process_latency.snapshot();
    if (read_snapshot.bucket_counts[2] != 1 ||
        process_snapshot.bucket_counts[0] != 1) {
        return fail("independent histograms used the wrong bucket layout");
    }
    if (registry.find_histogram("read_latency_us") != &read_latency ||
        registry.find_histogram("process_latency_us") != &process_latency ||
        registry.size() != 4) {
        return fail("histogram lookup or total registry size is incorrect");
    }

    const MetricsRegistry& const_registry = registry;
    if (const_registry.find_counter("processed_bytes") != &processed_bytes ||
        const_registry.find_gauge("queue_depth") != &queue_depth ||
        const_registry.find_histogram("read_latency_us") != &read_latency) {
        return fail("const lookup did not return the registered metrics");
    }
    if (registry.find_counter("missing") != nullptr ||
        registry.find_gauge("processed_bytes") != nullptr ||
        registry.find_histogram("queue_depth") != nullptr) {
        return fail("missing or wrong-type lookup did not return nullptr");
    }

    if (!throws_as<std::invalid_argument>([&registry] {
            registry.add_gauge("processed_bytes");
        })) {
        return fail("a duplicate name across metric types was accepted");
    }
    if (!throws_as<std::invalid_argument>([&registry] {
            registry.add_counter("");
        })) {
        return fail("an empty metric name was accepted");
    }
    const std::string overlong_name(
        MetricsRegistry::kMaxMetricNameLength + 1,
        'x'
    );
    if (!throws_as<std::length_error>([&registry, &overlong_name] {
            registry.add_counter(overlong_name);
        })) {
        return fail("an overlong metric name was accepted");
    }

    MetricsRegistry exception_registry;
    constexpr std::array<Histogram::Value, 0> empty_bounds{};
    if (!throws_as<std::invalid_argument>([&exception_registry, &empty_bounds] {
            exception_registry.add_histogram("latency", empty_bounds);
        }) ||
        exception_registry.size() != 0) {
        return fail("failed histogram construction changed the registry");
    }
    exception_registry.add_counter("latency");

    MetricsRegistry capacity_registry;
    auto& first_counter = capacity_registry.add_counter("counter_0");
    for (std::size_t index = 1;
         index < MetricsRegistry::kMaxMetricCount;
         ++index) {
        capacity_registry.add_counter("counter_" + std::to_string(index));
    }
    first_counter.increment();
    if (capacity_registry.size() != MetricsRegistry::kMaxMetricCount ||
        capacity_registry.find_counter("counter_0") != &first_counter ||
        first_counter.value() != 1) {
        return fail("capacity growth invalidated a metric reference");
    }
    if (!throws_as<std::length_error>([&capacity_registry] {
            capacity_registry.add_gauge("one_too_many");
        }) ||
        capacity_registry.size() != MetricsRegistry::kMaxMetricCount) {
        return fail("the fixed registry capacity was not enforced");
    }

    return 0;
}
