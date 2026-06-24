#pragma once

#include "util/fd_guard.h"

#include <cstddef>
#include <cstdint>

namespace asyncdataloader::util {

struct OpenFileResult {
    FdGuard fd;
    int error_number{0};
};

struct ReadAtResult {
    std::size_t bytes_read{0};
    int error_number{0};
};

struct WriteAtResult {
    std::size_t bytes_written{0};
    int error_number{0};
};

struct FsyncResult {
    int error_number{0};
};

[[nodiscard]] OpenFileResult open_read_only(const char* path) noexcept;
[[nodiscard]] ReadAtResult read_at(int fd, void* buffer, std::size_t byte_count, std::uint64_t offset) noexcept;
[[nodiscard]] WriteAtResult write_all_at(
    int fd,
    const void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) noexcept;
[[nodiscard]] FsyncResult fsync_fd(int fd) noexcept;

}  // namespace asyncdataloader::util
