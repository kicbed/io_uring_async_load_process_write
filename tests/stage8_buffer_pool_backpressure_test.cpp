#include "buffer/aligned_buffer_pool.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage8 buffer-pool backpressure test failed: " << message
              << '\n';
    return 1;
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    using asyncdataloader::buffer::AlignedBufferPool;
    using asyncdataloader::buffer::BufferHandle;
    using asyncdataloader::config::PipelineConfig;

    PipelineConfig blocking_config;
    blocking_config.block_size = 4096;
    blocking_config.max_inflight_buffers = 1;
    blocking_config.queue_depth = 1;
    blocking_config.buffer_alignment = 4096;

    AlignedBufferPool blocking_pool(blocking_config);
    std::optional<BufferHandle> first{blocking_pool.acquire()};
    std::byte* const first_address = first->data();

    std::promise<void> waiter_started;
    auto waiter_started_future = waiter_started.get_future();
    auto blocked_acquire = std::async(
        std::launch::async,
        [&blocking_pool, &waiter_started] {
            waiter_started.set_value();
            return blocking_pool.acquire();
        }
    );

    waiter_started_future.wait();
    if (blocked_acquire.wait_for(50ms) != std::future_status::timeout) {
        return fail("acquire did not wait while the only buffer was leased");
    }

    first.reset();
    {
        BufferHandle awakened = blocked_acquire.get();
        if (!awakened.valid() || awakened.data() != first_address) {
            return fail("waiting acquire did not receive the returned buffer");
        }
        if (blocking_pool.available() != 0) {
            return fail("awakened handle was also reported as available");
        }
    }
    if (blocking_pool.available() != 1) {
        return fail("awakened handle did not return its buffer on scope exit");
    }

    PipelineConfig stress_config;
    stress_config.block_size = 4096;
    stress_config.max_inflight_buffers = 2;
    stress_config.queue_depth = 1;
    stress_config.buffer_alignment = 4096;

    AlignedBufferPool stress_pool(stress_config);
    std::atomic<int> active_handles{0};
    std::atomic<int> maximum_active{0};
    std::atomic<bool> capacity_violation{false};

    {
        std::vector<std::jthread> workers;
        for (int worker_index = 0; worker_index < 4; ++worker_index) {
            workers.emplace_back([&, worker_index] {
                for (int iteration = 0; iteration < 100; ++iteration) {
                    auto handle = stress_pool.acquire();
                    const int active_now = active_handles.fetch_add(1) + 1;

                    int observed_maximum = maximum_active.load();
                    while (observed_maximum < active_now &&
                           !maximum_active.compare_exchange_weak(
                               observed_maximum,
                               active_now
                           )) {
                    }

                    if (active_now > 2) {
                        capacity_violation.store(true);
                    }

                    handle.bytes().front() =
                        static_cast<std::byte>(worker_index);
                    std::this_thread::yield();
                    active_handles.fetch_sub(1);
                }
            });
        }
    }

    if (capacity_violation.load() || maximum_active.load() > 2) {
        return fail("concurrent acquires exceeded configured pool capacity");
    }
    if (maximum_active.load() == 0 || stress_pool.available() != 2) {
        return fail("stress run did not return every buffer");
    }

    return 0;
}
