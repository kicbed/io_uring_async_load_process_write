#include "backend/detail/backend_factory_operations.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace {

using asyncdataloader::backend::BackendConfig;
using asyncdataloader::backend::BackendKind;
using asyncdataloader::backend::IOBackend;
using asyncdataloader::backend::detail::BackendFactoryOperations;

class NamedBackend final : public IOBackend {
public:
    explicit NamedBackend(std::string_view name) noexcept : name_(name) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] asyncdataloader::coroutine::Task<std::size_t> read_at(
        int,
        std::span<std::byte>,
        std::uint64_t
    ) override {
        co_return std::size_t{0};
    }

    void wait_one() override {}

private:
    std::string_view name_;
};

std::size_t uring_calls{0};
std::size_t thread_pool_calls{0};
std::size_t sync_calls{0};

int fail(std::string_view message) {
    std::cerr << "stage13 backend-factory failure test failed: "
              << message << '\n';
    return 1;
}

void reset_calls() noexcept {
    uring_calls = 0;
    thread_pool_calls = 0;
    sync_calls = 0;
}

std::unique_ptr<IOBackend> uring_system_failure(const BackendConfig&) {
    ++uring_calls;
    throw std::system_error(
        EPERM,
        std::generic_category(),
        "injected io_uring construction failure"
    );
}

std::unique_ptr<IOBackend> uring_invalid_config(const BackendConfig&) {
    ++uring_calls;
    throw std::invalid_argument("injected invalid io_uring configuration");
}

std::unique_ptr<IOBackend> thread_pool_success(const BackendConfig&) {
    ++thread_pool_calls;
    return std::make_unique<NamedBackend>("injected_thread_pool");
}

std::unique_ptr<IOBackend> thread_pool_system_failure(
    const BackendConfig&
) {
    ++thread_pool_calls;
    throw std::system_error(
        EAGAIN,
        std::generic_category(),
        "injected thread-pool construction failure"
    );
}

std::unique_ptr<IOBackend> sync_success(const BackendConfig&) {
    ++sync_calls;
    return std::make_unique<NamedBackend>("injected_sync");
}

BackendFactoryOperations operations_with(
    BackendFactoryOperations::Constructor uring,
    BackendFactoryOperations::Constructor thread_pool
) noexcept {
    return BackendFactoryOperations{
        uring,
        thread_pool,
        &sync_success,
    };
}

int test_explicit_backend_is_fail_fast() {
    reset_calls();
    BackendConfig config;
    config.kind = BackendKind::Uring;
    const auto operations = operations_with(
        &uring_system_failure,
        &thread_pool_success
    );

    try {
        static_cast<void>(
            asyncdataloader::backend::detail::create_backend_with(
                config,
                operations
            )
        );
        return fail("explicit io_uring unexpectedly fell back");
    } catch (const std::system_error& error) {
        if (error.code().value() != EPERM) {
            return fail("explicit io_uring changed the construction error");
        }
    } catch (...) {
        return fail("explicit io_uring changed the exception type");
    }

    if (uring_calls != 1 || thread_pool_calls != 0 || sync_calls != 0) {
        return fail("explicit backend tried another constructor");
    }
    return 0;
}

int test_auto_falls_back_to_thread_pool() {
    reset_calls();
    BackendConfig config;
    config.kind = BackendKind::Auto;
    const auto operations = operations_with(
        &uring_system_failure,
        &thread_pool_success
    );

    auto backend = asyncdataloader::backend::detail::create_backend_with(
        config,
        operations
    );
    if (backend->name() != "injected_thread_pool" || uring_calls != 1 ||
        thread_pool_calls != 1 || sync_calls != 0) {
        return fail("Auto did not fall back from io_uring to thread pool");
    }
    return 0;
}

int test_auto_falls_back_to_sync() {
    reset_calls();
    BackendConfig config;
    config.kind = BackendKind::Auto;
    const auto operations = operations_with(
        &uring_system_failure,
        &thread_pool_system_failure
    );

    auto backend = asyncdataloader::backend::detail::create_backend_with(
        config,
        operations
    );
    if (backend->name() != "injected_sync" || uring_calls != 1 ||
        thread_pool_calls != 1 || sync_calls != 1) {
        return fail("Auto did not reach the final Sync fallback");
    }
    return 0;
}

int test_invalid_config_is_not_hidden_by_fallback() {
    reset_calls();
    BackendConfig config;
    config.kind = BackendKind::Auto;
    const auto operations = operations_with(
        &uring_invalid_config,
        &thread_pool_success
    );

    try {
        static_cast<void>(
            asyncdataloader::backend::detail::create_backend_with(
                config,
                operations
            )
        );
        return fail("Auto hid an invalid backend configuration");
    } catch (const std::invalid_argument& error) {
        if (std::string_view{error.what()} !=
            "injected invalid io_uring configuration") {
            return fail("invalid configuration changed its message");
        }
    } catch (...) {
        return fail("invalid configuration changed its exception type");
    }

    if (uring_calls != 1 || thread_pool_calls != 0 || sync_calls != 0) {
        return fail("invalid configuration incorrectly triggered fallback");
    }
    return 0;
}

}  // namespace

int main() {
    if (const int result = test_explicit_backend_is_fail_fast(); result != 0) {
        return result;
    }
    if (const int result = test_auto_falls_back_to_thread_pool(); result != 0) {
        return result;
    }
    if (const int result = test_auto_falls_back_to_sync(); result != 0) {
        return result;
    }
    if (const int result = test_invalid_config_is_not_hidden_by_fallback();
        result != 0) {
        return result;
    }
    return 0;
}
