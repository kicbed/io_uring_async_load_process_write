#pragma once

#include <charconv>
#include <cstddef>
#include <string_view>
#include <system_error>

namespace asyncdataloader::benchmark {

inline constexpr std::size_t kMaxBenchmarkIterations = 1'000'000;
inline constexpr std::size_t kMaxBenchmarkBlockSize =
    std::size_t{1} * 1024 * 1024 * 1024;

inline bool parse_positive_size(
    std::string_view text,
    std::size_t maximum,
    std::size_t& value
) noexcept {
    if (text.empty() || maximum == 0) {
        return false;
    }

    std::size_t parsed = 0;
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() ||
        parsed == 0 || parsed > maximum) {
        return false;
    }

    value = parsed;
    return true;
}

}  // namespace asyncdataloader::benchmark
