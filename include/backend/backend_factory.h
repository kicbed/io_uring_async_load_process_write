#pragma once

#include "backend/io_backend.h"

#include <cstddef>
#include <memory>

namespace asyncdataloader::backend {

enum class BackendKind {
    Auto,
    Uring,
    ThreadPool,
    Sync,
};

struct BackendConfig {
    BackendKind kind{BackendKind::Auto};
    unsigned uring_queue_depth{8};
    std::size_t thread_pool_worker_count{2};
    std::size_t max_inflight{8};

    // These switches only control which candidates Auto mode may try.
    bool auto_try_uring{true};
    bool auto_try_thread_pool{true};
};

class BackendFactory {
public:
    [[nodiscard]] static std::unique_ptr<IOBackend> create(
        const BackendConfig& config
    );
};

}  // namespace asyncdataloader::backend
