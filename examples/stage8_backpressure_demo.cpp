#include "buffer/aligned_buffer_pool.h"
#include "pipeline/spsc_queue.h"

#include <chrono>
#include <cstddef>
#include <future>
#include <iostream>
#include <stdexcept>
#include <utility>

int main() {
    using namespace std::chrono_literals;
    using asyncdataloader::buffer::AlignedBufferPool;
    using asyncdataloader::buffer::BufferHandle;
    using asyncdataloader::config::PipelineConfig;
    using asyncdataloader::pipeline::SPSCQueue;

    PipelineConfig config;
    config.block_size = 4096;
    config.max_inflight_buffers = 2;
    config.queue_depth = 1;
    config.buffer_alignment = 4096;

    AlignedBufferPool pool(config);
    SPSCQueue<BufferHandle> queue(config.queue_depth);

    std::promise<void> first_queued;
    std::promise<void> second_push_started;
    auto first_queued_future = first_queued.get_future();
    auto second_push_started_future = second_push_started.get_future();

    auto producer = std::async(
        std::launch::async,
        [&pool, &queue, &first_queued, &second_push_started] {
            auto first = pool.acquire();
            first.bytes().front() = static_cast<std::byte>(1);
            if (!queue.push(std::move(first))) {
                throw std::runtime_error("queue closed during first push");
            }
            first_queued.set_value();

            auto second = pool.acquire();
            second.bytes().front() = static_cast<std::byte>(2);
            second_push_started.set_value();
            if (!queue.push(std::move(second))) {
                throw std::runtime_error("queue closed during second push");
            }
        }
    );

    first_queued_future.wait();
    second_push_started_future.wait();

    const bool backpressure_observed =
        producer.wait_for(50ms) == std::future_status::timeout;

    unsigned int first_marker = 0;
    {
        auto first = queue.pop();
        if (!first.has_value()) {
            std::cerr << "queue closed before first marker\n";
            return 1;
        }
        first_marker = std::to_integer<unsigned int>(first->bytes().front());
    }

    producer.get();

    unsigned int second_marker = 0;
    {
        auto second = queue.pop();
        if (!second.has_value()) {
            std::cerr << "queue closed before second marker\n";
            return 1;
        }
        second_marker = std::to_integer<unsigned int>(second->bytes().front());
    }

    const std::size_t returned_buffers = pool.available();

    std::cout << "pool_capacity=" << pool.capacity() << '\n';
    std::cout << "queue_capacity=" << queue.capacity() << '\n';
    std::cout << "producer_blocked_while_queue_full="
              << (backpressure_observed ? "true" : "false") << '\n';
    std::cout << "consumed_markers=" << first_marker << ',' << second_marker
              << '\n';
    std::cout << "buffers_returned=" << returned_buffers << '\n';

    if (!backpressure_observed || first_marker != 1U || second_marker != 2U ||
        returned_buffers != pool.capacity()) {
        std::cerr << "backpressure demo invariant failed\n";
        return 1;
    }

    return 0;
}
