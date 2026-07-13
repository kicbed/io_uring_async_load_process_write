#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "benchmark/benchmark_stats.h"

namespace asyncdataloader::benchmark {

BenchmarkSummary summarize_latencies(std::vector<double> samples_ms) {
    BenchmarkSummary summary;
    if (samples_ms.empty()) {
        return summary;
    }

    for (const double sample_ms : samples_ms) {
        if (sample_ms < 0.0 || !std::isfinite(sample_ms)) {
            throw std::invalid_argument(
                "latency samples must be finite and non-negative");
        }
    }

    summary.sample_count = samples_ms.size();
    const double total_ms =
        std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0);
    summary.average_ms =
        total_ms / static_cast<double>(summary.sample_count);

    std::sort(samples_ms.begin(), samples_ms.end());

    const auto nearest_rank = [&samples_ms](double percentile) {
        const auto rank = static_cast<std::size_t>(
            std::ceil(percentile * static_cast<double>(samples_ms.size())));
        return samples_ms[rank - 1];
    };

    summary.p50_ms = nearest_rank(0.50);
    summary.p95_ms = nearest_rank(0.95);
    summary.p99_ms = nearest_rank(0.99);

    return summary;
}

}  // namespace asyncdataloader::benchmark
