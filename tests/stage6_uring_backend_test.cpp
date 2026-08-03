#include "backend/uring_backend.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage6 UringBackend test failed: " << message << '\n';
    return 1;
}

template <typename T>
int wait_for_completion(
    asyncdataloader::backend::IOBackend& backend,
    asyncdataloader::coroutine::Task<T>& task
) {
    task.start();
    if (task.done()) {
        return fail("a submitted io_uring read should suspend until wait_one()");
    }

    backend.wait_one();
    if (!task.done()) {
        return fail("wait_one() should resume the completed read Task");
    }

    return 0;
}

}  // namespace

int main() {
    try {
        asyncdataloader::backend::UringBackend invalid_backend{0};
        return fail("queue depth zero should be rejected");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("queue depth zero should throw std::invalid_argument");
    }

    char path[] = "/tmp/asyncdataloader_uring_backend_test_XXXXXX";
    const int raw_fd = ::mkstemp(path);
    if (raw_fd < 0) {
        return fail("mkstemp failed");
    }

    asyncdataloader::util::FdGuard fd{raw_fd};
    ::unlink(path);

    constexpr char kContents[] = "abcdef";
    const auto write_result = asyncdataloader::util::write_all_at(
        fd.get(),
        kContents,
        sizeof(kContents) - 1,
        0
    );
    if (write_result.error_number != 0) {
        return fail("test setup could not write the input bytes");
    }

    asyncdataloader::backend::UringBackend concrete_backend{8};
    asyncdataloader::backend::IOBackend& backend = concrete_backend;

    if (backend.name() != "io_uring") {
        return fail("name() should identify the io_uring backend");
    }

    std::array<std::byte, 4> full_buffer{};
    auto full_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{full_buffer},
        std::uint64_t{2}
    );
    if (wait_for_completion(backend, full_read) != 0) {
        return 1;
    }
    if (full_read.result() != full_buffer.size()) {
        return fail("read_at() should report the requested byte count");
    }
    if (std::memcmp(full_buffer.data(), "cdef", full_buffer.size()) != 0) {
        return fail("read_at() should read bytes from the requested offset");
    }

    std::array<std::byte, 4> short_buffer{};
    auto short_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{short_buffer},
        std::uint64_t{5}
    );
    if (wait_for_completion(backend, short_read) != 0) {
        return 1;
    }
    if (short_read.result() != 1) {
        return fail("a read near EOF should report a successful short read");
    }
    if (std::memcmp(short_buffer.data(), "f", 1) != 0) {
        return fail("a short read should preserve the byte before EOF");
    }

    std::array<std::byte, 1> eof_buffer{};
    auto eof_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{eof_buffer},
        std::uint64_t{sizeof(kContents) - 1}
    );
    if (wait_for_completion(backend, eof_read) != 0) {
        return 1;
    }
    if (eof_read.result() != 0) {
        return fail("a read starting at EOF should return zero bytes");
    }

    const int closed_fd = ::dup(fd.get());
    if (closed_fd < 0) {
        return fail("dup failed while preparing the error-path test");
    }
    ::close(closed_fd);

    std::array<std::byte, 1> error_buffer{};
    auto failed_read = backend.read_at(
        closed_fd,
        std::span<std::byte>{error_buffer},
        std::uint64_t{0}
    );
    if (wait_for_completion(backend, failed_read) != 0) {
        return 1;
    }

    try {
        static_cast<void>(failed_read.result());
        return fail("a closed descriptor should fail the read operation");
    } catch (const std::system_error& error) {
        if (error.code().value() != EBADF) {
            return fail("the CQE error should preserve EBADF");
        }
    } catch (...) {
        return fail("a negative CQE result should become std::system_error");
    }

    return 0;
}
