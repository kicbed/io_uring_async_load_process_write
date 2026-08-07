#pragma once

#include "metrics/gauge.h"

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace asyncdataloader::pipeline {

// This implementation favors clarity and correctness over lock-free
// optimization. Use exactly one data producer and one data consumer. close()
// and fail() may be used to wake either side, but all threads must still be
// stopped and joined before destroying the queue.
template <typename T>
class SPSCQueue {
public:
    static_assert(
        std::is_nothrow_move_constructible_v<T>,
        "SPSCQueue requires nothrow-move-constructible values"
    );
    static_assert(
        std::is_nothrow_destructible_v<T>,
        "SPSCQueue requires nothrow-destructible values"
    );

    explicit SPSCQueue(std::size_t capacity) : slots_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument(
                "SPSCQueue capacity must be greater than zero"
            );
        }
    }

    // The Gauge is borrowed and must outlive this queue. It observes queue
    // occupancy only; the mutex and condition variables still synchronize
    // work-item ownership.
    SPSCQueue(std::size_t capacity, metrics::Gauge& depth)
        : SPSCQueue(capacity) {
        if (depth.value() != 0 || depth.high_watermark() != 0) {
            throw std::invalid_argument(
                "SPSCQueue depth Gauge must be dedicated and start at zero"
            );
        }
        depth_metric_ = &depth;
    }

    ~SPSCQueue() noexcept {
        // Destruction requires all producer/consumer threads to have stopped.
        // Resetting here keeps an observed queue from reporting stale depth if
        // a caller intentionally destroys a normally undrained queue.
        if (depth_metric_ != nullptr) {
            depth_metric_->set(0);
        }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // Returns false when normal close wins before the value is enqueued. The
    // by-value parameter owns the item while waiting, so rejection destroys it
    // and releases any RAII resources. A failed queue rethrows the original
    // cross-thread exception.
    [[nodiscard]] bool push(T value) {
        std::unique_lock lock(mutex_);
        not_full_cv_.wait(lock, [this] {
            return state_ != State::open || size_ < slots_.size();
        });

        if (state_ == State::failed) {
            const std::exception_ptr failure = failure_;
            lock.unlock();
            std::rethrow_exception(failure);
        }
        if (state_ == State::closed) {
            return false;
        }

        assert(!slots_[tail_].has_value());
        slots_[tail_].emplace(std::move(value));
        tail_ = next_index(tail_);
        ++size_;
        if (depth_metric_ != nullptr) {
            depth_metric_->increment();
        }

        lock.unlock();
        not_empty_cv_.notify_one();
        return true;
    }

    // Returns nullopt only after a normal close and after all queued values
    // have been drained. A failed queue rethrows its stored exception.
    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        not_empty_cv_.wait(lock, [this] {
            return state_ != State::open || size_ > 0;
        });

        if (state_ == State::failed) {
            const std::exception_ptr failure = failure_;
            lock.unlock();
            std::rethrow_exception(failure);
        }
        if (size_ == 0) {
            assert(state_ == State::closed);
            return std::nullopt;
        }

        assert(slots_[head_].has_value());
        std::optional<T> value;
        value.emplace(std::move(*slots_[head_]));
        slots_[head_].reset();
        head_ = next_index(head_);
        --size_;
        if (depth_metric_ != nullptr) {
            depth_metric_->decrement();
        }

        lock.unlock();
        not_full_cv_.notify_one();
        return value;
    }

    // Normal close preserves queued values. Consumers drain them before pop()
    // returns nullopt. Repeated close calls are harmless.
    void close() {
        {
            const std::lock_guard lock(mutex_);
            if (state_ == State::open) {
                state_ = State::closed;
            }
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    // Failure stops delivery immediately, destroys queued values, and keeps
    // the first exception for both producer and consumer. Destruction of a
    // queued BlockWorkItem returns its BufferHandle lease through RAII.
    void fail(std::exception_ptr failure) {
        if (!failure) {
            throw std::invalid_argument(
                "SPSCQueue failure requires a non-null exception"
            );
        }

        {
            const std::lock_guard lock(mutex_);
            if (state_ == State::failed) {
                return;
            }

            state_ = State::failed;
            failure_ = std::move(failure);
            discard_queued_values_locked();
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return slots_.size();
    }

    [[nodiscard]] std::size_t size() const {
        const std::lock_guard lock(mutex_);
        return size_;
    }

    [[nodiscard]] bool closed() const {
        const std::lock_guard lock(mutex_);
        return state_ != State::open;
    }

    [[nodiscard]] bool failed() const {
        const std::lock_guard lock(mutex_);
        return state_ == State::failed;
    }

private:
    enum class State {
        open,
        closed,
        failed,
    };

    [[nodiscard]] std::size_t next_index(std::size_t index) const noexcept {
        return (index + 1) % slots_.size();
    }

    void discard_queued_values_locked() noexcept {
        for (auto& slot : slots_) {
            slot.reset();
        }
        head_ = 0;
        tail_ = 0;
        size_ = 0;
        if (depth_metric_ != nullptr) {
            depth_metric_->set(0);
        }
    }

    std::vector<std::optional<T>> slots_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
    State state_{State::open};
    std::exception_ptr failure_;
    metrics::Gauge* depth_metric_{nullptr};
};

}  // namespace asyncdataloader::pipeline
