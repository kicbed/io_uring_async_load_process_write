#include "buffer/aligned_buffer.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace {

using asyncdataloader::buffer::AlignedBuffer;
using asyncdataloader::util::FdGuard;

constexpr int kSkipReturnCode = 77;
constexpr std::size_t kAlignment = 4096;
constexpr std::size_t kFileSize = 2 * kAlignment;

int fail(std::string_view message) {
    std::cerr << "stage13 O_DIRECT contract test failed: "
              << message << '\n';
    return 1;
}

int skip(std::string_view reason) {
    std::cout << "stage13 O_DIRECT contract test skipped: "
              << reason << '\n';
    return kSkipReturnCode;
}

class TemporaryFile {
public:
    TemporaryFile() {
        char path_template[] =
            "/tmp/asyncdataloader_stage13_odirect_XXXXXX";
        const int raw_fd = ::mkstemp(path_template);
        if (raw_fd < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "mkstemp O_DIRECT fixture"
            );
        }
        path_ = path_template;
        fd_ = FdGuard{raw_fd};
    }

    ~TemporaryFile() noexcept {
        if (!path_.empty()) {
            static_cast<void>(::unlink(path_.c_str()));
        }
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    [[nodiscard]] int fd() const noexcept {
        return fd_.get();
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

    void close() noexcept {
        fd_ = FdGuard{};
    }

private:
    std::filesystem::path path_;
    FdGuard fd_;
};

std::array<std::byte, kFileSize> make_fixture_data() {
    std::array<std::byte, kFileSize> data{};
    for (std::size_t index = 0; index < data.size(); ++index) {
        data[index] = std::byte{static_cast<unsigned char>(index % 251U)};
    }
    return data;
}

void prepare_fixture(
    TemporaryFile& file,
    std::span<const std::byte> data
) {
    const auto write_result = asyncdataloader::util::write_all_at(
        file.fd(),
        data.data(),
        data.size(),
        0
    );
    if (write_result.error_number != 0 ||
        write_result.bytes_written != data.size()) {
        throw std::runtime_error("could not write O_DIRECT fixture");
    }

    const auto fsync_result = asyncdataloader::util::fsync_fd(file.fd());
    if (fsync_result.error_number != 0) {
        throw std::system_error(
            fsync_result.error_number,
            std::generic_category(),
            "fsync O_DIRECT fixture"
        );
    }
    file.close();
}

int expect_einval(
    int fd,
    void* buffer,
    std::size_t byte_count,
    off_t offset,
    std::string_view dimension
) {
    errno = 0;
    const ssize_t result = ::pread(fd, buffer, byte_count, offset);
    if (result < 0 && errno == EINVAL) {
        return 0;
    }
    if (result >= 0) {
        return skip(
            std::string{"filesystem did not reject misaligned "} +
            std::string{dimension}
        );
    }

    std::cerr << "stage13 O_DIRECT contract test failed: misaligned "
              << dimension << " returned "
              << std::generic_category().message(errno) << '\n';
    return 1;
}

}  // namespace

int main() {
#ifndef O_DIRECT
    return skip("O_DIRECT is not available in this build environment");
#else
    try {
        TemporaryFile fixture;
        const auto expected = make_fixture_data();
        prepare_fixture(fixture, expected);

        const int raw_direct_fd = ::open(
            fixture.path().c_str(),
            O_RDONLY | O_DIRECT | O_CLOEXEC
        );
        if (raw_direct_fd < 0) {
            if (errno == EINVAL || errno == EOPNOTSUPP) {
                return skip("the temporary filesystem rejected O_DIRECT");
            }
            throw std::system_error(
                errno,
                std::generic_category(),
                "open O_DIRECT fixture"
            );
        }
        FdGuard direct_fd{raw_direct_fd};

        AlignedBuffer buffer{kFileSize, kAlignment};
        const auto address = reinterpret_cast<std::uintptr_t>(buffer.data());
        if (address % kAlignment != 0) {
            return fail("AlignedBuffer address is not 4096-byte aligned");
        }

        errno = 0;
        const ssize_t aligned_result = ::pread(
            direct_fd.get(),
            buffer.data(),
            kAlignment,
            0
        );
        if (aligned_result < 0 &&
            (errno == EINVAL || errno == EOPNOTSUPP)) {
            return skip(
                "the filesystem has a different direct-I/O alignment contract"
            );
        }
        if (aligned_result != static_cast<ssize_t>(kAlignment)) {
            return fail("aligned O_DIRECT read did not return one full block");
        }
        if (!std::equal(
                expected.begin(),
                expected.begin() + static_cast<std::ptrdiff_t>(kAlignment),
                buffer.bytes().begin()
            )) {
            return fail("aligned O_DIRECT read returned incorrect bytes");
        }

        if (const int result = expect_einval(
                direct_fd.get(),
                buffer.data() + 1,
                kAlignment,
                0,
                "buffer address"
            ); result != 0) {
            return result;
        }
        if (const int result = expect_einval(
                direct_fd.get(),
                buffer.data(),
                kAlignment - 1,
                0,
                "length"
            ); result != 0) {
            return result;
        }
        if (const int result = expect_einval(
                direct_fd.get(),
                buffer.data(),
                kAlignment,
                1,
                "file offset"
            ); result != 0) {
            return result;
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "stage13 O_DIRECT contract test setup failed: "
                  << error.what() << '\n';
        return 1;
    }
#endif
}
