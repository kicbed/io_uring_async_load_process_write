#include "buffer/aligned_buffer_pool.h"
#include "metrics/gauge.h"
#include "pipeline/block_work_item.h"
#include "pipeline/spsc_queue.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage10 queue lifecycle test failed: " << message << '\n';
    return 1;
}

bool has_runtime_error_message(
    const std::function<void()>& operation,
    std::string_view expected
) {
    try {
        operation();
    } catch (const std::runtime_error& error) {
        return error.what() == expected;
    }
    return false;
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    using asyncdataloader::buffer::AlignedBufferPool;
    using asyncdataloader::config::PipelineConfig;
    using asyncdataloader::pipeline::BlockWorkItem;
    using asyncdataloader::pipeline::SPSCQueue;

    SPSCQueue<int> draining_queue(2);
    if (!draining_queue.push(10) || !draining_queue.push(20)) {
        return fail("open queue rejected values before normal close");
    }
    draining_queue.close();
    draining_queue.close();
    if (!draining_queue.closed() || draining_queue.failed()) {
        return fail("normal close did not produce a non-failed closed state");
    }

    auto first = draining_queue.pop();
    auto second = draining_queue.pop();
    auto end = draining_queue.pop();
    if (!first.has_value() || *first != 10 || !second.has_value() ||
        *second != 20 || end.has_value()) {
        return fail("normal close did not drain FIFO values before EOF");
    }
    if (draining_queue.push(30)) {
        return fail("normal close accepted a new value");
    }

    SPSCQueue<int> empty_queue(1);
    std::promise<void> pop_started;
    auto pop_started_future = pop_started.get_future();
    auto blocked_pop = std::async(
        std::launch::async,
        [&empty_queue, &pop_started] {
            pop_started.set_value();
            return empty_queue.pop();
        }
    );
    pop_started_future.wait();
    if (blocked_pop.wait_for(50ms) != std::future_status::timeout) {
        [[maybe_unused]] const auto unexpected = blocked_pop.get();
        return fail("empty pop did not block before close");
    }
    empty_queue.close();
    if (blocked_pop.get().has_value()) {
        return fail("normal close did not wake empty pop with EOF");
    }

    SPSCQueue<int> full_queue(1);
    if (!full_queue.push(11)) {
        return fail("open queue rejected the value used to fill it");
    }
    std::promise<void> push_started;
    auto push_started_future = push_started.get_future();
    auto blocked_push = std::async(
        std::launch::async,
        [&full_queue, &push_started] {
            push_started.set_value();
            return full_queue.push(22);
        }
    );
    push_started_future.wait();
    if (blocked_push.wait_for(50ms) != std::future_status::timeout) {
        [[maybe_unused]] const bool unexpected = blocked_push.get();
        return fail("full push did not block before close");
    }
    full_queue.close();
    if (blocked_push.get()) {
        return fail("normal close allowed a blocked producer to enqueue");
    }
    auto retained = full_queue.pop();
    if (!retained.has_value() || *retained != 11 ||
        full_queue.pop().has_value()) {
        return fail("normal close did not retain and drain the queued value");
    }

    SPSCQueue<int> failed_queue(1);
    std::promise<void> failed_pop_started;
    auto failed_pop_started_future = failed_pop_started.get_future();
    auto failed_pop = std::async(
        std::launch::async,
        [&failed_queue, &failed_pop_started] {
            failed_pop_started.set_value();
            return failed_queue.pop();
        }
    );
    failed_pop_started_future.wait();
    if (failed_pop.wait_for(50ms) != std::future_status::timeout) {
        [[maybe_unused]] const auto unexpected = failed_pop.get();
        return fail("empty pop did not block before failure");
    }

    constexpr std::string_view first_failure{"reader failed"};
    failed_queue.fail(
        std::make_exception_ptr(std::runtime_error(std::string(first_failure)))
    );
    failed_queue.fail(
        std::make_exception_ptr(std::runtime_error("later failure"))
    );
    if (!failed_queue.closed() || !failed_queue.failed()) {
        return fail("failure did not close and mark the queue failed");
    }
    if (!has_runtime_error_message(
            [&failed_pop] {
                [[maybe_unused]] const auto value = failed_pop.get();
            },
            first_failure
        )) {
        return fail("blocked pop did not receive the first failure");
    }
    if (!has_runtime_error_message(
            [&failed_queue] {
                [[maybe_unused]] const bool pushed = failed_queue.push(7);
            },
            first_failure
        )) {
        return fail("push did not rethrow the stored failure");
    }
    if (!has_runtime_error_message(
            [&failed_queue] {
                [[maybe_unused]] const auto value = failed_queue.pop();
            },
            first_failure
        )) {
        return fail("pop did not preserve the first failure");
    }

    SPSCQueue<int> null_failure_queue(1);
    bool null_failure_rejected = false;
    try {
        null_failure_queue.fail({});
    } catch (const std::invalid_argument&) {
        null_failure_rejected = true;
    }
    if (!null_failure_rejected || null_failure_queue.closed()) {
        return fail("null failure was accepted or changed queue state");
    }

    asyncdataloader::metrics::Gauge observed_depth;
    SPSCQueue<int> observed_queue(2, observed_depth);
    if (!observed_queue.push(1) || !observed_queue.push(2) ||
        observed_depth.value() != 2 ||
        observed_depth.high_watermark() != 2) {
        return fail("queue push did not update its depth Gauge");
    }
    [[maybe_unused]] auto observed_value = observed_queue.pop();
    if (observed_depth.value() != 1 ||
        observed_depth.high_watermark() != 2) {
        return fail("queue pop changed current/peak depth incorrectly");
    }
    observed_queue.fail(std::make_exception_ptr(
        std::runtime_error("observed queue failed")
    ));
    if (observed_depth.value() != 0 ||
        observed_depth.high_watermark() != 2) {
        return fail("queue failure left a stale current depth");
    }

    asyncdataloader::metrics::Gauge dirty_depth;
    dirty_depth.increment();
    bool dirty_depth_rejected = false;
    try {
        SPSCQueue<int> invalid_observed_queue(1, dirty_depth);
    } catch (const std::invalid_argument&) {
        dirty_depth_rejected = true;
    }
    if (!dirty_depth_rejected) {
        return fail("queue accepted a reused nonzero depth Gauge");
    }

    PipelineConfig config;
    config.block_size = 4096;
    config.max_inflight_buffers = 2;
    config.queue_depth = 1;
    config.buffer_alignment = 4096;

    AlignedBufferPool pool(config);
    SPSCQueue<BlockWorkItem> work_queue(config.queue_depth);
    if (!work_queue.push(BlockWorkItem{0, 0, 16, pool.acquire()})) {
        return fail("open work queue rejected its first block");
    }

    std::promise<void> work_push_started;
    auto work_push_started_future = work_push_started.get_future();
    auto blocked_work_push = std::async(
        std::launch::async,
        [
            &work_queue,
            &work_push_started,
            item = BlockWorkItem{1, 4096, 8, pool.acquire()}
        ]() mutable {
            work_push_started.set_value();
            return work_queue.push(std::move(item));
        }
    );
    work_push_started_future.wait();
    if (blocked_work_push.wait_for(50ms) != std::future_status::timeout) {
        [[maybe_unused]] const bool unexpected = blocked_work_push.get();
        return fail("full work queue did not apply backpressure");
    }
    if (pool.available() != 0) {
        return fail("blocked work items did not retain both buffer leases");
    }

    constexpr std::string_view process_failure{"processor failed"};
    work_queue.fail(std::make_exception_ptr(
        std::runtime_error(std::string(process_failure))
    ));
    if (!has_runtime_error_message(
            [&blocked_work_push] {
                [[maybe_unused]] const bool pushed = blocked_work_push.get();
            },
            process_failure
        )) {
        return fail("blocked work push did not receive pipeline failure");
    }
    if (pool.available() != pool.capacity() || work_queue.size() != 0) {
        return fail("failure did not release queued and blocked buffer leases");
    }

    return 0;
}
