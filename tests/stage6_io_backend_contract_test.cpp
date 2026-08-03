#include "backend/io_backend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <type_traits>

namespace {

class ImmediateReadBackend final : public asyncdataloader::backend::IOBackend {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "immediate";
    }

    [[nodiscard]] asyncdataloader::coroutine::Task<std::size_t> read_at(
        int fd,
        std::span<std::byte> buffer,
        std::uint64_t offset
    ) override {
        ++read_call_count_;
        last_fd_ = fd;
        last_buffer_size_ = buffer.size();
        last_offset_ = offset;
        co_return buffer.size();
    }

    void wait_one() override {
        ++wait_one_call_count_;
    }

    [[nodiscard]] std::size_t read_call_count() const noexcept {
        return read_call_count_;
    }

    [[nodiscard]] std::size_t wait_one_call_count() const noexcept {
        return wait_one_call_count_;
    }

    [[nodiscard]] int last_fd() const noexcept {
        return last_fd_;
    }

    [[nodiscard]] std::size_t last_buffer_size() const noexcept {
        return last_buffer_size_;
    }

    [[nodiscard]] std::uint64_t last_offset() const noexcept {
        return last_offset_;
    }

private:
    std::size_t read_call_count_{0};
    std::size_t wait_one_call_count_{0};
    int last_fd_{-1};
    std::size_t last_buffer_size_{0};
    std::uint64_t last_offset_{0};
};

int fail(std::string_view message) {
    std::cerr << "stage6 IOBackend contract test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::backend::IOBackend;

    static_assert(std::is_abstract_v<IOBackend>);
    static_assert(std::has_virtual_destructor_v<IOBackend>);
    static_assert(!std::is_copy_constructible_v<ImmediateReadBackend>);
    static_assert(!std::is_move_constructible_v<ImmediateReadBackend>);

    constexpr int kFakeFd{42};
    constexpr std::uint64_t kOffset{4096};
    std::array<std::byte, 16> storage{};

    ImmediateReadBackend concrete_backend;
    IOBackend& backend = concrete_backend;

    if (backend.name() != "immediate") {
        return fail("virtual name() dispatch returned the wrong backend name");
    }

    auto read_task = backend.read_at(
        kFakeFd,
        std::span<std::byte>{storage},
        kOffset
    );

    if (!read_task.valid()) {
        return fail("read_at() returned an invalid Task");
    }
    if (read_task.done()) {
        return fail("a lazy Task should not be done before start()");
    }
    if (concrete_backend.read_call_count() != 0) {
        return fail("the coroutine body ran before Task::start()");
    }

    read_task.start();

    if (!read_task.done()) {
        return fail("the immediate backend should complete during start()");
    }
    if (concrete_backend.read_call_count() != 1) {
        return fail("read_at() coroutine body should run exactly once");
    }
    if (concrete_backend.last_fd() != kFakeFd) {
        return fail("read_at() did not preserve the file descriptor");
    }
    if (concrete_backend.last_buffer_size() != storage.size()) {
        return fail("read_at() did not preserve the buffer view");
    }
    if (concrete_backend.last_offset() != kOffset) {
        return fail("read_at() did not preserve the file offset");
    }
    if (read_task.result() != storage.size()) {
        return fail("read_at() returned the wrong byte count");
    }
    if (concrete_backend.wait_one_call_count() != 0) {
        return fail("an immediately completed Task should not require wait_one()");
    }

    return 0;
}
