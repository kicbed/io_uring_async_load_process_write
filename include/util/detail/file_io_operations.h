#pragma once

#include "util/file_io.h"

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace asyncdataloader::util::detail {

// Internal dependency-injection seam for deterministic syscall error tests.
// Production callers continue to use the public functions in file_io.h.
struct FileIOOperations {
    using OpenReadOnly = int (*)(const char*) noexcept;
    using ReadAt = ssize_t (*)(
        int,
        void*,
        std::size_t,
        std::uint64_t
    ) noexcept;
    using WriteAt = ssize_t (*)(
        int,
        const void*,
        std::size_t,
        std::uint64_t
    ) noexcept;
    using Fsync = int (*)(int) noexcept;

    OpenReadOnly open_read_only{nullptr};
    ReadAt read_at{nullptr};
    WriteAt write_at{nullptr};
    Fsync fsync{nullptr};
};

[[nodiscard]] FileIOOperations system_file_io_operations() noexcept;

[[nodiscard]] OpenFileResult open_read_only_with(
    const char* path,
    const FileIOOperations& operations
) noexcept;

[[nodiscard]] ReadAtResult read_at_with(
    int fd,
    void* buffer,
    std::size_t byte_count,
    std::uint64_t offset,
    const FileIOOperations& operations
) noexcept;

[[nodiscard]] WriteAtResult write_all_at_with(
    int fd,
    const void* buffer,
    std::size_t byte_count,
    std::uint64_t offset,
    const FileIOOperations& operations
) noexcept;

[[nodiscard]] FsyncResult fsync_fd_with(
    int fd,
    const FileIOOperations& operations
) noexcept;

}  // namespace asyncdataloader::util::detail
