#include "benchmark/benchmark_cli.h"
#include "benchmark/benchmark_report.h"
#include "benchmark/benchmark_stats.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int fail(const char* message) {
    std::cerr << message << '\n';
    return 1;
}

bool nearly_equal(double lhs, double rhs) {
    return std::abs(lhs - rhs) < 1e-9;
}

}  // namespace

int main() {
    using asyncdataloader::benchmark::make_benchmark_report;
    using asyncdataloader::benchmark::parse_positive_size;
    using asyncdataloader::benchmark::summarize_latencies;
    using asyncdataloader::benchmark::write_benchmark_csv_header;
    using asyncdataloader::benchmark::write_benchmark_csv_row;

    std::size_t parsed = 99;
    if (!parse_positive_size("3", 10, parsed) || parsed != 3) {
        return fail("positive bounded integer should parse");
    }
    if (parse_positive_size("0", 10, parsed) ||
        parse_positive_size("3junk", 10, parsed) ||
        parse_positive_size("11", 10, parsed) || parsed != 3) {
        return fail("invalid integer should fail without changing output");
    }

    const auto empty = summarize_latencies({});
    if (empty.sample_count != 0 || !nearly_equal(empty.average_ms, 0.0)) {
        return fail("empty samples should produce a zero summary");
    }

    const auto summary = summarize_latencies({100.0, 20.0, 40.0, 10.0, 30.0});
    if (summary.sample_count != 5) {
        return fail("sample_count should equal the number of samples");
    }
    if (!nearly_equal(summary.average_ms, 40.0)) {
        return fail("average_ms should be the arithmetic mean");
    }
    if (!nearly_equal(summary.p50_ms, 30.0) ||
        !nearly_equal(summary.p95_ms, 100.0) ||
        !nearly_equal(summary.p99_ms, 100.0)) {
        return fail("percentiles should use the nearest-rank definition");
    }

    try {
        (void)summarize_latencies({1.0, -1.0});
        return fail("negative latency should throw std::invalid_argument");
    } catch (const std::invalid_argument&) {
    }

    try {
        (void)summarize_latencies({1.0, std::nan("")});
        return fail("non-finite latency should throw std::invalid_argument");
    } catch (const std::invalid_argument&) {
    }

    const auto report = make_benchmark_report(
        "test_scan",
        2U * 1024U * 1024U,
        0,
        {1.0, 3.0, 2.0}
    );
    if (!nearly_equal(report.total_elapsed_ms, 6.0) ||
        !nearly_equal(report.throughput_mib_s, 1000.0)) {
        return fail("report should calculate aggregate throughput");
    }

    std::ostringstream csv;
    write_benchmark_csv_header(csv);
    write_benchmark_csv_row(csv, report);
    const std::string csv_text = csv.str();
    if (csv_text.find("name,bytes_per_iteration") != 0 ||
        csv_text.find("test_scan,2097152,0,3,6,2,2,3,3,1000") ==
            std::string::npos) {
        return fail("report should use the shared CSV schema");
    }

    return 0;
}
