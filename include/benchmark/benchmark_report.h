#pragma once

#include "benchmark/benchmark_stats.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace asyncdataloader::benchmark {

struct BenchmarkReport {
    std::string name;
    std::uint64_t bytes_read_per_iteration{0};
    std::uint64_t bytes_written_per_iteration{0};
    BenchmarkSummary latency{};
    double total_elapsed_ms{0.0};
    double throughput_mib_s{0.0};
};

BenchmarkReport make_benchmark_report(
    std::string name,
    std::uint64_t bytes_read_per_iteration,
    std::uint64_t bytes_written_per_iteration,
    const std::vector<double>& samples_ms
);

void write_benchmark_csv_header(std::ostream& output);
void write_benchmark_csv_row(
    std::ostream& output,
    const BenchmarkReport& report
);

}  // namespace asyncdataloader::benchmark
