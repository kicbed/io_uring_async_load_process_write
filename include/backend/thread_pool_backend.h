#pragma once

#include "backend/io_backend.h"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

namespace asyncdataloader::backend {

class ThreadPoolBackend final : public IOBackend {
public:
    ThreadPoolBackend(
        std::size_t worker_count,
        std::size_t max_inflight
    );
    ~ThreadPoolBackend() override;

    [[nodiscard]] std::string_view name() const noexcept override;

    [[nodiscard]] coroutine::Task<std::size_t> read_at(
        int fd,
        std::span<std::byte> buffer,
        std::uint64_t offset
    ) override;

    void wait_one() override;

private:
    struct ReadRequest;
    class ReadAwaiter;

    [[nodiscard]] int try_submit(ReadRequest* request);
    void worker_loop();

    std::size_t max_inflight_;
    std::mutex mutex_;
    std::condition_variable work_available_;
    std::condition_variable completion_available_;
    std::vector<ReadRequest*> work_queue_;
    std::vector<ReadRequest*> completion_queue_;
    std::size_t inflight_count_{0};
    bool stopping_{false};
    std::vector<std::jthread> workers_;
};

}  // namespace asyncdataloader::backend
