#include "baseline/sync_preprocess_baseline.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {

int fail(const char* message) {
    std::cerr << "stage2_sync_preprocess_baseline_test: " << message << '\n';
    return 1;
}

int create_temp_file(char* path, std::size_t path_size) {
    if (path_size < sizeof("/tmp/asyncdataloader_sync_baseline_test_XXXXXX")) {
        return -1;
    }

    std::strcpy(path, "/tmp/asyncdataloader_sync_baseline_test_XXXXXX");
    const int fd = ::mkstemp(path);
    if (fd < 0) {
        std::cerr << "mkstemp failed: " << std::strerror(errno) << '\n';
        return -1;
    }

    return fd;
}

int test_sync_preprocess_baseline_processes_file_in_blocks() {
    char input_path[64]{};
    char output_path[64]{};

    const int input_fd = create_temp_file(input_path, sizeof(input_path));
    if (input_fd < 0) {
        return 1;
    }

    const int output_fd = create_temp_file(output_path, sizeof(output_path));
    if (output_fd < 0) {
        ::close(input_fd);
        ::unlink(input_path);
        return 1;
    }
    ::close(output_fd);

    constexpr char input[] = "abcXYZ";
    if (::write(input_fd, input, sizeof(input) - 1) != static_cast<ssize_t>(sizeof(input) - 1)) {
        ::close(input_fd);
        ::unlink(input_path);
        ::unlink(output_path);
        return fail("failed to write input contents");
    }
    ::close(input_fd);

    const asyncdataloader::baseline::SyncPreprocessConfig config{
        input_path,
        output_path,
        2,
    };
    const asyncdataloader::baseline::SyncPreprocessResult result =
        asyncdataloader::baseline::run_sync_preprocess_baseline(config);

    if (result.error_number != 0) {
        ::unlink(input_path);
        ::unlink(output_path);
        return fail("run_sync_preprocess_baseline should complete without error");
    }
    if (result.bytes_read != sizeof(input) - 1) {
        ::unlink(input_path);
        ::unlink(output_path);
        return fail("run_sync_preprocess_baseline should report total bytes read");
    }
    if (result.bytes_written != sizeof(input) - 1) {
        ::unlink(input_path);
        ::unlink(output_path);
        return fail("run_sync_preprocess_baseline should report total bytes written");
    }

    const int verify_fd = ::open(output_path, O_RDONLY);
    if (verify_fd < 0) {
        ::unlink(input_path);
        ::unlink(output_path);
        return fail("test setup should reopen output file");
    }

    std::array<char, sizeof(input) - 1> output{};
    const ssize_t bytes_read = ::read(verify_fd, output.data(), output.size());
    ::close(verify_fd);
    ::unlink(input_path);
    ::unlink(output_path);

    if (bytes_read != static_cast<ssize_t>(output.size())) {
        return fail("output file should contain the transformed byte count");
    }
    if (std::memcmp(output.data(), "ABCxyz", output.size()) != 0) {
        return fail("baseline should transform each block before writing output");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int result = test_sync_preprocess_baseline_processes_file_in_blocks(); result != 0) {
        return result;
    }

    return 0;
}
