#include "util/file_io.h"

#include <liburing.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr unsigned kQueueDepth{8};
constexpr std::size_t kReadSize{4096};

struct ReadRequest {
    std::uint64_t id{1};
};

int run_single_read(
    io_uring& ring,
    int input_fd,
    std::vector<char>& buffer,
    ReadRequest& request
) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        std::cerr << "no available SQE\n";
        return 1;
    }

    io_uring_prep_read(sqe, input_fd, buffer.data(), buffer.size(), 0);

    io_uring_sqe_set_data(sqe, &request);   // 把 request 指针挂到 SQE

    int submit_result = io_uring_submit(&ring);
    if (submit_result < 0) {
        std::cerr << "io_uring_submit failed: "
                << std::strerror(-submit_result) << '\n';
        return 1;
    }

    if (submit_result != 1) {   // 只提交了一个请求，返回值应该为 1
        std::cerr << "expected 1 submission, got " << submit_result << '\n';
        return 1;
    }

    struct io_uring_cqe* cqe = nullptr;
    int wait_result = 0;
    do {
        wait_result = io_uring_wait_cqe(&ring, &cqe);
    } while (wait_result == -EINTR);

    if (wait_result < 0) {
        std::cerr << "io_uring_wait_cqe failed: "
                << std::strerror(-wait_result) << '\n';
        return 1;
    }

    auto* completion_request = static_cast<ReadRequest*>(
    io_uring_cqe_get_data(cqe)
    );

    // 验证是不是我们提交的那个 request
    if (completion_request != &request) {
        std::cerr << "request mismatch\n";
        io_uring_cqe_seen(&ring, cqe);  // 即使验证失败也要消费
        return 1;
    }

    int completion_result = cqe->res;
    if (completion_result < 0) {
        std::cerr << "read failed: " << std::strerror(-completion_result) << '\n';
        io_uring_cqe_seen(&ring, cqe);
        return 1;
    }

    io_uring_cqe_seen(&ring, cqe);   // 告诉内核这个 CQE 可以复用了

    std::cout << "bytes_read=" << completion_result
            << " request_id=" << completion_request->id << '\n';
    return 0;
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

    std::vector<char> buffer(kReadSize);
    ReadRequest request{};
    io_uring ring{};

    int init_result = io_uring_queue_init(kQueueDepth, &ring, 0);
    if (init_result < 0) {
        std::cerr << "io_uring_queue_init failed: "
                << std::strerror(-init_result) << '\n';
        return 1;   // 初始化失败，不用调 queue_exit
    }

    int run_result = run_single_read(ring, input.fd.get(), buffer, request);

    io_uring_queue_exit(&ring);   // 只调用一次，无论成功失败

    return run_result;
}
