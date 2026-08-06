#include "buffer/aligned_buffer_pool.h"
#include "buffer/buffer_handle.h"
#include "config/pipeline_config.h"
#include "metrics/histogram.h"
#include "metrics/metrics_registry.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage9 metrics snapshot test failed: " << message << '\n';
    return 1;
}

}  // namespace

int main() {
    using asyncdataloader::buffer::AlignedBufferPool;
    using asyncdataloader::buffer::BufferHandle;
    using asyncdataloader::config::PipelineConfig;
    using asyncdataloader::metrics::Histogram;
    using asyncdataloader::metrics::MetricsRegistry;

    MetricsRegistry metrics;
    auto& processed_blocks = metrics.add_counter("processed_blocks");
    auto& inflight_buffers =
        metrics.add_gauge("buffer_pool.inflight_buffers");
    constexpr std::array<Histogram::Value, 2> latency_bounds{10, 100};
    auto& process_latency =
        metrics.add_histogram("process_latency_ns", latency_bounds);

    PipelineConfig config;
    config.block_size = 4096;
    config.max_inflight_buffers = 2;
    config.queue_depth = 1;
    config.buffer_alignment = 4096;

    AlignedBufferPool pool(config, inflight_buffers);
    if (inflight_buffers.value() != 0 ||
        inflight_buffers.high_watermark() != 0) {
        return fail("an unused instrumented pool did not start at zero");
    }

    {
        std::optional<BufferHandle> first{pool.acquire()};
        std::optional<BufferHandle> second{pool.acquire()};
        if (inflight_buffers.value() != 2 ||
            inflight_buffers.high_watermark() != 2 ||
            pool.try_acquire().has_value()) {
            return fail("successful and failed acquires updated Gauge incorrectly");
        }

        BufferHandle moved(std::move(*first));
        if (inflight_buffers.value() != 2 || first->valid()) {
            return fail("moving a lease changed the inflight count");
        }

        second.reset();
        if (inflight_buffers.value() != 1) {
            return fail("returning one lease did not decrement the Gauge");
        }
    }

    if (inflight_buffers.value() != 0 ||
        inflight_buffers.high_watermark() != 2 ||
        pool.available() != pool.capacity()) {
        return fail("RAII return did not restore the current inflight count");
    }

    processed_blocks.increment(2);
    process_latency.observe(50);

    const MetricsRegistry::Snapshot snapshot = metrics.snapshot();
    if (snapshot.counters.size() != 1 ||
        snapshot.counters[0].name != "processed_blocks" ||
        snapshot.counters[0].value != 2) {
        return fail("Counter snapshot is incorrect");
    }
    if (snapshot.gauges.size() != 1 ||
        snapshot.gauges[0].name != "buffer_pool.inflight_buffers" ||
        snapshot.gauges[0].value != 0 ||
        snapshot.gauges[0].high_watermark != 2) {
        return fail("Gauge snapshot is incorrect");
    }
    if (snapshot.histograms.size() != 1 ||
        snapshot.histograms[0].name != "process_latency_ns" ||
        snapshot.histograms[0].upper_bound_count != 2 ||
        snapshot.histograms[0].upper_bounds[0] != 10 ||
        snapshot.histograms[0].upper_bounds[1] != 100 ||
        snapshot.histograms[0].data.sample_count != 1 ||
        snapshot.histograms[0].data.sample_sum != 50 ||
        snapshot.histograms[0].data.bucket_counts[1] != 1) {
        return fail("Histogram snapshot is incorrect");
    }

    processed_blocks.increment();
    if (snapshot.counters[0].value != 2 || processed_blocks.value() != 3) {
        return fail("a snapshot changed after its source metric was updated");
    }

    asyncdataloader::metrics::Gauge dirty_gauge;
    dirty_gauge.set(1);
    bool dirty_gauge_rejected = false;
    try {
        AlignedBufferPool invalid_pool(config, dirty_gauge);
    } catch (const std::invalid_argument&) {
        dirty_gauge_rejected = true;
    }
    if (!dirty_gauge_rejected) {
        return fail("the pool accepted a nonzero shared inflight Gauge");
    }

    return 0;
}
