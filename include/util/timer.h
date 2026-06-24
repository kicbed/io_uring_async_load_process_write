#pragma once

#include <chrono>
#include <cstdint>

namespace asyncdataloader::util {

class Timer {
public:
    Timer() noexcept;

    void reset() noexcept;
    [[nodiscard]] std::uint64_t elapsed_ns() const noexcept;
    [[nodiscard]] double elapsed_ms() const noexcept;

private:
    std::chrono::steady_clock::time_point start_{};
};

}  // namespace asyncdataloader::util
