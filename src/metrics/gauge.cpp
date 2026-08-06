#include "metrics/gauge.h"

#include <atomic>
#include <limits>

namespace asyncdataloader::metrics {

void Gauge::set(Value new_value) noexcept {
    current_.store(new_value, std::memory_order_relaxed);
    update_high_watermark(new_value);
}

void Gauge::increment() noexcept {
    const Value previous = current_.fetch_add(1, std::memory_order_relaxed);
    // Avoid ordinary signed overflow when deriving the post-increment value.
    if (previous == std::numeric_limits<Value>::max()) {
        update_high_watermark(previous);
        return;
    }
    update_high_watermark(previous + 1);
}

void Gauge::decrement() noexcept {
    current_.fetch_sub(1, std::memory_order_relaxed);
}

Gauge::Value Gauge::value() const noexcept {
    return current_.load(std::memory_order_relaxed);
}

Gauge::Value Gauge::high_watermark() const noexcept {
    return high_watermark_.load(std::memory_order_relaxed);
}

void Gauge::update_high_watermark(Value candidate) noexcept {
    Value observed = high_watermark_.load(std::memory_order_relaxed);
    while (candidate > observed &&
           !high_watermark_.compare_exchange_weak(
               observed,
               candidate,
               std::memory_order_relaxed,
               std::memory_order_relaxed
           )) {
    }
}

}  // namespace asyncdataloader::metrics
