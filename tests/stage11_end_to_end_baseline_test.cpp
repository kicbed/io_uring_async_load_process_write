#include "benchmark/end_to_end_baseline.h"

#include "config/pipeline_config.h"
#include "pipeline/builtin_stages.h"
#include "pipeline/pipeline.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <fcntl.h>
#include <unistd.h>

namespace {

using asyncdataloader::config::PipelineConfig;
using asyncdataloader::pipeline::ByteIncrementStage;
using asyncdataloader::pipeline::Pipeline;
using asyncdataloader::util::FdGuard;

int fail(std::string_view message) {
    std::cerr << "stage11 end-to-end baseline test failed: "
              << message << '\n';
    return 1;
}

class TempDirectory {
public:
    TempDirectory() {
        char path[] = "/tmp/asyncdataloader_stage11_baseline_XXXXXX";
        const char* created = ::mkdtemp(path);
        if (created == nullptr) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "mkdtemp"
            );
        }
        path_ = created;
    }

    ~TempDirectory() noexcept {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] std::filesystem::path file(
        std::string_view name
    ) const {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

FdGuard open_read_write_truncated(const std::filesystem::path& path) {
    const int raw_fd = ::open(
        path.c_str(),
        O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC,
        0600
    );
    if (raw_fd < 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "open test file"
        );
    }
    return FdGuard{raw_fd};
}

template <std::size_t Size>
void write_bytes(int fd, const std::array<std::byte, Size>& bytes) {
    const asyncdataloader::util::WriteAtResult result =
        asyncdataloader::util::write_all_at(
            fd,
            bytes.data(),
            bytes.size(),
            0
        );
    if (result.error_number != 0 || result.bytes_written != bytes.size()) {
        throw std::runtime_error("could not prepare test bytes");
    }
}

PipelineConfig make_config() {
    PipelineConfig config;
    config.block_size = 8;
    config.max_inflight_buffers = 3;
    config.queue_depth = 1;
    config.buffer_alignment = 8;
    return config;
}

int test_serial_reference_and_bounded_verifier() {
    TempDirectory directory;
    const auto input_path = directory.file("input.bin");
    const auto output_path = directory.file("serial.bin");
    const auto expected_path = directory.file("expected.bin");

    const std::array<std::byte, 19> input{
        std::byte{0},
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{10},
        std::byte{20},
        std::byte{30},
        std::byte{40},
        std::byte{50},
        std::byte{60},
        std::byte{70},
        std::byte{80},
        std::byte{90},
        std::byte{100},
        std::byte{110},
        std::byte{120},
        std::byte{200},
        std::byte{254},
        std::byte{255},
    };
    std::array<std::byte, input.size()> expected = input;
    for (std::byte& value : expected) {
        const unsigned int number = std::to_integer<unsigned int>(value);
        value = std::byte{static_cast<unsigned char>(number + 1U)};
    }

    FdGuard input_fd = open_read_write_truncated(input_path);
    FdGuard output_fd = open_read_write_truncated(output_path);
    FdGuard expected_fd = open_read_write_truncated(expected_path);
    write_bytes(input_fd.get(), input);
    write_bytes(expected_fd.get(), expected);

    Pipeline processing;
    processing.add_stage(std::make_unique<ByteIncrementStage>());
    const auto result =
        asyncdataloader::benchmark::run_serial_pipeline_reference(
            input_fd.get(),
            output_fd.get(),
            make_config(),
            processing
        );

    if (result.blocks_written != 3 || result.bytes_written != input.size()) {
        return fail("serial reference did not report 3 blocks and 19 bytes");
    }
    const std::uint64_t verified =
        asyncdataloader::benchmark::verify_files_equal_bounded(
            output_path,
            expected_path,
            make_config()
        );
    if (verified != input.size()) {
        return fail("bounded verifier returned the wrong byte count");
    }

    const std::array<std::byte, 1> wrong_byte{std::byte{99}};
    const auto overwrite = asyncdataloader::util::write_all_at(
        expected_fd.get(),
        wrong_byte.data(),
        wrong_byte.size(),
        5
    );
    if (overwrite.error_number != 0 || overwrite.bytes_written != 1) {
        throw std::runtime_error("could not prepare mismatching output");
    }

    try {
        static_cast<void>(
            asyncdataloader::benchmark::verify_files_equal_bounded(
                output_path,
                expected_path,
                make_config()
            )
        );
        return fail("bounded verifier accepted different output bytes");
    } catch (const std::runtime_error& error) {
        if (std::string_view{error.what()}.find("byte offset 5") ==
            std::string_view::npos) {
            return fail("bounded verifier reported the wrong mismatch offset");
        }
    }
    return 0;
}

int test_empty_input_and_empty_pipeline_validation() {
    TempDirectory directory;
    FdGuard input = open_read_write_truncated(directory.file("empty.bin"));
    FdGuard output = open_read_write_truncated(directory.file("output.bin"));

    Pipeline processing;
    processing.add_stage(std::make_unique<ByteIncrementStage>());
    const auto result =
        asyncdataloader::benchmark::run_serial_pipeline_reference(
            input.get(),
            output.get(),
            make_config(),
            processing
        );
    if (result.blocks_written != 0 || result.bytes_written != 0) {
        return fail("empty input produced nonzero serial work");
    }

    Pipeline empty_pipeline;
    try {
        static_cast<void>(
            asyncdataloader::benchmark::run_serial_pipeline_reference(
                input.get(),
                output.get(),
                make_config(),
                empty_pipeline
            )
        );
        return fail("serial reference accepted an empty stage chain");
    } catch (const std::invalid_argument&) {
    }
    return 0;
}

}  // namespace

int main() {
    try {
        if (const int result = test_serial_reference_and_bounded_verifier();
            result != 0) {
            return result;
        }
        if (const int result = test_empty_input_and_empty_pipeline_validation();
            result != 0) {
            return result;
        }
    } catch (const std::exception& error) {
        std::cerr << "stage11 end-to-end baseline test threw: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
