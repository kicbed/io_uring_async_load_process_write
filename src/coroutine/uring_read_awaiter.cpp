#include "coroutine/uring_read_awaiter.h"

#include <cerrno>
#include <limits>
#include <stdexcept>
#include <system_error>

namespace asyncdataloader::coroutine {

UringContext::UringContext(unsigned queue_depth) {
    if (queue_depth == 0) {
        throw std::invalid_argument("io_uring queue depth must be positive");
    }

    const int init_result = io_uring_queue_init(queue_depth, &ring_, 0);
    if (init_result < 0) {
        throw std::system_error(
            -init_result,
            std::generic_category(),
            "io_uring_queue_init"
        );
    }
}

UringContext::~UringContext() {
    io_uring_queue_exit(&ring_);
}

ReadAwaiter UringContext::read_at(
    int fd,
    void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
) {
    return ReadAwaiter{*this, fd, buffer, byte_count, offset};
}

void UringContext::wait_one() {
    io_uring_cqe* cqe = nullptr;
    int wait_result = 0;
    do {
        wait_result = io_uring_wait_cqe(&ring_, &cqe);
    } while (wait_result == -EINTR);

    if (wait_result < 0) {
        throw std::system_error(
            -wait_result,
            std::generic_category(),
            "io_uring_wait_cqe"
        );
    }

    auto* request = static_cast<CompletionRequest*>(
        io_uring_cqe_get_data(cqe)
    );
    const int completion_result = cqe->res;
    io_uring_cqe_seen(&ring_, cqe);

    if (request == nullptr) {
        throw std::runtime_error("io_uring CQE has no CompletionRequest");
    }

    // complete() resumes the coroutine and may destroy request immediately.
    request->complete(completion_result);
}

ReadAwaiter::ReadAwaiter(
    UringContext& context,
    int fd,
    void* buffer,
    std::size_t byte_count,
    std::uint64_t offset
)
    : context_(context),
      fd_(fd),
      buffer_(buffer),
      byte_count_(byte_count),
      offset_(offset) {
    if (fd_ < 0) {
        throw std::invalid_argument("read fd must be valid");
    }
    if (buffer_ == nullptr && byte_count_ != 0) {
        throw std::invalid_argument("read buffer must not be null");
    }
    if (byte_count_ > std::numeric_limits<unsigned>::max()) {
        throw std::length_error("read byte count exceeds io_uring limit");
    }
}

bool ReadAwaiter::await_ready() const noexcept {
    return false;
}

bool ReadAwaiter::await_suspend(std::coroutine_handle<> continuation) {
    io_uring_sqe* sqe = io_uring_get_sqe(&context_.ring_);
    if (sqe == nullptr) {
        immediate_result_ = -EAGAIN;
        has_immediate_result_ = true;
        return false;
    }

    io_uring_prep_read(
        sqe,
        fd_,
        buffer_,
        static_cast<unsigned>(byte_count_),
        offset_
    );
    io_uring_sqe_set_data(sqe, &request_);

    const int submit_result = io_uring_submit(&context_.ring_);
    if (submit_result < 0) {
        immediate_result_ = submit_result;
        has_immediate_result_ = true;
        return false;
    }
    if (submit_result != 1) {
        immediate_result_ = -EIO;
        has_immediate_result_ = true;
        return false;
    }

    // This learning context has no concurrent CQ poller. The request is armed
    // after successful submission and before main starts waiting for its CQE.
    request_.set_continuation(continuation);
    return true;
}

std::size_t ReadAwaiter::await_resume() {
    const int result = has_immediate_result_
        ? immediate_result_
        : request_.completion_result();

    if (result < 0) {
        throw std::system_error(
            -result,
            std::generic_category(),
            "io_uring read"
        );
    }

    return static_cast<std::size_t>(result);
}

}  // namespace asyncdataloader::coroutine
