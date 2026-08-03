#include "coroutine/task.h"
#include "coroutine/uring_read_awaiter.h"
#include "util/file_io.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <system_error>
#include <vector>

namespace {

constexpr unsigned kQueueDepth{8};
constexpr std::size_t kReadSize{4096};

asyncdataloader::coroutine::Task<std::size_t> async_read(
    asyncdataloader::coroutine::UringContext& context,
    int fd,
    std::vector<char>& buffer
) {
    co_return co_await context.read_at(
        fd,
        buffer.data(),
        buffer.size(),
        std::uint64_t{0}
    );
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " <input>\n";
        return 1;
    }

    auto input = asyncdataloader::util::open_read_only(argv[1]);
    if (input.error_number != 0) {
        std::cerr << "open input failed: "
                  << std::generic_category().message(input.error_number)
                  << '\n';
        return 1;
    }

    std::vector<char> buffer(kReadSize);

    try {
        asyncdataloader::coroutine::UringContext context{kQueueDepth};
        auto read_task = async_read(context, input.fd.get(), buffer);

        read_task.start();
        if (!read_task.done()) {
            context.wait_one();
        }

        const std::size_t bytes_read = read_task.result();
        std::cout << "bytes_read=" << bytes_read << '\n';
        std::cout << "payload=";
        std::cout.write(
            buffer.data(),
            static_cast<std::streamsize>(bytes_read)
        );
        std::cout << '\n';
    } catch (const std::exception& error) {
        std::cerr << "async read failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
