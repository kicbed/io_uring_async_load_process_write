#include "backend/thread_pool_backend.h"

#include "coroutine/completion_request.h"
#include "util/file_io.h"

#include <cerrno>
#include <coroutine>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace asyncdataloader::backend {

struct ThreadPoolBackend::ReadRequest {
    ReadRequest(
        int request_fd,
        void* request_buffer,
        std::size_t request_byte_count,
        std::uint64_t request_offset
    ) noexcept
        : fd(request_fd),
          buffer(request_buffer),
          byte_count(request_byte_count),
          offset(request_offset) {}

    int fd;
    void* buffer;
    std::size_t byte_count;
    std::uint64_t offset;
    coroutine::CompletionRequest completion;
    int completion_result{0};
};

class ThreadPoolBackend::ReadAwaiter {
public:
    ReadAwaiter(
        ThreadPoolBackend& backend,
        int fd,
        std::span<std::byte> buffer,
        std::uint64_t offset
    )
        : backend_(backend),
          request_(fd, buffer.data(), buffer.size(), offset) {
        if (buffer.data() == nullptr && !buffer.empty()) {
            throw std::invalid_argument("read buffer must not be null");
        }
        if (buffer.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::length_error(
                "thread-pool read byte count exceeds completion limit"
            );
        }
    }

    ReadAwaiter(const ReadAwaiter&) = delete;
    ReadAwaiter& operator=(const ReadAwaiter&) = delete;
    ReadAwaiter(ReadAwaiter&&) = delete;
    ReadAwaiter& operator=(ReadAwaiter&&) = delete;

    [[nodiscard]] bool await_ready() const noexcept {
        return false;
    }

    bool await_suspend(std::coroutine_handle<> continuation) {
        request_.completion.set_continuation(continuation);

        const int submit_result = backend_.try_submit(&request_);
        if (submit_result < 0) {
            immediate_result_ = submit_result;
            has_immediate_result_ = true;
            return false;
        }

        return true;
    }

    std::size_t await_resume() {
        const int result = has_immediate_result_
            ? immediate_result_
            : request_.completion.completion_result();

        if (result < 0) {
            throw std::system_error(
                -result,
                std::generic_category(),
                "thread-pool read"
            );
        }

        return static_cast<std::size_t>(result);
    }

private:
    ThreadPoolBackend& backend_;
    ReadRequest request_;
    int immediate_result_{0};
    bool has_immediate_result_{false};
};

ThreadPoolBackend::ThreadPoolBackend(
    std::size_t worker_count,
    std::size_t max_inflight
)
    : max_inflight_(max_inflight) {
    if (worker_count == 0) {
        throw std::invalid_argument(
            "thread-pool worker count must be positive"
        );
    }
    if (max_inflight_ == 0) {
        throw std::invalid_argument(
            "thread-pool max_inflight must be positive"
        );
    }

    work_queue_.reserve(max_inflight_);
    completion_queue_.reserve(max_inflight_);
    workers_.reserve(worker_count);

    try {
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    } catch (...) {
        {
            const std::lock_guard lock{mutex_};
            stopping_ = true;
        }
        work_available_.notify_all();
        workers_.clear();
        throw;
    }
}

ThreadPoolBackend::~ThreadPoolBackend() {
    {
        const std::lock_guard lock{mutex_};
        stopping_ = true;
    }
    work_available_.notify_all();
    workers_.clear();
}

std::string_view ThreadPoolBackend::name() const noexcept {
    return "thread_pool";
}

coroutine::Task<std::size_t> ThreadPoolBackend::read_at(
    int fd,
    std::span<std::byte> buffer,
    std::uint64_t offset
) {
    co_return co_await ReadAwaiter{*this, fd, buffer, offset};
}

void ThreadPoolBackend::wait_one() {
    ReadRequest* request = nullptr;
    int completion_result = 0;

    {
        std::unique_lock lock{mutex_};
        if (inflight_count_ == 0) {
            throw std::logic_error(
                "ThreadPoolBackend has no pending completion"
            );
        }

        completion_available_.wait(lock, [this] {
            return !completion_queue_.empty();
        });

        request = completion_queue_.back();
        completion_queue_.pop_back();
        completion_result = request->completion_result;
        --inflight_count_;
    }

    // Resuming the coroutine can destroy request. Do not access it afterward.
    request->completion.complete(completion_result);
}

int ThreadPoolBackend::try_submit(ReadRequest* request) {
    {
        const std::lock_guard lock{mutex_};
        if (stopping_) {
            return -ECANCELED;
        }
        if (inflight_count_ >= max_inflight_) {
            return -EAGAIN;
        }

        work_queue_.push_back(request);
        ++inflight_count_;
    }

    work_available_.notify_one();
    return 0;
}

void ThreadPoolBackend::worker_loop() {
    while (true) {
        ReadRequest* request = nullptr;

        {
            std::unique_lock lock{mutex_};
            work_available_.wait(lock, [this] {
                return stopping_ || !work_queue_.empty();
            });

            if (stopping_ && work_queue_.empty()) {
                return;
            }

            request = work_queue_.back();
            work_queue_.pop_back();
        }

        const util::ReadAtResult result = util::read_at(
            request->fd,
            request->buffer,
            request->byte_count,
            request->offset
        );

        const int completion_result = result.error_number != 0
            ? -result.error_number
            : static_cast<int>(result.bytes_read);

        {
            const std::lock_guard lock{mutex_};
            request->completion_result = completion_result;
            completion_queue_.push_back(request);
        }
        completion_available_.notify_one();
    }
}

}  // namespace asyncdataloader::backend
