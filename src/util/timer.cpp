#include "util/timer.h"

namespace asyncdataloader::util {

Timer::Timer() noexcept : start_(std::chrono::steady_clock::now()){

}

void Timer::reset() noexcept {
    start_ = std::chrono::steady_clock::now();
}

std::uint64_t Timer::elapsed_ns() const noexcept {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_).count();
}

double Timer::elapsed_ms() const noexcept {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(now - start_).count();
}

}  // namespace asyncdataloader::util
