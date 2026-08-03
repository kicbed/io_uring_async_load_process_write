#include "backend/uring_backend.h"

namespace asyncdataloader::backend {

UringBackend::UringBackend(unsigned queue_depth)
    : context_(queue_depth) {}

std::string_view UringBackend::name() const noexcept {
    return "io_uring";
}

coroutine::Task<std::size_t> UringBackend::read_at(
    int fd,
    std::span<std::byte> buffer,
    std::uint64_t offset
) {
    co_return co_await context_.read_at(
        fd,
        buffer.data(),
        buffer.size(),
        offset
    );
}

void UringBackend::wait_one() {
    context_.wait_one();
}

}  // namespace asyncdataloader::backend
