#include "util/fd_guard.h"

#include <liburing.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>

namespace {

constexpr unsigned kQueueDepth{4};
constexpr std::uint64_t kWriteOffset{0};
constexpr char kPayloadText[]{"AsyncDataLoader io_uring write demo\n"};

struct WriteRequest {
    std::uint64_t id{1};
    std::uint64_t offset{kWriteOffset};
    std::size_t expected_bytes{0};
};

int run_single_write(
    io_uring& ring,
    int output_fd,
    const std::string& payload,
    WriteRequest& request
) {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        std::cerr << "no available SQE\n";
        return 1;
    }
    io_uring_prep_write(
        sqe,
        output_fd,              // 写到哪个文件
        payload.data(),         // 从哪块内存取数据
        payload.size(),         // 写多少字节
        request.offset          // 写到文件的哪个位置
    );
    io_uring_sqe_set_data(sqe, &request);
    int submit_result = io_uring_submit(&ring);
    if (submit_result < 0) {
        std::cerr << "io_uring_submit failed: "
                  << std::strerror(-submit_result) << '\n';
        return 1;
    }
    if (submit_result != 1) {
        std::cerr << "expected 1 submission, got "
                  << submit_result << '\n';
        return 1;
    }
    io_uring_cqe* cqe = nullptr;
    int wait_result;
    do {
        wait_result = io_uring_wait_cqe(&ring, &cqe);
    } while (wait_result == -EINTR);

    if (wait_result < 0) {
        std::cerr << "io_uring_wait_cqe failed: "
                  << std::strerror(-wait_result) << '\n';
        return 1;
    }
    auto* completion_request = static_cast<WriteRequest*>(
        io_uring_cqe_get_data(cqe)
    );
    if (completion_request != &request) {
        std::cerr << "request mismatch\n";
        io_uring_cqe_seen(&ring, cqe);
        return 1;
    }
    int completion_result = cqe->res;
    io_uring_cqe_seen(&ring, cqe);
    if (completion_result < 0) {
        std::cerr << "write failed: "
                << std::strerror(-completion_result) << '\n';
        return 1;
    }

    if (static_cast<std::size_t>(completion_result) != request.expected_bytes) {
        std::cerr << "short write: expected " << request.expected_bytes
                << ", wrote " << completion_result << '\n';
        return 1;
    }
    std::cout << "bytes_written=" << completion_result
          << " request_id=" << request.id
          << " offset=" << request.offset << '\n';
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <output>\n";
        return 1;
    }

    const int output_fd = ::open(
        argv[1],
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        0644
    );
    if (output_fd < 0) {
        const int error_number = errno;
        std::cerr << "open output failed: " << std::strerror(error_number) << '\n';
        return 1;
    }
    asyncdataloader::util::FdGuard output{output_fd};

    std::string payload{kPayloadText};
    WriteRequest request{};
    request.expected_bytes = payload.size();

    io_uring ring{};
    const int init_result = io_uring_queue_init(kQueueDepth, &ring, 0);
    if (init_result < 0) {
        std::cerr << "io_uring_queue_init failed: "
                  << std::strerror(-init_result) << '\n';
        return 1;
    }

    const int run_result = run_single_write(
        ring,
        output.get(),
        payload,
        request
    );
    io_uring_queue_exit(&ring);
    return run_result;
}
