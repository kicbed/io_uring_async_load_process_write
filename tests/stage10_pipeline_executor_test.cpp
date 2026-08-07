#include "backend/sync_backend.h"
#include "backend/thread_pool_backend.h"
#include "config/pipeline_config.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_executor.h"
#include "pipeline/stage.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace {

using asyncdataloader::backend::SyncBackend;
using asyncdataloader::backend::ThreadPoolBackend;
using asyncdataloader::config::PipelineConfig;
using asyncdataloader::pipeline::Pipeline;
using asyncdataloader::pipeline::PipelineExecutor;
using asyncdataloader::pipeline::Stage;
using asyncdataloader::util::FdGuard;

int fail(std::string_view message) {
    std::cerr << "stage10 PipelineExecutor test failed: " << message << '\n';
    return 1;
}

FdGuard make_temp_file() {
    char path[] = "/tmp/asyncdataloader_stage10_executor_XXXXXX";
    const int raw_fd = ::mkstemp(path);
    if (raw_fd < 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "mkstemp"
        );
    }
    ::unlink(path);
    return FdGuard{raw_fd};
}

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
        if (call_count_ < observed_sizes_.size()) {
            observed_sizes_[call_count_] = block.size();
        }
        ++call_count_;

        if (worker_thread_ == std::thread::id{}) {
            worker_thread_ = std::this_thread::get_id();
        } else if (worker_thread_ != std::this_thread::get_id()) {
            throw std::runtime_error(
                "one-processor executor used multiple processor threads"
            );
        }

        for (std::byte& value : block) {
            const auto integer = std::to_integer<unsigned int>(value);
            value = std::byte{static_cast<unsigned char>(integer + 1U)};
        }
    }

    [[nodiscard]] std::size_t call_count() const noexcept {
        return call_count_;
    }

    [[nodiscard]] const std::array<std::size_t, 3>& observed_sizes()
        const noexcept {
        return observed_sizes_;
    }

    [[nodiscard]] std::thread::id worker_thread() const noexcept {
        return worker_thread_;
    }

private:
    std::size_t call_count_{0};
    std::array<std::size_t, 3> observed_sizes_{};
    std::thread::id worker_thread_{};
};

class ThrowingStage final : public Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "throwing";
    }

    void process(std::span<std::byte>) override {
        throw std::runtime_error("processor test failure");
    }
};

template <std::size_t Size>
void write_input(int fd, const std::array<std::byte, Size>& input) {
    const auto result = asyncdataloader::util::write_all_at(
        fd,
        input.data(),
        input.size(),
        0
    );
    if (result.error_number != 0 || result.bytes_written != input.size()) {
        throw std::runtime_error("could not prepare test input");
    }
}

int test_increment_pipeline(asyncdataloader::backend::IOBackend& backend) {
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
        std::byte{253},
        std::byte{254},
    };

    FdGuard input_fd = make_temp_file();
    FdGuard output_fd = make_temp_file();
    write_input(input_fd.get(), input);

    Pipeline processing;
    auto increment = std::make_unique<IncrementStage>();
    IncrementStage* const increment_view = increment.get();
    processing.add_stage(std::move(increment));

    PipelineExecutor executor(make_config(), backend, processing);
    const auto result = executor.run(input_fd.get(), output_fd.get());

    if (result.blocks_written != 3 || result.bytes_written != input.size()) {
        return fail("successful result did not report three blocks/19 bytes");
    }
    if (increment_view->call_count() != 3 ||
        increment_view->observed_sizes() !=
            std::array<std::size_t, 3>{8, 8, 3}) {
        return fail("processor did not receive two full blocks and one tail");
    }
    if (increment_view->worker_thread() == std::this_thread::get_id()) {
        return fail("processing Stage ran on the caller thread");
    }

    std::array<std::byte, 19> output{};
    const auto read_result = asyncdataloader::util::read_at(
        output_fd.get(),
        output.data(),
        output.size(),
        0
    );
    if (read_result.error_number != 0 ||
        read_result.bytes_read != output.size()) {
        return fail("could not read the complete transformed output");
    }

    for (std::size_t index = 0; index < input.size(); ++index) {
        const auto expected = std::to_integer<unsigned int>(input[index]) + 1U;
        if (output[index] !=
            std::byte{static_cast<unsigned char>(expected)}) {
            return fail("output was not the input transformed by +1");
        }
    }

    std::byte extra{};
    const auto eof_result = asyncdataloader::util::read_at(
        output_fd.get(),
        &extra,
        1,
        output.size()
    );
    if (eof_result.error_number != 0 || eof_result.bytes_read != 0) {
        return fail("output contained bytes beyond the transformed input");
    }
    return 0;
}

int test_empty_input_closes_all_stages() {
    FdGuard input_fd = make_temp_file();
    FdGuard output_fd = make_temp_file();

    SyncBackend backend;
    Pipeline processing;
    auto increment = std::make_unique<IncrementStage>();
    IncrementStage* const increment_view = increment.get();
    processing.add_stage(std::move(increment));

    PipelineExecutor executor(make_config(), backend, processing);
    const auto result = executor.run(input_fd.get(), output_fd.get());

    if (result.blocks_written != 0 || result.bytes_written != 0 ||
        increment_view->call_count() != 0) {
        return fail("empty input did not drain as a zero-work pipeline");
    }
    return 0;
}

int test_processor_failure_reaches_caller() {
    std::array<std::byte, 64> input{};
    input.fill(std::byte{7});

    FdGuard input_fd = make_temp_file();
    FdGuard output_fd = make_temp_file();
    write_input(input_fd.get(), input);

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<ThrowingStage>());
    PipelineExecutor executor(make_config(), backend, processing);

    try {
        [[maybe_unused]] const auto result =
            executor.run(input_fd.get(), output_fd.get());
        return fail("processor exception did not fail the run");
    } catch (const std::runtime_error& error) {
        if (std::string_view{error.what()} != "processor test failure") {
            return fail("caller received the wrong processor exception");
        }
    } catch (...) {
        return fail("processor failure changed exception type");
    }
    return 0;
}

int test_writer_failure_reaches_caller() {
    std::array<std::byte, 32> input{};
    input.fill(std::byte{9});

    FdGuard input_fd = make_temp_file();
    write_input(input_fd.get(), input);

    int pipe_fds[2]{};
    if (::pipe(pipe_fds) != 0) {
        throw std::system_error(errno, std::generic_category(), "pipe");
    }
    [[maybe_unused]] FdGuard pipe_read{pipe_fds[0]};
    FdGuard pipe_write{pipe_fds[1]};

    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(std::make_unique<IncrementStage>());
    PipelineExecutor executor(make_config(), backend, processing);

    try {
        [[maybe_unused]] const auto result =
            executor.run(input_fd.get(), pipe_write.get());
        return fail("writer pwrite failure did not fail the run");
    } catch (const std::system_error& error) {
        if (error.code().value() != ESPIPE) {
            return fail("writer failure did not preserve ESPIPE");
        }
    } catch (...) {
        return fail("writer failure changed exception type");
    }
    return 0;
}

int test_empty_processing_pipeline_is_rejected() {
    FdGuard input_fd = make_temp_file();
    FdGuard output_fd = make_temp_file();
    SyncBackend backend;
    Pipeline processing;
    PipelineExecutor executor(make_config(), backend, processing);

    try {
        [[maybe_unused]] const auto result =
            executor.run(input_fd.get(), output_fd.get());
        return fail("executor accepted a pipeline with no processing Stage");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("empty processing pipeline used the wrong error type");
    }
    return 0;
}

}  // namespace

int main() {
    try {
        {
            SyncBackend backend;
            if (const int result = test_increment_pipeline(backend);
                result != 0) {
                return result;
            }
        }
        {
            ThreadPoolBackend backend(1, 1);
            if (const int result = test_increment_pipeline(backend);
                result != 0) {
                return result;
            }
        }
        if (const int result = test_empty_input_closes_all_stages();
            result != 0) {
            return result;
        }
        if (const int result = test_processor_failure_reaches_caller();
            result != 0) {
            return result;
        }
        if (const int result = test_writer_failure_reaches_caller();
            result != 0) {
            return result;
        }
        if (const int result = test_empty_processing_pipeline_is_rejected();
            result != 0) {
            return result;
        }
    } catch (const std::exception& error) {
        std::cerr << "stage10 PipelineExecutor test setup failed: "
                  << error.what() << '\n';
        return 1;
    }

    return 0;
}
