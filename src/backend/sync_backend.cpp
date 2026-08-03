#include "backend/sync_backend.h"

#include "util/file_io.h"

#include <stdexcept>
#include <system_error>

namespace asyncdataloader::backend {

std::string_view SyncBackend::name() const noexcept {
    return "sync";
}

coroutine::Task<std::size_t> SyncBackend::read_at(
    int fd,
    std::span<std::byte> buffer,
    std::uint64_t offset
) {
    const util::ReadAtResult result = util::read_at(
        fd,
        buffer.data(),
        buffer.size(),
        offset
    );

    if (result.error_number != 0) {
        throw std::system_error(
            result.error_number,
            std::generic_category(),
            "SyncBackend::read_at"
        );
    }

    co_return result.bytes_read;
}

void SyncBackend::wait_one() {
    throw std::logic_error(
        "SyncBackend has no pending asynchronous completion"
    );
}

}  // namespace asyncdataloader::backend
