#pragma once

#include <cstddef>
#include <vector>

namespace asyncdataloader::benchmark {

struct BenchmarkSummary {
    std::size_t sample_count{0};
    double average_ms{0.0};
    double p50_ms{0.0};
    double p95_ms{0.0};
    double p99_ms{0.0};
};

// Uses the nearest-rank percentile definition. Empty input returns zero values.
// Negative or non-finite latency samples are invalid.
BenchmarkSummary summarize_latencies(std::vector<double> samples_ms);

}  // namespace asyncdataloader::benchmark
