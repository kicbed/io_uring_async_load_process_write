#pragma once

#include <atomic>
#include <cstdint>

namespace asyncdataloader::metrics {

// A monotonically increasing event total. Counter provides atomic numeric
// updates only; it is not a synchronization mechanism for pipeline data.
class Counter {
public:
    Counter() noexcept = default;

    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;
    Counter(Counter&&) = delete;
    Counter& operator=(Counter&&) = delete;

    void increment(std::uint64_t amount = 1) noexcept;
    [[nodiscard]] std::uint64_t value() const noexcept;

private:
    std::atomic<std::uint64_t> value_{0};
};

}  // namespace asyncdataloader::metrics
