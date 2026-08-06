#include "metrics/counter.h"

#include <atomic>
#include <cstdint>

namespace asyncdataloader::metrics {

void Counter::increment(std::uint64_t amount) noexcept {
    value_.fetch_add(amount, std::memory_order_relaxed);
}

std::uint64_t Counter::value() const noexcept {
    return value_.load(std::memory_order_relaxed);
}

}  // namespace asyncdataloader::metrics
