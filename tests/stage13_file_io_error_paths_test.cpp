#include "util/detail/file_io_operations.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using asyncdataloader::util::detail::FileIOOperations;

struct ScriptStep {
    ssize_t result{0};
    int error_number{0};
};

struct WriteCall {
    int fd{-1};
    const void* buffer{nullptr};
    std::size_t byte_count{0};
    std::uint64_t offset{0};
};

constexpr std::size_t kMaxScriptSteps = 8;
std::array<ScriptStep, kMaxScriptSteps> script_steps{};
std::size_t script_size{0};
std::size_t script_index{0};
std::size_t operation_call_count{0};
std::array<WriteCall, kMaxScriptSteps> write_calls{};
std::size_t write_call_count{0};

constexpr std::array<std::byte, 3> kReadPayload{
    std::byte{'x'},
    std::byte{'y'},
    std::byte{'z'},
};

int fail(std::string_view message) {
    std::cerr << "stage13 file-I/O error-path test failed: "
              << message << '\n';
    return 1;
}

void set_script(std::initializer_list<ScriptStep> steps) {
    if (steps.size() > script_steps.size()) {
        throw std::length_error("file-I/O test script is too large");
    }

    script_size = steps.size();
    script_index = 0;
    operation_call_count = 0;
    write_call_count = 0;
    std::copy(steps.begin(), steps.end(), script_steps.begin());
}

ScriptStep next_step() noexcept {
    if (script_index >= script_size) {
        return ScriptStep{-1, EIO};
    }
    return script_steps[script_index++];
}

int permission_denied_open(const char*) noexcept {
    errno = EACCES;
    return -1;
}

ssize_t scripted_read_at(
    int,
    void* buffer,
    std::size_t byte_count,
    std::uint64_t
) noexcept {
    ++operation_call_count;
    const ScriptStep step = next_step();
    if (step.result < 0) {
        errno = step.error_number;
        return -1;
    }

    const std::size_t result_size = static_cast<std::size_t>(step.result);
    if (result_size > byte_count || result_size > kReadPayload.size()) {
        errno = EIO;
        return -1;
    }

    std::memcpy(buffer, kReadPayload.data(), result_size);
    errno = 0;
    return step.result;
}

ssize_t scripted_write_at(
    int fd,
    const void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) noexcept {
    ++operation_call_count;
    if (write_call_count >= write_calls.size()) {
        errno = EOVERFLOW;
        return -1;
    }
    write_calls[write_call_count++] = WriteCall{
        fd,
        buffer,
        byte_count,
        offset,
    };

    const ScriptStep step = next_step();
    if (step.result < 0) {
        errno = step.error_number;
        return -1;
    }
    if (static_cast<std::size_t>(step.result) > byte_count) {
        errno = EIO;
        return -1;
    }

    errno = 0;
    return step.result;
}

int scripted_fsync(int) noexcept {
    ++operation_call_count;
    const ScriptStep step = next_step();
    if (step.result < 0) {
        errno = step.error_number;
        return -1;
    }

    errno = 0;
    return static_cast<int>(step.result);
}

int test_permission_denied_open_preserves_errno() {
    FileIOOperations operations =
        asyncdataloader::util::detail::system_file_io_operations();
    operations.open_read_only = &permission_denied_open;

    const auto result =
        asyncdataloader::util::detail::open_read_only_with(
            "permission-denied.bin",
            operations
        );
    if (result.fd.valid() || result.error_number != EACCES) {
        return fail("open failure did not preserve EACCES");
    }
    return 0;
}

int test_read_retries_eintr_and_preserves_errors() {
    FileIOOperations operations =
        asyncdataloader::util::detail::system_file_io_operations();
    operations.read_at = &scripted_read_at;

    set_script({ScriptStep{-1, EINTR}, ScriptStep{3, 0}});
    std::array<std::byte, 3> buffer{};
    const auto retried = asyncdataloader::util::detail::read_at_with(
        11,
        buffer.data(),
        buffer.size(),
        40,
        operations
    );
    if (retried.error_number != 0 ||
        retried.bytes_read != buffer.size() ||
        operation_call_count != 2 || buffer != kReadPayload) {
        return fail("read_at did not retry EINTR and return the read bytes");
    }

    set_script({ScriptStep{-1, EACCES}});
    const auto denied = asyncdataloader::util::detail::read_at_with(
        11,
        buffer.data(),
        buffer.size(),
        40,
        operations
    );
    if (denied.bytes_read != 0 || denied.error_number != EACCES ||
        operation_call_count != 1) {
        return fail("read_at did not preserve a non-EINTR error");
    }
    return 0;
}

int test_write_retries_eintr_and_completes_short_writes() {
    FileIOOperations operations =
        asyncdataloader::util::detail::system_file_io_operations();
    operations.write_at = &scripted_write_at;

    const std::array<std::byte, 5> payload{
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
        std::byte{5},
    };
    set_script({
        ScriptStep{-1, EINTR},
        ScriptStep{2, 0},
        ScriptStep{3, 0},
    });

    const auto result =
        asyncdataloader::util::detail::write_all_at_with(
            17,
            payload.data(),
            payload.size(),
            100,
            operations
        );
    if (result.error_number != 0 ||
        result.bytes_written != payload.size() || write_call_count != 3) {
        return fail("write_all_at did not finish the scripted write");
    }

    const bool first_call_is_original =
        write_calls[0].fd == 17 &&
        write_calls[0].buffer == payload.data() &&
        write_calls[0].byte_count == 5 && write_calls[0].offset == 100;
    const bool eintr_retry_is_identical =
        write_calls[1].fd == 17 &&
        write_calls[1].buffer == payload.data() &&
        write_calls[1].byte_count == 5 && write_calls[1].offset == 100;
    const bool short_write_advances_suffix =
        write_calls[2].fd == 17 &&
        write_calls[2].buffer == payload.data() + 2 &&
        write_calls[2].byte_count == 3 && write_calls[2].offset == 102;
    if (!first_call_is_original || !eintr_retry_is_identical ||
        !short_write_advances_suffix) {
        return fail("write retry advanced the pointer or offset incorrectly");
    }
    return 0;
}

int test_write_reports_partial_progress_and_zero_write() {
    FileIOOperations operations =
        asyncdataloader::util::detail::system_file_io_operations();
    operations.write_at = &scripted_write_at;

    const std::array<std::byte, 5> payload{};
    set_script({ScriptStep{2, 0}, ScriptStep{-1, ENOSPC}});
    const auto partial =
        asyncdataloader::util::detail::write_all_at_with(
            19,
            payload.data(),
            payload.size(),
            200,
            operations
        );
    if (partial.bytes_written != 2 || partial.error_number != ENOSPC ||
        write_call_count != 2 || write_calls[1].offset != 202 ||
        write_calls[1].buffer != payload.data() + 2) {
        return fail("partial write failure lost progress or errno");
    }

    set_script({ScriptStep{0, 0}});
    const auto zero = asyncdataloader::util::detail::write_all_at_with(
        19,
        payload.data(),
        payload.size(),
        200,
        operations
    );
    if (zero.bytes_written != 0 || zero.error_number != EIO ||
        write_call_count != 1) {
        return fail("zero-byte pwrite did not become EIO");
    }
    return 0;
}

int test_fsync_retries_eintr_and_preserves_errors() {
    FileIOOperations operations =
        asyncdataloader::util::detail::system_file_io_operations();
    operations.fsync = &scripted_fsync;

    set_script({ScriptStep{-1, EINTR}, ScriptStep{0, 0}});
    const auto retried =
        asyncdataloader::util::detail::fsync_fd_with(23, operations);
    if (retried.error_number != 0 || operation_call_count != 2) {
        return fail("fsync did not retry EINTR");
    }

    set_script({ScriptStep{-1, EROFS}});
    const auto failed =
        asyncdataloader::util::detail::fsync_fd_with(23, operations);
    if (failed.error_number != EROFS || operation_call_count != 1) {
        return fail("fsync did not preserve the final error");
    }
    return 0;
}

}  // namespace

int main() {
    try {
        if (const int result = test_permission_denied_open_preserves_errno();
            result != 0) {
            return result;
        }
        if (const int result = test_read_retries_eintr_and_preserves_errors();
            result != 0) {
            return result;
        }
        if (const int result =
                test_write_retries_eintr_and_completes_short_writes();
            result != 0) {
            return result;
        }
        if (const int result =
                test_write_reports_partial_progress_and_zero_write();
            result != 0) {
            return result;
        }
        if (const int result =
                test_fsync_retries_eintr_and_preserves_errors();
            result != 0) {
            return result;
        }
    } catch (const std::exception& error) {
        std::cerr << "stage13 file-I/O error-path test setup failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
