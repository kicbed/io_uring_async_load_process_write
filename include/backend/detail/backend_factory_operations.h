#pragma once

#include "backend/backend_factory.h"

#include <memory>

namespace asyncdataloader::backend::detail {

// Internal constructor seam. It lets tests inject deterministic construction
// failures while exercising the real BackendFactory fallback policy.
struct BackendFactoryOperations {
    using Constructor = std::unique_ptr<IOBackend> (*)(
        const BackendConfig&
    );

    Constructor create_uring{nullptr};
    Constructor create_thread_pool{nullptr};
    Constructor create_sync{nullptr};
};

[[nodiscard]] BackendFactoryOperations
system_backend_factory_operations() noexcept;

[[nodiscard]] std::unique_ptr<IOBackend> create_backend_with(
    const BackendConfig& config,
    const BackendFactoryOperations& operations
);

}  // namespace asyncdataloader::backend::detail
