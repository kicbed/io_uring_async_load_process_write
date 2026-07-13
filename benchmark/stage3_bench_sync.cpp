#include "baseline/sync_preprocess_baseline.h"
#include "benchmark/benchmark_cli.h"
#include "benchmark/benchmark_report.h"
#include "util/timer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <new>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        std::cerr
            << "usage: stage3_bench_sync <input> <output> <block_size> "
               "[iterations]\n";
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];

    std::size_t block_size = 0;
    if (!asyncdataloader::benchmark::parse_positive_size(
            argv[3],
            asyncdataloader::benchmark::kMaxBenchmarkBlockSize,
            block_size
        )) {
        std::cerr << "invalid block_size: " << argv[3] << '\n';
        return 1;
    }

    std::size_t iterations = 1;
    if (argc == 5 &&
        !asyncdataloader::benchmark::parse_positive_size(
            argv[4],
            asyncdataloader::benchmark::kMaxBenchmarkIterations,
            iterations
        )) {
        std::cerr << "invalid iterations: " << argv[4] << '\n';
        return 1;
    }

    std::vector<double> samples_ms;
    try {
        samples_ms.reserve(iterations);
    } catch (const std::bad_alloc&) {
        std::cerr << "unable to allocate benchmark samples\n";
        return 1;
    }

    const asyncdataloader::baseline::SyncPreprocessConfig config{
        .input_path = input_path,
        .output_path = output_path,
        .block_size = block_size,
    };

    std::uint64_t bytes_read_per_iteration = 0;
    std::uint64_t bytes_written_per_iteration = 0;

    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        asyncdataloader::util::Timer timer;

        asyncdataloader::baseline::SyncPreprocessResult result;
        try {
            result =
                asyncdataloader::baseline::run_sync_preprocess_baseline(config);
        } catch (const std::bad_alloc&) {
            std::cerr << "unable to allocate sync preprocessing buffer\n";
            return 1;
        }

        const double elapsed_ms = timer.elapsed_ms();
        if (result.error_number != 0) {
            std::cerr << "run_sync_preprocess_baseline failed: "
                      << std::strerror(result.error_number) << '\n';
            return 1;
        }

        if (iteration == 0) {
            bytes_read_per_iteration = result.bytes_read;
            bytes_written_per_iteration = result.bytes_written;
        } else if (result.bytes_read != bytes_read_per_iteration ||
                   result.bytes_written != bytes_written_per_iteration) {
            std::cerr << "sync benchmark byte counts changed between iterations\n";
            return 1;
        }

        samples_ms.push_back(elapsed_ms);
    }

    const auto report = asyncdataloader::benchmark::make_benchmark_report(
        "sync_baseline",
        bytes_read_per_iteration,
        bytes_written_per_iteration,
        samples_ms
    );
    asyncdataloader::benchmark::write_benchmark_csv_header(std::cout);
    asyncdataloader::benchmark::write_benchmark_csv_row(std::cout, report);

    return 0;
}
