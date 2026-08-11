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
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <optional>
#include <poll.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using asyncdataloader::backend::SyncBackend;
using asyncdataloader::config::PipelineConfig;
using asyncdataloader::pipeline::Pipeline;
using asyncdataloader::pipeline::PipelineExecutor;
using asyncdataloader::pipeline::Stage;
using asyncdataloader::util::FdGuard;

constexpr std::size_t kBlockSize = 64;
constexpr std::size_t kInputBlockCount = 3;
constexpr auto kWaitTimeout = std::chrono::seconds{5};

int fail(std::string_view message) {
    std::cerr << "stage13 crash-safety test failed: " << message << '\n';
    return 1;
}

class TempDirectory {
public:
    TempDirectory() {
        char path_template[] =
            "/tmp/asyncdataloader_stage13_crash_XXXXXX";
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

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class ChildProcess {
public:
    explicit ChildProcess(pid_t pid) noexcept : pid_(pid) {}

    ~ChildProcess() noexcept {
        terminate_and_reap();
    }

    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    [[nodiscard]] int kill_and_wait() {
        if (pid_ <= 0) {
            throw std::logic_error("child process was already reaped");
        }

        if (::kill(pid_, SIGKILL) != 0 && errno != ESRCH) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "kill crash-test child"
            );
        }

        int status = 0;
        pid_t wait_result = -1;
        do {
            wait_result = ::waitpid(pid_, &status, 0);
        } while (wait_result < 0 && errno == EINTR);

        if (wait_result < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "waitpid crash-test child"
            );
        }

        pid_ = -1;
        return status;
    }

private:
    void terminate_and_reap() noexcept {
        if (pid_ <= 0) {
            return;
        }

        static_cast<void>(::kill(pid_, SIGKILL));
        int status = 0;
        pid_t wait_result = -1;
        do {
            wait_result = ::waitpid(pid_, &status, 0);
        } while (wait_result < 0 && errno == EINTR);
        pid_ = -1;
    }

    pid_t pid_{-1};
};

class BlockingIncrementStage final : public Stage {
public:
    explicit BlockingIncrementStage(int ready_fd) noexcept
        : ready_fd_(ready_fd) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "blocking_increment";
    }

    void process(std::span<std::byte> block) override {
        for (std::byte& value : block) {
            const unsigned int numeric =
                std::to_integer<unsigned int>(value);
            value = std::byte{static_cast<unsigned char>(numeric + 1U)};
        }

        ++processed_block_count_;
        if (processed_block_count_ != 2) {
            return;
        }

        notify_parent();
        while (true) {
            static_cast<void>(::pause());
        }
    }

private:
    void notify_parent() {
        constexpr char marker = 'R';
        ssize_t write_result = -1;
        do {
            write_result = ::write(ready_fd_, &marker, 1);
        } while (write_result < 0 && errno == EINTR);

        if (write_result < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "write crash-test readiness marker"
            );
        }
        if (write_result != 1) {
            throw std::runtime_error(
                "crash-test readiness marker was a short write"
            );
        }
    }

    int ready_fd_{-1};
    std::size_t processed_block_count_{0};
};

PipelineConfig make_config() {
    PipelineConfig config;
    config.block_size = kBlockSize;
    config.max_inflight_buffers = 3;
    config.queue_depth = 1;
    config.buffer_alignment = kBlockSize;
    return config;
}

std::array<std::byte, kBlockSize * kInputBlockCount> make_input() {
    std::array<std::byte, kBlockSize * kInputBlockCount> input{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = std::byte{static_cast<unsigned char>(
            (index * 17U) % 251U
        )};
    }
    return input;
}

std::array<std::byte, kBlockSize> expected_first_output_block(
    const std::array<std::byte, kBlockSize * kInputBlockCount>& input
) {
    std::array<std::byte, kBlockSize> expected{};
    std::copy_n(input.begin(), expected.size(), expected.begin());
    for (std::byte& value : expected) {
        const unsigned int numeric = std::to_integer<unsigned int>(value);
        value = std::byte{static_cast<unsigned char>(numeric + 1U)};
    }
    return expected;
}

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
        throw std::system_error(
            errno,
            std::generic_category(),
            "open crash-test fixture"
        );
    }

    FdGuard fd{raw_fd};
    const auto write_result = asyncdataloader::util::write_all_at(
        fd.get(),
        bytes.data(),
        bytes.size(),
        0
    );
    if (write_result.error_number != 0 ||
        write_result.bytes_written != bytes.size()) {
        throw std::runtime_error("could not write crash-test fixture");
    }

    const auto fsync_result = asyncdataloader::util::fsync_fd(fd.get());
    if (fsync_result.error_number != 0) {
        throw std::system_error(
            fsync_result.error_number,
            std::generic_category(),
            "fsync crash-test fixture"
        );
    }
}

template <std::size_t Size>
bool file_matches(
    const std::filesystem::path& path,
    const std::array<std::byte, Size>& expected
) {
    auto opened = asyncdataloader::util::open_read_only(path.c_str());
    if (opened.error_number != 0) {
        return false;
    }

    std::array<std::byte, Size + 1> actual{};
    const auto read_result = asyncdataloader::util::read_at(
        opened.fd.get(),
        actual.data(),
        actual.size(),
        0
    );
    return read_result.error_number == 0 &&
        read_result.bytes_read == expected.size() &&
        std::equal(expected.begin(), expected.end(), actual.begin());
}

int parse_ready_fd(std::string_view text) {
    int fd = -1;
    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        fd
    );
    if (error != std::errc{} || end != text.data() + text.size() || fd < 0) {
        throw std::invalid_argument("invalid crash-test readiness fd");
    }
    return fd;
}

int run_child_mode(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    int ready_fd
) {
    SyncBackend backend;
    Pipeline processing;
    processing.add_stage(
        std::make_unique<BlockingIncrementStage>(ready_fd)
    );
    PipelineExecutor executor(make_config(), backend, processing);
    [[maybe_unused]] const auto result =
        executor.run_file(input_path, output_path);

    std::cerr << "crash-test child unexpectedly completed the pipeline\n";
    return 70;
}

void wait_for_ready_marker(int ready_fd) {
    struct pollfd descriptor {
        ready_fd,
        POLLIN,
        0,
    };

    int poll_result = -1;
    do {
        poll_result = ::poll(
            &descriptor,
            1,
            static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    kWaitTimeout
                ).count()
            )
        );
    } while (poll_result < 0 && errno == EINTR);

    if (poll_result < 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "poll crash-test readiness pipe"
        );
    }
    if (poll_result == 0) {
        throw std::runtime_error("timed out waiting for crash-test child");
    }

    char marker = '\0';
    ssize_t read_result = -1;
    do {
        read_result = ::read(ready_fd, &marker, 1);
    } while (read_result < 0 && errno == EINTR);

    if (read_result != 1 || marker != 'R') {
        throw std::runtime_error(
            "crash-test child exited before its readiness marker"
        );
    }
}

std::optional<std::filesystem::path> find_partial_temporary_file(
    const std::filesystem::path& directory,
    const std::filesystem::path& final_output_path
) {
    const std::string prefix =
        "." + final_output_path.filename().string() + ".tmp.";

    for (const auto& entry : std::filesystem::directory_iterator{directory}) {
        const std::string filename = entry.path().filename().string();
        if (!filename.starts_with(prefix)) {
            continue;
        }

        std::error_code error;
        const std::uintmax_t size = entry.file_size(error);
        if (!error && size >= kBlockSize) {
            return entry.path();
        }
    }
    return std::nullopt;
}

std::filesystem::path wait_for_partial_temporary_file(
    const std::filesystem::path& directory,
    const std::filesystem::path& final_output_path
) {
    const auto deadline = std::chrono::steady_clock::now() + kWaitTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto path = find_partial_temporary_file(
                directory,
                final_output_path
            ); path.has_value()) {
            return *path;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }

    throw std::runtime_error(
        "timed out waiting for a partially written temporary output"
    );
}

int run_crash_case(bool create_existing_output) {
    TempDirectory directory;
    const auto input_path = directory.child("input.bin");
    const auto output_path = directory.child("output.bin");
    const auto input = make_input();
    const auto expected_partial = expected_first_output_block(input);
    const std::array<std::byte, 7> old_output{
        std::byte{3},
        std::byte{1},
        std::byte{4},
        std::byte{1},
        std::byte{5},
        std::byte{9},
        std::byte{2},
    };

    write_file(input_path, input);
    if (create_existing_output) {
        write_file(output_path, old_output);
    }

    std::array<int, 2> ready_pipe{-1, -1};
    if (::pipe(ready_pipe.data()) != 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "pipe crash-test readiness"
        );
    }
    FdGuard ready_read{ready_pipe[0]};
    FdGuard ready_write{ready_pipe[1]};

    const std::string input_text = input_path.string();
    const std::string output_text = output_path.string();
    const std::string ready_fd_text = std::to_string(ready_write.get());

    const pid_t child_pid = ::fork();
    if (child_pid < 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "fork crash-test child"
        );
    }
    if (child_pid == 0) {
        static_cast<void>(::close(ready_read.get()));
        ::execl(
            "/proc/self/exe",
            "stage13_crash_safety_test",
            "--child",
            input_text.c_str(),
            output_text.c_str(),
            ready_fd_text.c_str(),
            static_cast<char*>(nullptr)
        );
        ::_exit(127);
    }

    ChildProcess child{child_pid};
    ready_write = FdGuard{};
    wait_for_ready_marker(ready_read.get());

    const std::filesystem::path temporary_path =
        wait_for_partial_temporary_file(directory.path(), output_path);
    if (std::filesystem::file_size(temporary_path) != kBlockSize) {
        return fail("more than one block reached the temporary output");
    }
    if (!file_matches(temporary_path, expected_partial)) {
        return fail("temporary output does not contain the processed block");
    }

    if (create_existing_output) {
        if (!file_matches(output_path, old_output)) {
            return fail("existing final output changed before SIGKILL");
        }
    } else if (std::filesystem::exists(output_path)) {
        return fail("final output appeared before commit and SIGKILL");
    }

    const int child_status = child.kill_and_wait();
    if (!WIFSIGNALED(child_status) ||
        WTERMSIG(child_status) != SIGKILL) {
        return fail("child did not terminate because of SIGKILL");
    }

    if (create_existing_output) {
        if (!file_matches(output_path, old_output)) {
            return fail("SIGKILL damaged the existing final output");
        }
    } else if (std::filesystem::exists(output_path)) {
        return fail("SIGKILL exposed a partial final output");
    }

    if (!std::filesystem::exists(temporary_path) ||
        std::filesystem::file_size(temporary_path) != kBlockSize ||
        !file_matches(temporary_path, expected_partial)) {
        return fail("SIGKILL test lost its expected orphan temporary file");
    }
    if (!file_matches(input_path, input)) {
        return fail("crash-safety test changed the input file");
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc == 5 && std::string_view{argv[1]} == "--child") {
            return run_child_mode(argv[2], argv[3], parse_ready_fd(argv[4]));
        }
        if (argc != 1) {
            return fail("unexpected command-line arguments");
        }

        if (const int result = run_crash_case(true); result != 0) {
            return result;
        }
        if (const int result = run_crash_case(false); result != 0) {
            return result;
        }
    } catch (const std::exception& error) {
        std::cerr << "stage13 crash-safety test setup failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
