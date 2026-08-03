#include "backend/backend_factory.h"
#include "util/file_io.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::size_t kReadSize{4096};
constexpr std::string_view kBackendPrefix{"--backend="};

void print_usage(const char* program_name) {
    std::cerr
        << "usage: " << program_name
        << " <input> [--backend=auto|uring|thread_pool|sync]"
           " [--disable-uring] [--disable-thread-pool]\n";
}

asyncdataloader::backend::BackendKind parse_backend_kind(
    std::string_view value
) {
    using asyncdataloader::backend::BackendKind;

    if (value == "auto") {
        return BackendKind::Auto;
    }
    if (value == "uring") {
        return BackendKind::Uring;
    }
    if (value == "thread_pool") {
        return BackendKind::ThreadPool;
    }
    if (value == "sync") {
        return BackendKind::Sync;
    }

    throw std::invalid_argument("unknown backend: " + std::string{value});
}

std::string_view backend_kind_name(
    asyncdataloader::backend::BackendKind kind
) noexcept {
    using asyncdataloader::backend::BackendKind;

    switch (kind) {
    case BackendKind::Auto:
        return "auto";
    case BackendKind::Uring:
        return "uring";
    case BackendKind::ThreadPool:
        return "thread_pool";
    case BackendKind::Sync:
        return "sync";
    }

    return "unknown";
}

asyncdataloader::backend::BackendConfig parse_backend_config(
    int argc,
    char* argv[]
) {
    asyncdataloader::backend::BackendConfig config;
    bool backend_was_set{false};

    for (int index = 2; index < argc; ++index) {
        const std::string_view argument{argv[index]};

        if (argument.starts_with(kBackendPrefix)) {
            if (backend_was_set) {
                throw std::invalid_argument("--backend may only be specified once");
            }

            config.kind = parse_backend_kind(
                argument.substr(kBackendPrefix.size())
            );
            backend_was_set = true;
            continue;
        }

        if (argument == "--disable-uring") {
            config.auto_try_uring = false;
            continue;
        }

        if (argument == "--disable-thread-pool") {
            config.auto_try_thread_pool = false;
            continue;
        }

        throw std::invalid_argument("unknown option: " + std::string{argument});
    }

    if (config.kind != asyncdataloader::backend::BackendKind::Auto &&
        (!config.auto_try_uring || !config.auto_try_thread_pool)) {
        throw std::invalid_argument(
            "--disable-* options are only valid with --backend=auto"
        );
    }

    return config;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }

    asyncdataloader::backend::BackendConfig config;
    try {
        config = parse_backend_config(argc, argv);
    } catch (const std::invalid_argument& error) {
        std::cerr << "invalid arguments: " << error.what() << '\n';
        print_usage(argv[0]);
        return 2;
    }

    auto input = asyncdataloader::util::open_read_only(argv[1]);
    if (input.error_number != 0) {
        std::cerr << "open input failed: "
                  << std::generic_category().message(input.error_number)
                  << '\n';
        return 1;
    }

    std::array<std::byte, kReadSize> buffer{};

    try {
        auto backend =
            asyncdataloader::backend::BackendFactory::create(config);
        auto read_task = backend->read_at(
            input.fd.get(),
            std::span<std::byte>{buffer},
            std::uint64_t{0}
        );

        read_task.start();
        if (!read_task.done()) {
            backend->wait_one();
        }

        const std::size_t bytes_read = read_task.result();
        std::cout << "requested_backend=" << backend_kind_name(config.kind)
                  << '\n';
        std::cout << "selected_backend=" << backend->name() << '\n';
        std::cout << "bytes_read=" << bytes_read << '\n';
        std::cout << "payload=";
        std::cout.write(
            reinterpret_cast<const char*>(buffer.data()),
            static_cast<std::streamsize>(bytes_read)
        );
        std::cout << '\n';
    } catch (const std::exception& error) {
        std::cerr << "backend read failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
