#include "backend/sync_backend.h"
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
    std::cerr << "stage6 SyncBackend test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    char path[] = "/tmp/asyncdataloader_sync_backend_test_XXXXXX";
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

    asyncdataloader::backend::SyncBackend concrete_backend;
    asyncdataloader::backend::IOBackend& backend = concrete_backend;

    if (backend.name() != "sync") {
        return fail("name() should identify the sync backend");
    }

    std::array<std::byte, 4> full_buffer{};
    auto full_read = backend.read_at(
        fd.get(),
        std::span<std::byte>{full_buffer},
        std::uint64_t{2}
    );

    if (full_read.done()) {
        return fail("read_at() should remain lazy until Task::start()");
    }

    full_read.start();
    if (!full_read.done()) {
        return fail("a synchronous read should complete inside Task::start()");
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
    short_read.start();
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
    if (eof_read.result() != 0) {
        return fail("a read starting at EOF should return zero bytes");
    }

    std::array<std::byte, 1> error_buffer{};
    auto failed_read = backend.read_at(
        -1,
        std::span<std::byte>{error_buffer},
        std::uint64_t{0}
    );

    try {
        failed_read.start();
    } catch (...) {
        return fail("Task::start() should store, not expose, operation errors");
    }

    try {
        static_cast<void>(failed_read.result());
        return fail("Task::result() should rethrow the read error");
    } catch (const std::system_error& error) {
        if (error.code().value() != EBADF) {
            return fail("read error should preserve EBADF");
        }
    } catch (...) {
        return fail("read failure should use std::system_error");
    }

    try {
        backend.wait_one();
        return fail("wait_one() should reject use on SyncBackend");
    } catch (const std::logic_error&) {
    } catch (...) {
        return fail("wait_one() should throw std::logic_error");
    }

    return 0;
}
