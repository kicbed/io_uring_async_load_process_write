#include "backend/thread_pool_backend.h"
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
    std::cerr << "stage6 ThreadPoolBackend test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    try {
        asyncdataloader::backend::ThreadPoolBackend invalid_workers{0, 1};
        return fail("worker count zero should be rejected");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("worker count zero should throw std::invalid_argument");
    }

    try {
        asyncdataloader::backend::ThreadPoolBackend invalid_capacity{1, 0};
        return fail("max_inflight zero should be rejected");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("max_inflight zero should throw std::invalid_argument");
    }

    char path[] = "/tmp/asyncdataloader_thread_pool_test_XXXXXX";
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

    asyncdataloader::backend::ThreadPoolBackend concrete_backend{2, 2};
    asyncdataloader::backend::IOBackend& backend = concrete_backend;

    if (backend.name() != "thread_pool") {
        return fail("name() should identify the thread-pool backend");
    }

    std::array<std::byte, 4> full_buffer{};
    auto full_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{full_buffer},
        std::uint64_t{2}
    );
    full_read.start();
    if (full_read.done()) {
        return fail("a worker read should suspend until wait_one()");
    }
    backend.wait_one();
    if (!full_read.done() || full_read.result() != full_buffer.size()) {
        return fail("wait_one() should complete the worker read");
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
    short_read.start();
    backend.wait_one();
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
    eof_read.start();
    backend.wait_one();
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
    failed_read.start();
    backend.wait_one();

    try {
        static_cast<void>(failed_read.result());
        return fail("a closed descriptor should fail the worker read");
    } catch (const std::system_error& error) {
        if (error.code().value() != EBADF) {
            return fail("the worker error should preserve EBADF");
        }
    } catch (...) {
        return fail("a worker read error should become std::system_error");
    }

    std::array<std::byte, 1> first_buffer{};
    std::array<std::byte, 1> second_buffer{};
    std::array<std::byte, 1> overflow_buffer{};

    auto first_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{first_buffer},
        std::uint64_t{0}
    );
    auto second_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{second_buffer},
        std::uint64_t{1}
    );
    auto overflow_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{overflow_buffer},
        std::uint64_t{2}
    );

    first_read.start();
    second_read.start();
    overflow_read.start();

    if (!overflow_read.done()) {
        return fail("a request beyond max_inflight should fail immediately");
    }
    try {
        static_cast<void>(overflow_read.result());
        return fail("a request beyond max_inflight should report EAGAIN");
    } catch (const std::system_error& error) {
        if (error.code().value() != EAGAIN) {
            return fail("bounded submission should preserve EAGAIN");
        }
    } catch (...) {
        return fail("bounded submission should use std::system_error");
    }

    backend.wait_one();
    backend.wait_one();

    if (!first_read.done() || !second_read.done()) {
        return fail("both accepted requests should eventually complete");
    }
    if (first_read.result() != 1 || second_read.result() != 1) {
        return fail("accepted requests should return their byte counts");
    }
    if (std::memcmp(first_buffer.data(), "a", 1) != 0 ||
        std::memcmp(second_buffer.data(), "b", 1) != 0) {
        return fail("accepted requests should preserve their offsets");
    }

    return 0;
}
