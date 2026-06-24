#include "baseline/sync_preprocess_baseline.h"
#include "util/timer.h"

#include <iostream>
#include <cerrno>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: stage2_sync_preprocess_demo <input> <output> <block_size>\n";
        return 1;
    }

    const char* input_path = argv[1];
    const char* output_path = argv[2];
    std::size_t block_size = std::stoull(argv[3]);

    asyncdataloader::baseline::SyncPreprocessConfig config{
    input_path,
    output_path,
    block_size,
    };

    asyncdataloader::util::Timer timer;
    asyncdataloader::baseline::SyncPreprocessResult result =
        asyncdataloader::baseline::run_sync_preprocess_baseline(config);
    double elapsed_ms = timer.elapsed_ms();

    if (result.error_number != 0) {
        std::cerr << "error: " << std::strerror(result.error_number) << "\n";
        return 1;
    }

    std::cout << "bytes_read=" << result.bytes_read
                << " bytes_written=" << result.bytes_written
                << " elapsed_ms=" << elapsed_ms << '\n';

    return 0;
}
