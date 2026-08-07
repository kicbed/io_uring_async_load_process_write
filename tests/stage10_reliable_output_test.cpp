#include "backend/sync_backend.h"
#include "config/pipeline_config.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_executor.h"
#include "pipeline/stage.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using asyncdataloader::backend::SyncBackend;
using asyncdataloader::config::PipelineConfig;
using asyncdataloader::pipeline::Pipeline;
using asyncdataloader::pipeline::PipelineExecutor;
using asyncdataloader::pipeline::Stage;
using asyncdataloader::util::FdGuard;

int fail(std::string_view message) {
    std::cerr << "stage10 reliable-output test failed: " << message << '\n';
    return 1;
}

class TempDirectory {
public:
    TempDirectory() {
        char path_template[] =
            "/tmp/asyncdataloader_stage10_reliable_XXXXXX";
        char* const created = ::mkdtemp(path_template);
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

    [[nodiscard]] std::filesystem::path child(
        std::string_view name
    ) const {
        return path_ / name;
    }

    [[nodiscard]] std::size_t entry_count() const {
        return static_cast<std::size_t>(std::distance(
            std::filesystem::directory_iterator{path_},
            std::filesystem::directory_iterator{}
        ));
    }

private:
    std::filesystem::path path_;
};

PipelineConfig make_config() {
    PipelineConfig config;
    config.block_size = 8;
    config.max_inflight_buffers = 3;
    config.queue_depth = 1;
    config.buffer_alignment = 8;
    return config;
}

class IncrementStage final : public Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "increment";
    }

    void process(std::span<std::byte> block) override {
        for (std::byte& value : block) {
            const auto integer = std::to_integer<unsigned int>(value);
            value = std::byte{static_cast<unsigned char>(integer + 1U)};
        }
    }
};

class ThrowingStage final : public Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "throwing";
    }

    void process(std::span<std::byte>) override {
        throw std::runtime_error("reliable-output processor failure");
    }
};

void write_file(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes
) {
    const int raw_fd = ::open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0600
    );
    if (raw_fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open test file");
    }

    FdGuard fd{raw_fd};
    const auto result = asyncdataloader::util::write_all_at(
        fd.get(),
        bytes.data(),
        bytes.size(),
        0
    );
    if (result.error_number != 0 || result.bytes_written != bytes.size()) {
        throw std::runtime_error("could not prepare reliable-output test file");
    }
}

bool file_matches(
    const std::filesystem::path& path,
    std::span<const std::byte> expected
) {
    auto opened = asyncdataloader::util::open_read_only(path.c_str());
    if (opened.error_number != 0) {
        return false;
    }

    std::vector<std::byte> actual(expected.size() + 1);
    const auto result = asyncdataloader::util::read_at(
        opened.fd.get(),
        actual.data(),
        actual.size(),
        0
    );
    return result.error_number == 0 &&
        result.bytes_read == expected.size() &&
        std::equal(expected.begin(), expected.end(), actual.begin());
}

template <std::size_t Size>
std::array<std::byte, Size> incremented(
    const std::array<std::byte, Size>& input
) {
    std::array<std::byte, Size> output{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto integer = std::to_integer<unsigned int>(input[index]);
        output[index] =
            std::byte{static_cast<unsigned char>(integer + 1U)};
    }
    return output;
}

int test_success_replaces_existing_output() {
    TempDirectory directory;
    const auto input_path = directory.child("input.bin");
    const auto output_path = directory.child("output.bin");

    const std::array<std::byte, 19> input{
        std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
        std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
        std::byte{50}, std::byte{60}, std::byte{70}, std::byte{80},
        std::byte{90}, std::byte{100}, std::byte{110}, std::byte{120},
        std::byte{200}, std::byte{253}, std::byte{254},
    };
    const std::array<std::byte, 4> old_output{
        std::byte{9}, std::byte{9}, std::byte{9}, std::byte{9},
    };
    write_file(input_path, input);
    write_file(output_path, old_output);

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<IncrementStage>());
    PipelineExecutor executor(make_config(), backend, processing);
    const auto result = executor.run_file(input_path, output_path);

    const auto expected = incremented(input);
    if (result.blocks_written != 3 || result.bytes_written != input.size()) {
        return fail("successful path run reported the wrong size");
    }
    if (!file_matches(output_path, expected)) {
        return fail("successful commit did not replace output with +1 data");
    }
    if (!file_matches(input_path, input)) {
        return fail("successful commit changed the input file");
    }
    if (directory.entry_count() != 2) {
        return fail("successful commit left a temporary file behind");
    }
    return 0;
}

int test_processing_failure_preserves_existing_output() {
    TempDirectory directory;
    const auto input_path = directory.child("input.bin");
    const auto output_path = directory.child("output.bin");

    std::array<std::byte, 64> input{};
    input.fill(std::byte{7});
    const std::array<std::byte, 5> old_output{
        std::byte{1}, std::byte{3}, std::byte{5},
        std::byte{7}, std::byte{9},
    };
    write_file(input_path, input);
    write_file(output_path, old_output);

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<ThrowingStage>());
    PipelineExecutor executor(make_config(), backend, processing);

    try {
        [[maybe_unused]] const auto result =
            executor.run_file(input_path, output_path);
        return fail("processor failure unexpectedly committed output");
    } catch (const std::runtime_error& error) {
        if (std::string_view{error.what()} !=
            "reliable-output processor failure") {
            return fail("processor failure message changed");
        }
    } catch (...) {
        return fail("processor failure changed exception type");
    }

    if (!file_matches(output_path, old_output)) {
        return fail("processor failure damaged the existing final output");
    }
    if (directory.entry_count() != 2) {
        return fail("processor failure left a temporary file behind");
    }
    return 0;
}

int test_rename_failure_cleans_temporary_output() {
    TempDirectory directory;
    const auto input_path = directory.child("input.bin");
    const auto output_path = directory.child("final-directory");
    const std::array<std::byte, 8> input{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
        std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8},
    };
    write_file(input_path, input);
    std::filesystem::create_directory(output_path);

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<IncrementStage>());
    PipelineExecutor executor(make_config(), backend, processing);

    bool rename_failed = false;
    try {
        [[maybe_unused]] const auto result =
            executor.run_file(input_path, output_path);
    } catch (const std::system_error&) {
        rename_failed = true;
    }
    if (!rename_failed || !std::filesystem::is_directory(output_path)) {
        return fail("rename failure did not preserve the target directory");
    }
    if (directory.entry_count() != 2) {
        return fail("rename failure left a temporary file behind");
    }
    return 0;
}

int test_same_input_and_output_is_rejected() {
    TempDirectory directory;
    const auto input_path = directory.child("input.bin");
    const std::array<std::byte, 4> input{
        std::byte{11}, std::byte{12}, std::byte{13}, std::byte{14},
    };
    write_file(input_path, input);

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<IncrementStage>());
    PipelineExecutor executor(make_config(), backend, processing);

    try {
        [[maybe_unused]] const auto result =
            executor.run_file(input_path, input_path);
        return fail("same input/output path was accepted");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("same input/output rejection used the wrong error type");
    }

    if (!file_matches(input_path, input) || directory.entry_count() != 1) {
        return fail("same-file rejection changed input or created a temp file");
    }
    return 0;
}

int test_missing_input_preserves_existing_output() {
    TempDirectory directory;
    const auto input_path = directory.child("missing.bin");
    const auto output_path = directory.child("output.bin");
    const std::array<std::byte, 3> old_output{
        std::byte{21}, std::byte{22}, std::byte{23},
    };
    write_file(output_path, old_output);

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<IncrementStage>());
    PipelineExecutor executor(make_config(), backend, processing);

    try {
        [[maybe_unused]] const auto result =
            executor.run_file(input_path, output_path);
        return fail("missing input unexpectedly ran the pipeline");
    } catch (const std::system_error& error) {
        if (error.code().value() != ENOENT) {
            return fail("missing input did not preserve ENOENT");
        }
    } catch (...) {
        return fail("missing input used the wrong error type");
    }

    if (!file_matches(output_path, old_output) ||
        directory.entry_count() != 1) {
        return fail("missing input changed output or created a temp file");
    }
    return 0;
}

int test_empty_input_commits_an_empty_output() {
    TempDirectory directory;
    const auto input_path = directory.child("input.bin");
    const auto output_path = directory.child("output.bin");
    const std::array<std::byte, 0> empty_input{};
    const std::array<std::byte, 2> old_output{
        std::byte{31}, std::byte{32},
    };
    write_file(input_path, empty_input);
    write_file(output_path, old_output);

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<IncrementStage>());
    PipelineExecutor executor(make_config(), backend, processing);
    const auto result = executor.run_file(input_path, output_path);

    if (result.blocks_written != 0 || result.bytes_written != 0 ||
        std::filesystem::file_size(output_path) != 0) {
        return fail("empty input did not atomically commit an empty output");
    }
    if (directory.entry_count() != 2) {
        return fail("empty-input commit left a temporary file behind");
    }
    return 0;
}

}  // namespace

int main() {
    try {
        if (const int result = test_success_replaces_existing_output();
            result != 0) {
            return result;
        }
        if (const int result = test_processing_failure_preserves_existing_output();
            result != 0) {
            return result;
        }
        if (const int result = test_rename_failure_cleans_temporary_output();
            result != 0) {
            return result;
        }
        if (const int result = test_same_input_and_output_is_rejected();
            result != 0) {
            return result;
        }
        if (const int result = test_missing_input_preserves_existing_output();
            result != 0) {
            return result;
        }
        if (const int result = test_empty_input_commits_an_empty_output();
            result != 0) {
            return result;
        }
    } catch (const std::exception& error) {
        std::cerr << "stage10 reliable-output test setup failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
