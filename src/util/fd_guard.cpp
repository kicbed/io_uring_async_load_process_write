#include "util/fd_guard.h"

//导入close()
#include <unistd.h>

namespace asyncdataloader::util {

FdGuard::FdGuard(int fd) noexcept : fd_(fd) {}

FdGuard::~FdGuard() noexcept {
    if(fd_ >= 0){
        ::close(fd_);
    }
    fd_ = -1;
}

FdGuard::FdGuard(FdGuard && other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
}

FdGuard& FdGuard::operator=(FdGuard && other) noexcept{
    if(&other != this){
        if (fd_ >= 0){
            ::close(fd_);
        }
        fd_ = other.fd_;
        other.fd_ = -1;
    }

    return *this;
}

int FdGuard::get() const noexcept {
    return fd_;
}

bool FdGuard::valid() const noexcept {
    return fd_ >= 0;
}

}  // namespace asyncdataloader::util
