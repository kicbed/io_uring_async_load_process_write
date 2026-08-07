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
    if (!fifo_queue.push(10) || !fifo_queue.push(20)) {
        return fail("open queue rejected a FIFO value");
    }
    auto first_fifo = fifo_queue.pop();
    if (fifo_queue.size() != 1 || !first_fifo.has_value() ||
        *first_fifo != 10) {
        return fail("queue did not preserve FIFO order");
    }
    if (!fifo_queue.push(30)) {
        return fail("open queue rejected a wraparound value");
    }
    auto second_fifo = fifo_queue.pop();
    auto third_fifo = fifo_queue.pop();
    if (!second_fifo.has_value() || *second_fifo != 20 ||
        !third_fifo.has_value() || *third_fifo != 30 ||
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
        [[maybe_unused]] const auto unexpected_value = blocked_pop.get();
        return fail("pop did not wait while the queue was empty");
    }
    if (!empty_queue.push(42)) {
        return fail("open queue rejected a value for a waiting consumer");
    }
    auto waited_value = blocked_pop.get();
    if (!waited_value.has_value() || *waited_value != 42) {
        return fail("a waiting consumer received the wrong value");
    }

    SPSCQueue<int> full_queue(1);
    if (!full_queue.push(11)) {
        return fail("open queue rejected its first value");
    }
    std::promise<void> producer_started;
    auto producer_started_future = producer_started.get_future();
    auto blocked_push = std::async(
        std::launch::async,
        [&full_queue, &producer_started] {
            producer_started.set_value();
            return full_queue.push(22);
        }
    );

    producer_started_future.wait();
    if (blocked_push.wait_for(50ms) != std::future_status::timeout) {
        blocked_push.get();
        return fail("push did not wait while the queue was full");
    }
    auto first_full = full_queue.pop();
    if (!first_full.has_value() || *first_full != 11) {
        return fail("full queue lost its first value");
    }
    if (!blocked_push.get()) {
        return fail("waiting producer was rejected while queue remained open");
    }
    auto second_full = full_queue.pop();
    if (!second_full.has_value() || *second_full != 22) {
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

        if (!handle_queue.push(std::move(produced))) {
            return fail("open queue rejected a BufferHandle");
        }
        if (produced.valid() || pool.available() != 0) {
            return fail("push did not transfer the buffer lease to the queue");
        }

        auto consumed = handle_queue.pop();
        if (!consumed.has_value() || !consumed->valid() ||
            consumed->data() != original_address) {
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
