#include "benchmark/benchmark_cli.h"
#include "benchmark/benchmark_report.h"
#include "util/timer.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) {
        std::cerr << "usage: stage3_bench_mmap <input> [iterations]\n";
        return 1;
    }

    const char* input_path = argv[1];
    std::size_t iterations = 1;
    if (argc == 3 &&
        !asyncdataloader::benchmark::parse_positive_size(
            argv[2],
            asyncdataloader::benchmark::kMaxBenchmarkIterations,
            iterations
        )) {
        std::cerr << "invalid iterations: " << argv[2] << '\n';
        return 1;
    }

    std::vector<double> samples_ms;
    try {
        samples_ms.reserve(iterations);
    } catch (const std::bad_alloc&) {
        std::cerr << "unable to allocate benchmark samples\n";
        return 1;
    }

    const int fd = ::open(input_path, O_RDONLY);
    if (fd < 0) {
        std::cerr << "open failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    struct stat file_stat {};
    if (::fstat(fd, &file_stat) < 0) {
        const int error_number = errno;
        ::close(fd);
        std::cerr << "fstat failed: " << std::strerror(error_number) << '\n';
        return 1;
    }
    if (file_stat.st_size < 0) {
        ::close(fd);
        std::cerr << "fstat returned a negative file size\n";
        return 1;
    }

    const std::uint64_t file_size =
        static_cast<std::uint64_t>(file_stat.st_size);
    if (file_size > std::numeric_limits<std::size_t>::max()) {
        ::close(fd);
        std::cerr << "input is too large for this address space\n";
        return 1;
    }

    volatile std::uint64_t checksum = 0;
    if (file_size == 0) {
        samples_ms.assign(iterations, 0.0);
    } else {
        const std::size_t mapping_size = static_cast<std::size_t>(file_size);
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            asyncdataloader::util::Timer timer;

            void* mapped =
                ::mmap(nullptr, mapping_size, PROT_READ, MAP_PRIVATE, fd, 0);
            if (mapped == MAP_FAILED) {
                const int error_number = errno;
                ::close(fd);
                std::cerr << "mmap failed: "
                          << std::strerror(error_number) << '\n';
                return 1;
            }

            const auto* bytes = static_cast<const unsigned char*>(mapped);
            for (std::size_t i = 0; i < mapping_size; ++i) {
                checksum = checksum + bytes[i];
            }

            if (::munmap(mapped, mapping_size) < 0) {
                const int error_number = errno;
                ::close(fd);
                std::cerr << "munmap failed: "
                          << std::strerror(error_number) << '\n';
                return 1;
            }

            samples_ms.push_back(timer.elapsed_ms());
        }
    }

    if (::close(fd) < 0) {
        std::cerr << "close failed: " << std::strerror(errno) << '\n';
        return 1;
    }

    const auto report = asyncdataloader::benchmark::make_benchmark_report(
        "mmap_scan",
        file_size,
        0,
        samples_ms
    );
    asyncdataloader::benchmark::write_benchmark_csv_header(std::cout);
    asyncdataloader::benchmark::write_benchmark_csv_row(std::cout, report);

    return 0;
}
