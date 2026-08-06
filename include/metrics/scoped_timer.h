#pragma once

#include "metrics/histogram.h"

#include <chrono>

namespace asyncdataloader::metrics {

// Records one steady-clock duration in nanoseconds when the scope ends.
class ScopedTimer {
public:
    explicit ScopedTimer(Histogram& histogram) noexcept;
    ~ScopedTimer() noexcept;

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
    ScopedTimer(ScopedTimer&&) = delete;
    ScopedTimer& operator=(ScopedTimer&&) = delete;

private:
    using Clock = std::chrono::steady_clock;

    Histogram& histogram_;
    Clock::time_point start_;
};

}  // namespace asyncdataloader::metrics
