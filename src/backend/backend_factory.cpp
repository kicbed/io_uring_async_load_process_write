#include "backend/backend_factory.h"

#include "backend/sync_backend.h"
#include "backend/thread_pool_backend.h"
#include "backend/uring_backend.h"

#include <stdexcept>
#include <system_error>

namespace asyncdataloader::backend {
namespace {

std::unique_ptr<IOBackend> create_uring(const BackendConfig& config) {
    return std::make_unique<UringBackend>(config.uring_queue_depth);
}

std::unique_ptr<IOBackend> create_thread_pool(const BackendConfig& config) {
    return std::make_unique<ThreadPoolBackend>(
        config.thread_pool_worker_count,
        config.max_inflight
    );
}

}  // namespace

std::unique_ptr<IOBackend> BackendFactory::create(
    const BackendConfig& config
) {
    switch (config.kind) {
    case BackendKind::Uring:
        return create_uring(config);
    case BackendKind::ThreadPool:
        return create_thread_pool(config);
    case BackendKind::Sync:
        return std::make_unique<SyncBackend>();
    case BackendKind::Auto:
        break;
    default:
        throw std::invalid_argument("unknown backend kind");
    }

    if (config.auto_try_uring) {
        try {
            return create_uring(config);
        } catch (const std::system_error&) {
            // Initialization failed; Auto mode may try the next backend.
        }
    }

    if (config.auto_try_thread_pool) {
        try {
            return create_thread_pool(config);
        } catch (const std::system_error&) {
            // Worker creation failed; SyncBackend is the final fallback.
        }
    }

    return std::make_unique<SyncBackend>();
}

}  // namespace asyncdataloader::backend
