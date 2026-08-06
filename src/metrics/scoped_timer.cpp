#include "metrics/scoped_timer.h"

#include <chrono>

namespace asyncdataloader::metrics {

ScopedTimer::ScopedTimer(Histogram& histogram) noexcept
    : histogram_(histogram),
      start_(Clock::now()) {}

ScopedTimer::~ScopedTimer() noexcept {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - start_
    ).count();
    const Histogram::Value sample = elapsed > 0
        ? static_cast<Histogram::Value>(elapsed)
        : 0;
    histogram_.observe(sample);
}

}  // namespace asyncdataloader::metrics
