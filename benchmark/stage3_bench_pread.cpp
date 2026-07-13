#include "benchmark/benchmark_cli.h"
#include "benchmark/benchmark_report.h"
#include "util/file_io.h"
#include "util/timer.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <new>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr
            << "usage: stage3_bench_pread <input> <block_size> [iterations]\n";
        return 1;
    }

    const char* input_path = argv[1];

    std::size_t block_size = 0;
    if (!asyncdataloader::benchmark::parse_positive_size(
            argv[2],
            asyncdataloader::benchmark::kMaxBenchmarkBlockSize,
            block_size
        )) {
        std::cerr << "invalid block_size: " << argv[2] << '\n';
        return 1;
    }

    std::size_t iterations = 1;
    if (argc == 4 &&
        !asyncdataloader::benchmark::parse_positive_size(
            argv[3],
            asyncdataloader::benchmark::kMaxBenchmarkIterations,
            iterations
        )) {
        std::cerr << "invalid iterations: " << argv[3] << '\n';
        return 1;
    }

    std::vector<char> block;
    std::vector<double> samples_ms;
    try {
        block.resize(block_size);
        samples_ms.reserve(iterations);
    } catch (const std::bad_alloc&) {
        std::cerr << "unable to allocate pread benchmark buffers\n";
        return 1;
    }

    auto input = asyncdataloader::util::open_read_only(input_path);
    if (input.error_number != 0) {
        std::cerr << "open failed: " << std::strerror(input.error_number)
                  << '\n';
        return 1;
    }

    volatile std::uint64_t checksum = 0;
    std::uint64_t bytes_read_per_iteration = 0;

    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        asyncdataloader::util::Timer timer;
        std::uint64_t offset = 0;
        std::uint64_t total_read = 0;

        while (true) {
            const auto read_result = asyncdataloader::util::read_at(
                input.fd.get(),
                block.data(),
                block.size(),
                offset
            );
            if (read_result.error_number != 0) {
                std::cerr << "pread failed: "
                          << std::strerror(read_result.error_number) << '\n';
                return 1;
            }
            if (read_result.bytes_read == 0) {
                break;
            }

            for (std::size_t i = 0; i < read_result.bytes_read; ++i) {
                checksum = checksum +
                           static_cast<unsigned char>(block[i]);
            }

            offset += read_result.bytes_read;
            total_read += read_result.bytes_read;
        }

        if (iteration == 0) {
            bytes_read_per_iteration = total_read;
        } else if (total_read != bytes_read_per_iteration) {
            std::cerr << "pread byte count changed between iterations\n";
            return 1;
        }

        samples_ms.push_back(timer.elapsed_ms());
    }

    const auto report = asyncdataloader::benchmark::make_benchmark_report(
        "pread_scan",
        bytes_read_per_iteration,
        0,
        samples_ms
    );
    asyncdataloader::benchmark::write_benchmark_csv_header(std::cout);
    asyncdataloader::benchmark::write_benchmark_csv_row(std::cout, report);

    return 0;
}
