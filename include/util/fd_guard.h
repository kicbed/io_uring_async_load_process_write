#pragma once

namespace asyncdataloader::util {

class FdGuard {
public:
    FdGuard() noexcept = default;
    explicit FdGuard(int fd) noexcept;
    ~FdGuard() noexcept;

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    FdGuard(FdGuard&&) noexcept;
    FdGuard& operator=(FdGuard&&) noexcept;

    [[nodiscard]] int get() const noexcept;
    [[nodiscard]] bool valid() const noexcept;

private:
    int fd_{-1};
};

}  // namespace asyncdataloader::util
