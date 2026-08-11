#include "backend/backend_factory.h"

#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage6 BackendFactory test failed: " << message << '\n';
    return 1;
}

bool creates_named_backend(
    const asyncdataloader::backend::BackendConfig& config,
    std::string_view expected_name
) {
    auto backend = asyncdataloader::backend::BackendFactory::create(config);
    return backend != nullptr && backend->name() == expected_name;
}

}  // namespace

int main() {
    using asyncdataloader::backend::BackendConfig;
    using asyncdataloader::backend::BackendFactory;
    using asyncdataloader::backend::BackendKind;

    BackendConfig sync_config;
    sync_config.kind = BackendKind::Sync;
    if (!creates_named_backend(sync_config, "sync")) {
        return fail("explicit Sync selection created the wrong backend");
    }

    BackendConfig thread_pool_config;
    thread_pool_config.kind = BackendKind::ThreadPool;
    thread_pool_config.thread_pool_worker_count = 1;
    thread_pool_config.max_inflight = 2;
    if (!creates_named_backend(thread_pool_config, "thread_pool")) {
        return fail("explicit ThreadPool selection created the wrong backend");
    }

    BackendConfig uring_config;
    uring_config.kind = BackendKind::Uring;
    uring_config.uring_queue_depth = 2;
#if ASYNCDATALOADER_HAS_LIBURING
    if (!creates_named_backend(uring_config, "io_uring")) {
        return fail("explicit Uring selection created the wrong backend");
    }
#else
    try {
        static_cast<void>(BackendFactory::create(uring_config));
        return fail("explicit Uring should fail without compiled support");
    } catch (const std::system_error& error) {
        if (error.code().value() != ENOSYS) {
            return fail("uncompiled Uring did not report ENOSYS");
        }
    } catch (...) {
        return fail("uncompiled Uring used the wrong exception type");
    }

    BackendConfig auto_without_uring_config;
    auto_without_uring_config.kind = BackendKind::Auto;
    auto_without_uring_config.thread_pool_worker_count = 1;
    auto_without_uring_config.max_inflight = 2;
    if (!creates_named_backend(
            auto_without_uring_config,
            "thread_pool"
        )) {
        return fail("Auto did not bypass uncompiled io_uring support");
    }
#endif

    BackendConfig auto_thread_pool_config;
    auto_thread_pool_config.kind = BackendKind::Auto;
    auto_thread_pool_config.auto_try_uring = false;
    auto_thread_pool_config.thread_pool_worker_count = 1;
    auto_thread_pool_config.max_inflight = 2;
    if (!creates_named_backend(auto_thread_pool_config, "thread_pool")) {
        return fail("Auto should select ThreadPool when io_uring is skipped");
    }

    BackendConfig auto_sync_config;
    auto_sync_config.kind = BackendKind::Auto;
    auto_sync_config.auto_try_uring = false;
    auto_sync_config.auto_try_thread_pool = false;
    if (!creates_named_backend(auto_sync_config, "sync")) {
        return fail("Auto should select Sync when async candidates are skipped");
    }

    BackendConfig invalid_uring_config;
    invalid_uring_config.kind = BackendKind::Uring;
    invalid_uring_config.uring_queue_depth = 0;
    try {
        static_cast<void>(BackendFactory::create(invalid_uring_config));
        return fail("invalid explicit Uring config should not fall back");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("invalid Uring config should preserve invalid_argument");
    }

    BackendConfig invalid_auto_config;
    invalid_auto_config.kind = BackendKind::Auto;
    invalid_auto_config.uring_queue_depth = 0;
    try {
        static_cast<void>(BackendFactory::create(invalid_auto_config));
        return fail("invalid enabled Auto candidate should not be hidden");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("invalid Auto candidate should preserve invalid_argument");
    }

    BackendConfig unknown_config;
    unknown_config.kind = static_cast<BackendKind>(999);
    try {
        static_cast<void>(BackendFactory::create(unknown_config));
        return fail("unknown backend kind should be rejected");
    } catch (const std::invalid_argument&) {
    } catch (...) {
        return fail("unknown backend kind should use invalid_argument");
    }

    return 0;
}
