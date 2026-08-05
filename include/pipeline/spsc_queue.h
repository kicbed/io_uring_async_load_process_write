#pragma once

#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace asyncdataloader::pipeline {

// This first implementation favors clarity and correctness over lock-free
// optimization. Use exactly one producer and one consumer, and stop/join both
// threads before destroying the queue.
template <typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(std::size_t capacity) : slots_(capacity) {
        if (capacity == 0) {
            throw std::invalid_argument(
                "SPSCQueue capacity must be greater than zero"
            );
        }
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    void push(T value) {
        std::unique_lock lock(mutex_);
        not_full_cv_.wait(lock, [this] {
            return size_ < slots_.size();
        });

        assert(!slots_[tail_].has_value());
        slots_[tail_].emplace(std::move(value));
        tail_ = next_index(tail_);
        ++size_;

        lock.unlock();
        not_empty_cv_.notify_one();
    }

    [[nodiscard]] T pop() {
        std::unique_lock lock(mutex_);
        not_empty_cv_.wait(lock, [this] {
            return size_ > 0;
        });

        assert(slots_[head_].has_value());
        T value = std::move(*slots_[head_]);
        slots_[head_].reset();
        head_ = next_index(head_);
        --size_;

        lock.unlock();
        not_full_cv_.notify_one();
        return value;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return slots_.size();
    }

    [[nodiscard]] std::size_t size() const {
        const std::lock_guard lock(mutex_);
        return size_;
    }

private:
    [[nodiscard]] std::size_t next_index(std::size_t index) const noexcept {
        return (index + 1) % slots_.size();
    }

    std::vector<std::optional<T>> slots_;
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;
};

}  // namespace asyncdataloader::pipeline
