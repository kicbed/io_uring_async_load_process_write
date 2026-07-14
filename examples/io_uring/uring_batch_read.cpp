#include "util/file_io.h"

#include <liburing.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

constexpr unsigned kQueueDepth{4};
constexpr std::size_t kRequestCount{4};
constexpr std::size_t kBlockSize{4096};

struct ReadRequest {
    std::uint64_t id{0};
    std::uint64_t offset{0};
    std::array<char, kBlockSize> buffer{};
    int completion_result{0};
    bool completed{false};
};

using RequestBatch = std::array<ReadRequest, kRequestCount>;

void initialize_requests(RequestBatch& requests) {
    for (std::size_t index = 0; index < requests.size(); ++index) {
        requests[index].id = index + 1;
        requests[index].offset = index * kBlockSize;
    }
}

int run_batch_read(io_uring& ring, int input_fd, RequestBatch& requests) {
    for (std::size_t i = 0; i < requests.size(); ++i) {
        io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        if (!sqe) {
            std::cerr << "no available SQE for request " << i << '\n';
            return 1;
        }
        io_uring_prep_read(
        sqe,
        input_fd,
        requests[i].buffer.data(),   // 读到这个 request 自己的 buffer
        requests[i].buffer.size(),   // 读 4096 字节
        requests[i].offset           // 从文件的 offset 位置开始
        );

        io_uring_sqe_set_data(sqe, &requests[i]);
    }

    int submit_result = io_uring_submit(&ring);
    if (submit_result < 0) {
        std::cerr << "io_uring_submit failed: "
                  << std::strerror(-submit_result) << '\n';
        return 1;
    }
    if (static_cast<std::size_t>(submit_result) != requests.size()) {
        std::cerr << "expected 4 submissions, got " << submit_result << '\n';
        return 1;
    }

    bool has_internal_error = false;
    bool has_request_error = false;
    std::size_t seen_count = 0;
    while (seen_count < requests.size()) {
        io_uring_cqe* cqe = nullptr;
        int wait_result = 0;
        do {
            wait_result = io_uring_wait_cqe(&ring, &cqe);
        } while (wait_result == -EINTR);

        if (wait_result < 0) {
            std::cerr << "io_uring_wait_cqe failed: "
                      << std::strerror(-wait_result) << '\n';
            return 1;
        }

        void* completion_data = io_uring_cqe_get_data(cqe);
        ReadRequest* request = nullptr;
        for (auto& candidate : requests) {
            if (completion_data == static_cast<void*>(&candidate)) {
                request = &candidate;
                break;
            }
        }

        if (request == nullptr) {
            std::cerr << "request pointer out of range\n";
            has_internal_error = true;
        } else if (request->completed) {
            std::cerr << "request " << request->id << " already completed\n";
            has_internal_error = true;
        } else {
            request->completion_result = cqe->res;
            request->completed = true;
            if (request->completion_result < 0) {
                has_request_error = true;
            }
        }

        io_uring_cqe_seen(&ring, cqe);
        ++seen_count;
    }

    std::size_t total_bytes_read = 0;
    for (const auto& request : requests) {
        std::cout << "request_id=" << request.id
                  << " offset=" << request.offset
                  << " bytes_read=";

        if (!request.completed) {
            std::cout << "not_completed";
            has_internal_error = true;
        } else if (request.completion_result < 0) {
            std::cout << request.completion_result
                      << " error=" << std::strerror(-request.completion_result);
        } else {
            std::cout << request.completion_result;
            total_bytes_read += static_cast<std::size_t>(request.completion_result);
        }
        std::cout << '\n';
    }

    std::cout << "total_bytes_read=" << total_bytes_read << '\n';
    return (has_internal_error || has_request_error) ? 1 : 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <input>\n";
        return 1;
    }

    auto input = asyncdataloader::util::open_read_only(argv[1]);
    if (input.error_number != 0) {
        std::cerr << "open input failed: " << std::strerror(input.error_number) << '\n';
        return 1;
    }

    RequestBatch requests{};
    initialize_requests(requests);

    io_uring ring{};
    const int init_result = io_uring_queue_init(kQueueDepth, &ring, 0);
    if (init_result < 0) {
        std::cerr << "io_uring_queue_init failed: "
                  << std::strerror(-init_result) << '\n';
        return 1;
    }

    const int run_result = run_batch_read(ring, input.fd.get(), requests);
    io_uring_queue_exit(&ring);
    return run_result;
}
