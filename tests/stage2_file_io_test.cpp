#include "util/file_io.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <unistd.h>

namespace {

int fail(const char* message) {
    std::cerr << "stage2_file_io_test: " << message << '\n';
    return 1;
}

int create_temp_file(char* path, std::size_t path_size) {
    if (path_size < sizeof("/tmp/asyncdataloader_file_io_test_XXXXXX")) {
        return -1;
    }

    std::strcpy(path, "/tmp/asyncdataloader_file_io_test_XXXXXX");
    const int fd = ::mkstemp(path);
    if (fd < 0) {
        std::cerr << "mkstemp failed: " << std::strerror(errno) << '\n';
        return -1;
    }

    return fd;
}

int test_open_read_only_success() {
    char path[64]{};
    const int temp_fd = create_temp_file(path, sizeof(path));
    if (temp_fd < 0) {
        return 1;
    }
    ::close(temp_fd);

    const asyncdataloader::util::OpenFileResult result =
        asyncdataloader::util::open_read_only(path);
    ::unlink(path);

    if (!result.fd.valid()) {
        return fail("open_read_only should return a valid FdGuard for an existing file");
    }
    if (result.error_number != 0) {
        return fail("open_read_only should report error_number 0 on success");
    }

    return 0;
}

int test_open_read_only_missing_file_reports_errno() {
    const asyncdataloader::util::OpenFileResult result =
        asyncdataloader::util::open_read_only("/tmp/asyncdataloader_missing_input_file");

    if (result.fd.valid()) {
        return fail("open_read_only should not return a valid fd for a missing file");
    }
    if (result.error_number != ENOENT) {
        return fail("open_read_only should preserve errno for a missing file");
    }

    return 0;
}

int test_read_at_reads_from_offset() {
    char path[64]{};
    const int temp_fd = create_temp_file(path, sizeof(path));
    if (temp_fd < 0) {
        return 1;
    }

    constexpr char contents[] = "abcdef";
    if (::write(temp_fd, contents, sizeof(contents) - 1) != static_cast<ssize_t>(sizeof(contents) - 1)) {
        ::close(temp_fd);
        ::unlink(path);
        return fail("failed to write test contents");
    }
    ::close(temp_fd);

    const asyncdataloader::util::OpenFileResult open_result =
        asyncdataloader::util::open_read_only(path);
    ::unlink(path);
    if (!open_result.fd.valid()) {
        return fail("test setup should open the input file");
    }

    std::array<char, 3> buffer{};
    const asyncdataloader::util::ReadAtResult read_result =
        asyncdataloader::util::read_at(open_result.fd.get(), buffer.data(), buffer.size(), 2);

    if (read_result.error_number != 0) {
        return fail("read_at should report error_number 0 on success");
    }
    if (read_result.bytes_read != buffer.size()) {
        return fail("read_at should read the requested byte count when enough data exists");
    }
    if (std::memcmp(buffer.data(), "cde", buffer.size()) != 0) {
        return fail("read_at should read from the requested file offset");
    }

    return 0;
}

int test_read_at_reports_short_read_at_eof() {
    char path[64]{};
    const int temp_fd = create_temp_file(path, sizeof(path));
    if (temp_fd < 0) {
        return 1;
    }

    constexpr char contents[] = "abcdef";
    if (::write(temp_fd, contents, sizeof(contents) - 1) != static_cast<ssize_t>(sizeof(contents) - 1)) {
        ::close(temp_fd);
        ::unlink(path);
        return fail("failed to write test contents");
    }
    ::close(temp_fd);

    const asyncdataloader::util::OpenFileResult open_result =
        asyncdataloader::util::open_read_only(path);
    ::unlink(path);
    if (!open_result.fd.valid()) {
        return fail("test setup should open the input file");
    }

    std::array<char, 4> buffer{};
    const asyncdataloader::util::ReadAtResult read_result =
        asyncdataloader::util::read_at(open_result.fd.get(), buffer.data(), buffer.size(), 4);

    if (read_result.error_number != 0) {
        return fail("short read at EOF should not be reported as an error");
    }
    if (read_result.bytes_read != 2) {
        return fail("read_at should return the actual short byte count near EOF");
    }
    if (std::memcmp(buffer.data(), "ef", read_result.bytes_read) != 0) {
        return fail("read_at should preserve bytes read before EOF");
    }

    return 0;
}

int test_write_all_at_writes_to_offset() {
    char path[64]{};
    const int temp_fd = create_temp_file(path, sizeof(path));
    if (temp_fd < 0) {
        return 1;
    }

    constexpr char initial[] = "abcdef";
    if (::write(temp_fd, initial, sizeof(initial) - 1) != static_cast<ssize_t>(sizeof(initial) - 1)) {
        ::close(temp_fd);
        ::unlink(path);
        return fail("failed to write initial contents");
    }

    constexpr char patch[] = "XYZ";
    const asyncdataloader::util::WriteAtResult write_result =
        asyncdataloader::util::write_all_at(temp_fd, patch, sizeof(patch) - 1, 2);
    ::close(temp_fd);

    if (write_result.error_number != 0) {
        ::unlink(path);
        return fail("write_all_at should report error_number 0 on success");
    }
    if (write_result.bytes_written != sizeof(patch) - 1) {
        ::unlink(path);
        return fail("write_all_at should report the full byte count written");
    }

    const asyncdataloader::util::OpenFileResult open_result =
        asyncdataloader::util::open_read_only(path);
    ::unlink(path);
    if (!open_result.fd.valid()) {
        return fail("test setup should reopen the output file");
    }

    std::array<char, 6> buffer{};
    const asyncdataloader::util::ReadAtResult read_result =
        asyncdataloader::util::read_at(open_result.fd.get(), buffer.data(), buffer.size(), 0);

    if (read_result.error_number != 0) {
        return fail("read_at should verify written contents without an error");
    }
    if (read_result.bytes_read != buffer.size()) {
        return fail("read_at should verify all bytes in the test file");
    }
    if (std::memcmp(buffer.data(), "abXYZf", buffer.size()) != 0) {
        return fail("write_all_at should write bytes at the requested offset");
    }

    return 0;
}

int test_fsync_fd_success_after_write() {
    char path[64]{};
    const int temp_fd = create_temp_file(path, sizeof(path));
    if (temp_fd < 0) {
        return 1;
    }

    constexpr char contents[] = "persist";
    const asyncdataloader::util::WriteAtResult write_result =
        asyncdataloader::util::write_all_at(temp_fd, contents, sizeof(contents) - 1, 0);
    if (write_result.error_number != 0) {
        ::close(temp_fd);
        ::unlink(path);
        return fail("test setup should write contents before fsync");
    }

    const asyncdataloader::util::FsyncResult fsync_result =
        asyncdataloader::util::fsync_fd(temp_fd);
    ::close(temp_fd);
    ::unlink(path);

    if (fsync_result.error_number != 0) {
        return fail("fsync_fd should report error_number 0 after syncing a writable file");
    }

    return 0;
}

int test_fsync_fd_invalid_fd_reports_errno() {
    const asyncdataloader::util::FsyncResult fsync_result =
        asyncdataloader::util::fsync_fd(-1);

    if (fsync_result.error_number != EBADF) {
        return fail("fsync_fd should preserve errno for an invalid file descriptor");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int result = test_open_read_only_success(); result != 0) {
        return result;
    }

    if (const int result = test_open_read_only_missing_file_reports_errno(); result != 0) {
        return result;
    }

    if (const int result = test_read_at_reads_from_offset(); result != 0) {
        return result;
    }

    if (const int result = test_read_at_reports_short_read_at_eof(); result != 0) {
        return result;
    }

    if (const int result = test_write_all_at_writes_to_offset(); result != 0) {
        return result;
    }

    if (const int result = test_fsync_fd_success_after_write(); result != 0) {
        return result;
    }

    if (const int result = test_fsync_fd_invalid_fd_reports_errno(); result != 0) {
        return result;
    }

    return 0;
}
