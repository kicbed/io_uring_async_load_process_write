#include "backend/backend_factory.h"

#include "backend/detail/backend_factory_operations.h"
#include "backend/sync_backend.h"
#include "backend/thread_pool_backend.h"
#if ASYNCDATALOADER_HAS_LIBURING
#include "backend/uring_backend.h"
#endif

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace asyncdataloader::backend {
namespace {

std::unique_ptr<IOBackend> create_uring(const BackendConfig& config) {
#if ASYNCDATALOADER_HAS_LIBURING
    return std::make_unique<UringBackend>(config.uring_queue_depth);
#else
    if (config.uring_queue_depth == 0) {
        throw std::invalid_argument("io_uring queue depth must be positive");
    }
    throw std::system_error(
        ENOSYS,
        std::generic_category(),
        "io_uring support is not compiled into this build"
    );
#endif
}

std::unique_ptr<IOBackend> create_thread_pool(const BackendConfig& config) {
    return std::make_unique<ThreadPoolBackend>(
        config.thread_pool_worker_count,
        config.max_inflight
    );
}

std::unique_ptr<IOBackend> create_sync(const BackendConfig&) {
    return std::make_unique<SyncBackend>();
}

}  // namespace

namespace detail {

BackendFactoryOperations system_backend_factory_operations() noexcept {
    return BackendFactoryOperations{
        &create_uring,
        &create_thread_pool,
        &create_sync,
    };
}

std::unique_ptr<IOBackend> create_backend_with(
    const BackendConfig& config,
    const BackendFactoryOperations& operations
) {
    switch (config.kind) {
    case BackendKind::Uring:
        return operations.create_uring(config);
    case BackendKind::ThreadPool:
        return operations.create_thread_pool(config);
    case BackendKind::Sync:
        return operations.create_sync(config);
    case BackendKind::Auto:
        break;
    default:
        throw std::invalid_argument("unknown backend kind");
    }

    if (config.auto_try_uring) {
        try {
            return operations.create_uring(config);
        } catch (const std::system_error&) {
            // Initialization failed; Auto mode may try the next backend.
        }
    }

    if (config.auto_try_thread_pool) {
        try {
            return operations.create_thread_pool(config);
        } catch (const std::system_error&) {
            // Worker creation failed; SyncBackend is the final fallback.
        }
    }

    return operations.create_sync(config);
}

}  // namespace detail

std::unique_ptr<IOBackend> BackendFactory::create(
    const BackendConfig& config
) {
    return detail::create_backend_with(
        config,
        detail::system_backend_factory_operations()
    );
}

}  // namespace asyncdataloader::backend
