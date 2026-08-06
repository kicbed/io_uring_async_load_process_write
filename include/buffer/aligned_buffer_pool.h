#pragma once

#include "buffer/aligned_buffer.h"
#include "buffer/buffer_handle.h"
#include "config/pipeline_config.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace asyncdataloader::metrics {
class Gauge;
}  // namespace asyncdataloader::metrics

namespace asyncdataloader::buffer {

// The pool must outlive every BufferHandle it creates. All threads using the
// pool must be stopped and joined before the pool is destroyed. An optional
// inflight Gauge is borrowed and must outlive the pool.
class AlignedBufferPool {
public:
    explicit AlignedBufferPool(const config::PipelineConfig& config);
    AlignedBufferPool(
        const config::PipelineConfig& config,
        metrics::Gauge& inflight_buffers
    );
    ~AlignedBufferPool() noexcept;

    AlignedBufferPool(const AlignedBufferPool&) = delete;
    AlignedBufferPool& operator=(const AlignedBufferPool&) = delete;
    AlignedBufferPool(AlignedBufferPool&&) = delete;
    AlignedBufferPool& operator=(AlignedBufferPool&&) = delete;

    [[nodiscard]] std::optional<BufferHandle> try_acquire();
    [[nodiscard]] BufferHandle acquire();
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t available() const;

private:
    friend class BufferHandle;

    [[nodiscard]] AlignedBuffer& buffer_at(std::size_t index) noexcept;
    [[nodiscard]] const AlignedBuffer& buffer_at(
        std::size_t index
    ) const noexcept;
    [[nodiscard]] BufferHandle take_available_locked() noexcept;
    void release(std::size_t index) noexcept;

    std::vector<AlignedBuffer> buffers_;
    std::vector<std::size_t> available_indices_;
    std::vector<std::uint8_t> in_use_;
    mutable std::mutex mutex_;
    std::condition_variable available_cv_;
    metrics::Gauge* inflight_metric_{nullptr};
};

}  // namespace asyncdataloader::buffer
