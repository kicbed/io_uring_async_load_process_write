#include "util/fd_guard.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>
#include <unistd.h>

namespace {

int fail(const char* message) {
    std::cerr << "stage2_fd_guard_test: " << message << '\n';
    return 1;
}

int create_temp_fd() {
    char path[] = "/tmp/asyncdataloader_fd_guard_test_XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0) {
        std::cerr << "mkstemp failed: " << std::strerror(errno) << '\n';
        return -1;
    }

    ::unlink(path);
    return fd;
}

bool is_closed(int fd) {
    errno = 0;
    const ssize_t written = ::write(fd, "x", 1);
    return written == -1 && errno == EBADF;
}

int test_destructor_closes_owned_fd() {
    const int fd = create_temp_fd();
    if (fd < 0) {
        return 1;
    }

    {
        asyncdataloader::util::FdGuard guard(fd);
        if (!guard.valid()) {
            return fail("guard should report a valid descriptor after construction");
        }
        if (guard.get() != fd) {
            return fail("guard should return the descriptor it owns");
        }
    }

    if (!is_closed(fd)) {
        return fail("descriptor should be closed when FdGuard is destroyed");
    }

    return 0;
}

int test_move_constructor_transfers_ownership() {
    const int fd = create_temp_fd();
    if (fd < 0) {
        return 1;
    }

    {
        asyncdataloader::util::FdGuard source(fd);
        asyncdataloader::util::FdGuard moved(std::move(source));

        if (source.valid()) {
            return fail("moved-from guard should no longer own a descriptor");
        }
        if (!moved.valid()) {
            return fail("moved-to guard should own the descriptor");
        }
        if (moved.get() != fd) {
            return fail("moved-to guard should keep the original descriptor value");
        }
    }

    if (!is_closed(fd)) {
        return fail("descriptor should be closed by the moved-to guard");
    }

    return 0;
}

int test_move_assignment_replaces_owned_descriptor() {
    const int old_fd = create_temp_fd();
    if (old_fd < 0) {
        return 1;
    }

    const int new_fd = create_temp_fd();
    if (new_fd < 0) {
        ::close(old_fd);
        return 1;
    }

    {
        asyncdataloader::util::FdGuard target(old_fd);
        asyncdataloader::util::FdGuard source(new_fd);

        target = std::move(source);

        if (source.valid()) {
            return fail("move-assigned source should no longer own a descriptor");
        }
        if (!target.valid()) {
            return fail("move-assigned target should own a descriptor");
        }
        if (target.get() != new_fd) {
            return fail("move-assigned target should own the source descriptor");
        }
        if (!is_closed(old_fd)) {
            return fail("move assignment should close the target's old descriptor");
        }
    }

    if (!is_closed(new_fd)) {
        return fail("move-assigned target should close the new descriptor on destruction");
    }

    return 0;
}

}  // namespace

int main() {
    if (const int result = test_destructor_closes_owned_fd(); result != 0) {
        return result;
    }

    if (const int result = test_move_constructor_transfers_ownership(); result != 0) {
        return result;
    }

    if (const int result = test_move_assignment_replaces_owned_descriptor(); result != 0) {
        return result;
    }

    return 0;
}
