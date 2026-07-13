#include "benchmark/benchmark_report.h"

#include <iomanip>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace asyncdataloader::benchmark {

BenchmarkReport make_benchmark_report(
    std::string name,
    std::uint64_t bytes_read_per_iteration,
    std::uint64_t bytes_written_per_iteration,
    const std::vector<double>& samples_ms
) {
    if (name.empty() || name.find_first_of(",\r\n") != std::string::npos) {
        throw std::invalid_argument("benchmark name is not CSV-safe");
    }

    BenchmarkReport report;
    report.name = std::move(name);
    report.bytes_read_per_iteration = bytes_read_per_iteration;
    report.bytes_written_per_iteration = bytes_written_per_iteration;
    report.latency = summarize_latencies(samples_ms);
    report.total_elapsed_ms =
        std::accumulate(samples_ms.begin(), samples_ms.end(), 0.0);

    if (report.total_elapsed_ms > 0.0) {
        const double total_bytes_read =
            static_cast<double>(bytes_read_per_iteration) *
            static_cast<double>(report.latency.sample_count);
        report.throughput_mib_s = total_bytes_read / (1024.0 * 1024.0) /
                                  (report.total_elapsed_ms / 1000.0);
    }

    return report;
}

void write_benchmark_csv_header(std::ostream& output) {
    output
        << "name,bytes_per_iteration,bytes_written_per_iteration,sample_count,"
           "total_elapsed_ms,average_ms,p50_ms,p95_ms,p99_ms,throughput_mib_s\n";
}

void write_benchmark_csv_row(
    std::ostream& output,
    const BenchmarkReport& report
) {
    output << std::setprecision(10) << report.name << ','
           << report.bytes_read_per_iteration << ','
           << report.bytes_written_per_iteration << ','
           << report.latency.sample_count << ',' << report.total_elapsed_ms
           << ',' << report.latency.average_ms << ',' << report.latency.p50_ms
           << ',' << report.latency.p95_ms << ',' << report.latency.p99_ms
           << ',' << report.throughput_mib_s << '\n';
}

}  // namespace asyncdataloader::benchmark
