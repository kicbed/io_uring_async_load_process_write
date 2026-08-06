#pragma once

#include <atomic>
#include <cstdint>

namespace asyncdataloader::metrics {

// A signed current value plus its lifetime maximum. Gauge provides atomic
// numeric observation only; it does not synchronize pipeline data.
class Gauge {
public:
    using Value = std::int64_t;

    Gauge() noexcept = default;

    Gauge(const Gauge&) = delete;
    Gauge& operator=(const Gauge&) = delete;
    Gauge(Gauge&&) = delete;
    Gauge& operator=(Gauge&&) = delete;

    void set(Value new_value) noexcept;
    void increment() noexcept;
    void decrement() noexcept;
    [[nodiscard]] Value value() const noexcept;
    [[nodiscard]] Value high_watermark() const noexcept;

private:
    void update_high_watermark(Value candidate) noexcept;

    std::atomic<Value> current_{0};
    std::atomic<Value> high_watermark_{0};
};

}  // namespace asyncdataloader::metrics
