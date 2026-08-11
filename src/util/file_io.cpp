#include "util/file_io.h"

#include "util/detail/file_io_operations.h"

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace asyncdataloader::util {
namespace {

int system_open_read_only(const char* path) noexcept {
    return ::open(path, O_RDONLY);
}

ssize_t system_read_at(
    int fd,
    void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) noexcept {
    return ::pread(fd, buffer, byte_count, static_cast<off_t>(offset));
}

ssize_t system_write_at(
    int fd,
    const void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) noexcept {
    return ::pwrite(fd, buffer, byte_count, static_cast<off_t>(offset));
}

int system_fsync(int fd) noexcept {
    return ::fsync(fd);
}

}  // namespace

namespace detail {

FileIOOperations system_file_io_operations() noexcept {
    return FileIOOperations{
        &system_open_read_only,
        &system_read_at,
        &system_write_at,
        &system_fsync,
    };
}

OpenFileResult open_read_only_with(
    const char* path,
    const FileIOOperations& operations
) noexcept {
    const int fd = operations.open_read_only(path);
    if (fd < 0) {
        const int error_number = errno;
        return OpenFileResult{FdGuard{}, error_number};
    }

    return OpenFileResult{FdGuard{fd}, 0};
}

ReadAtResult read_at_with(
    int fd,
    void* buffer,
    std::size_t byte_count,
    std::uint64_t offset,
    const FileIOOperations& operations
) noexcept {
    ssize_t bytes_read = operations.read_at(
        fd,
        buffer,
        byte_count,
        offset
    );
    while (bytes_read < 0 && errno == EINTR) {
        bytes_read = operations.read_at(fd, buffer, byte_count, offset);
    }
    if (bytes_read < 0) {
        const int error_number = errno;
        return ReadAtResult{0, error_number};
    }

    return ReadAtResult{static_cast<std::size_t>(bytes_read), 0};
}

WriteAtResult write_all_at_with(
    int fd,
    const void* buffer,
    std::size_t byte_count,
    std::uint64_t offset,
    const FileIOOperations& operations
) noexcept {
    const auto* bytes = static_cast<const char*>(buffer);
    std::size_t written_count = 0;

    while (written_count < byte_count) {
        const std::size_t remaining = byte_count - written_count;
        const std::uint64_t current_offset =
            offset + static_cast<std::uint64_t>(written_count);
        ssize_t bytes_written = operations.write_at(
            fd,
            bytes + written_count,
            remaining,
            current_offset
        );

        while (bytes_written < 0 && errno == EINTR) {
            bytes_written = operations.write_at(
                fd,
                bytes + written_count,
                remaining,
                current_offset
            );
        }

        if (bytes_written < 0) {
            const int error_number = errno;
            return WriteAtResult{written_count, error_number};
        }
        if (bytes_written == 0) {
            return WriteAtResult{written_count, EIO};
        }

        written_count += static_cast<std::size_t>(bytes_written);
    }

    return WriteAtResult{written_count, 0};
}

FsyncResult fsync_fd_with(
    int fd,
    const FileIOOperations& operations
) noexcept {
    int sync_result = operations.fsync(fd);
    while (sync_result < 0 && errno == EINTR) {
        sync_result = operations.fsync(fd);
    }
    if (sync_result < 0) {
        const int error_number = errno;
        return FsyncResult{error_number};
    }

    return FsyncResult{0};
}

}  // namespace detail

OpenFileResult open_read_only(const char* path) noexcept {
    return detail::open_read_only_with(
        path,
        detail::system_file_io_operations()
    );
}

ReadAtResult read_at(
    int fd,
    void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) noexcept {
    return detail::read_at_with(
        fd,
        buffer,
        byte_count,
        offset,
        detail::system_file_io_operations()
    );
}

WriteAtResult write_all_at(
    int fd,
    const void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) noexcept {
    return detail::write_all_at_with(
        fd,
        buffer,
        byte_count,
        offset,
        detail::system_file_io_operations()
    );
}

FsyncResult fsync_fd(int fd) noexcept {
    return detail::fsync_fd_with(
        fd,
        detail::system_file_io_operations()
    );
}

}  // namespace asyncdataloader::util
