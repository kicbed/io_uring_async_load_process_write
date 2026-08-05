#include "buffer/aligned_buffer_pool.h"
#include "pipeline/spsc_queue.h"

#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage8 SPSC queue test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    using asyncdataloader::buffer::AlignedBufferPool;
    using asyncdataloader::buffer::BufferHandle;
    using asyncdataloader::config::PipelineConfig;
    using asyncdataloader::pipeline::SPSCQueue;

    try {
        [[maybe_unused]] SPSCQueue<int> invalid_queue(0);
        return fail("zero capacity was accepted");
    } catch (const std::invalid_argument&) {
    }

    SPSCQueue<int> fifo_queue(2);
    fifo_queue.push(10);
    fifo_queue.push(20);
    if (fifo_queue.size() != 2 || fifo_queue.pop() != 10) {
        return fail("queue did not preserve FIFO order");
    }
    fifo_queue.push(30);
    if (fifo_queue.pop() != 20 || fifo_queue.pop() != 30 ||
        fifo_queue.size() != 0) {
        return fail("ring-buffer wraparound lost FIFO order");
    }

    SPSCQueue<int> empty_queue(1);
    std::promise<void> consumer_started;
    auto consumer_started_future = consumer_started.get_future();
    auto blocked_pop = std::async(
        std::launch::async,
        [&empty_queue, &consumer_started] {
            consumer_started.set_value();
            return empty_queue.pop();
        }
    );

    consumer_started_future.wait();
    if (blocked_pop.wait_for(50ms) != std::future_status::timeout) {
        [[maybe_unused]] const int unexpected_value = blocked_pop.get();
        return fail("pop did not wait while the queue was empty");
    }
    empty_queue.push(42);
    if (blocked_pop.get() != 42) {
        return fail("a waiting consumer received the wrong value");
    }

    SPSCQueue<int> full_queue(1);
    full_queue.push(11);
    std::promise<void> producer_started;
    auto producer_started_future = producer_started.get_future();
    auto blocked_push = std::async(
        std::launch::async,
        [&full_queue, &producer_started] {
            producer_started.set_value();
            full_queue.push(22);
        }
    );

    producer_started_future.wait();
    if (blocked_push.wait_for(50ms) != std::future_status::timeout) {
        blocked_push.get();
        return fail("push did not wait while the queue was full");
    }
    if (full_queue.pop() != 11) {
        return fail("full queue lost its first value");
    }
    blocked_push.get();
    if (full_queue.pop() != 22) {
        return fail("waiting producer's value was not enqueued");
    }

    PipelineConfig config;
    config.block_size = 4096;
    config.max_inflight_buffers = 1;
    config.queue_depth = 1;
    config.buffer_alignment = 4096;

    AlignedBufferPool pool(config);
    {
        SPSCQueue<BufferHandle> handle_queue(config.queue_depth);
        BufferHandle produced = pool.acquire();
        std::byte* const original_address = produced.data();

        handle_queue.push(std::move(produced));
        if (produced.valid() || pool.available() != 0) {
            return fail("push did not transfer the buffer lease to the queue");
        }

        BufferHandle consumed = handle_queue.pop();
        if (!consumed.valid() || consumed.data() != original_address) {
            return fail("pop did not transfer the same buffer lease");
        }
        if (pool.available() != 0) {
            return fail("queued buffer was returned to the pool too early");
        }
    }

    if (pool.available() != 1) {
        return fail("consumed handle did not return its buffer by RAII");
    }

    return 0;
}
