#include "util/file_io.h"
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace asyncdataloader::util {

OpenFileResult open_read_only(const char* path) noexcept {
    int fd = -1;
    fd = ::open(path, O_RDONLY);
    if (fd < 0){
        int error_number = errno;
        return OpenFileResult{FdGuard{}, error_number};
    }

    return OpenFileResult{FdGuard{fd}, 0};
}

ReadAtResult read_at(int fd, void* buffer, std::size_t byte_count, std::uint64_t offset) noexcept {
    ssize_t bytes_read = ::pread(fd, buffer, byte_count, static_cast<off_t>(offset));
    while (bytes_read < 0 && errno == EINTR) {
        bytes_read = ::pread(fd, buffer, byte_count, static_cast<off_t>(offset));
    }
    if (bytes_read < 0){
        int error_number = errno;
        return ReadAtResult{0, error_number};
    }
    else{
        return ReadAtResult{static_cast<std::size_t>(bytes_read), 0};
    }
}

WriteAtResult write_all_at(
    int fd,
    const void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) noexcept {
    const auto* bytes = static_cast<const char*>(buffer);
    std::size_t written_count = 0;
    while (written_count < byte_count){
        ssize_t bytes_written = ::pwrite(fd, bytes + written_count, byte_count - written_count, static_cast<off_t>(offset + written_count));

        while (bytes_written < 0 && errno == EINTR){
            bytes_written = ::pwrite(fd, bytes + written_count, byte_count - written_count, static_cast<off_t>(offset + written_count));
        }

        if (bytes_written < 0){
            int error_number = errno;
            return WriteAtResult{written_count, error_number};
        }
        if (bytes_written == 0){
            return {written_count, EIO};
        }
        
        written_count += static_cast<std::size_t>(bytes_written);
    }


    return WriteAtResult{written_count, 0};
}

FsyncResult fsync_fd(int fd) noexcept {
    int sync_result = ::fsync(fd);
    while (sync_result < 0 && errno == EINTR){
        sync_result = ::fsync(fd);
    }
    if (sync_result < 0){
        int error_number = errno;
        return FsyncResult{error_number};
    }

    return FsyncResult{0};
}

}  // namespace asyncdataloader::util
