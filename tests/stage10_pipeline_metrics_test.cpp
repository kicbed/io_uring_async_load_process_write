#include "backend/sync_backend.h"
#include "config/pipeline_config.h"
#include "metrics/counter.h"
#include "metrics/gauge.h"
#include "metrics/histogram.h"
#include "metrics/metrics_registry.h"
#include "pipeline/builtin_stages.h"
#include "pipeline/pipeline.h"
#include "pipeline/pipeline_executor.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace {

using asyncdataloader::backend::SyncBackend;
using asyncdataloader::config::PipelineConfig;
using asyncdataloader::metrics::MetricsRegistry;
using asyncdataloader::pipeline::ByteIncrementStage;
using asyncdataloader::pipeline::Pipeline;
using asyncdataloader::pipeline::PipelineExecutor;
using asyncdataloader::pipeline::PipelineMetricNames;
using asyncdataloader::util::FdGuard;

int fail(std::string_view message) {
    std::cerr << "stage10 pipeline-metrics test failed: " << message << '\n';
    return 1;
}

FdGuard make_temp_file() {
    char path[] = "/tmp/asyncdataloader_stage10_metrics_XXXXXX";
    const int raw_fd = ::mkstemp(path);
    if (raw_fd < 0) {
        throw std::system_error(errno, std::generic_category(), "mkstemp");
    }
    static_cast<void>(::unlink(path));
    return FdGuard{raw_fd};
}

PipelineConfig make_config() {
    PipelineConfig config;
    config.block_size = 8;
    config.max_inflight_buffers = 3;
    config.queue_depth = 1;
    config.buffer_alignment = 8;
    return config;
}

const asyncdataloader::metrics::Counter& require_counter(
    const MetricsRegistry& metrics,
    std::string_view name
) {
    const auto* const metric = metrics.find_counter(name);
    if (metric == nullptr) {
        throw std::runtime_error("required Counter is missing");
    }
    return *metric;
}

const asyncdataloader::metrics::Gauge& require_gauge(
    const MetricsRegistry& metrics,
    std::string_view name
) {
    const auto* const metric = metrics.find_gauge(name);
    if (metric == nullptr) {
        throw std::runtime_error("required Gauge is missing");
    }
    return *metric;
}

const asyncdataloader::metrics::Histogram& require_histogram(
    const MetricsRegistry& metrics,
    std::string_view name
) {
    const auto* const metric = metrics.find_histogram(name);
    if (metric == nullptr) {
        throw std::runtime_error("required Histogram is missing");
    }
    return *metric;
}

}  // namespace

int main() {
    try {
        const std::array<std::byte, 19> input{
            std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3},
            std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40},
            std::byte{50}, std::byte{60}, std::byte{70}, std::byte{80},
            std::byte{90}, std::byte{100}, std::byte{110}, std::byte{120},
            std::byte{200}, std::byte{254}, std::byte{255},
        };

        FdGuard input_fd = make_temp_file();
        FdGuard output_fd = make_temp_file();
        const auto prepared = asyncdataloader::util::write_all_at(
            input_fd.get(),
            input.data(),
            input.size(),
            0
        );
        if (prepared.error_number != 0 ||
            prepared.bytes_written != input.size()) {
            return fail("could not prepare input");
        }

        MetricsRegistry metrics;
        Pipeline processing(metrics);
        processing.add_stage(std::make_unique<ByteIncrementStage>());
        SyncBackend backend;
        PipelineExecutor executor(
            make_config(),
            backend,
            processing,
            metrics
        );

        const auto result = executor.run(input_fd.get(), output_fd.get());
        if (result.blocks_written != 3 || result.bytes_written != input.size()) {
            return fail("run result did not report three blocks/19 bytes");
        }

        constexpr std::array<std::string_view, 3> block_counters{
            PipelineMetricNames::read_blocks,
            PipelineMetricNames::processed_blocks,
            PipelineMetricNames::written_blocks,
        };
        for (const auto name : block_counters) {
            if (require_counter(metrics, name).value() != 3) {
                return fail("a stage block Counter did not reach three");
            }
        }

        constexpr std::array<std::string_view, 3> byte_counters{
            PipelineMetricNames::read_bytes,
            PipelineMetricNames::processed_bytes,
            PipelineMetricNames::written_bytes,
        };
        for (const auto name : byte_counters) {
            if (require_counter(metrics, name).value() != input.size()) {
                return fail("a stage byte Counter did not reach 19");
            }
        }

        const auto& inflight = require_gauge(
            metrics,
            PipelineMetricNames::inflight_buffers
        );
        if (inflight.value() != 0 || inflight.high_watermark() == 0 ||
            inflight.high_watermark() > 3) {
            return fail("inflight Gauge escaped its configured bound");
        }

        constexpr std::array<std::string_view, 2> queue_gauges{
            PipelineMetricNames::read_process_queue_depth,
            PipelineMetricNames::process_write_queue_depth,
        };
        for (const auto name : queue_gauges) {
            const auto& gauge = require_gauge(metrics, name);
            if (gauge.value() != 0 || gauge.high_watermark() != 1) {
                return fail("queue Gauge did not finish empty within capacity");
            }
        }

        if (require_histogram(
                metrics,
                PipelineMetricNames::read_latency_ns
            ).snapshot().sample_count != 4) {
            return fail("read latency did not include three blocks plus EOF");
        }
        if (require_histogram(
                metrics,
                PipelineMetricNames::process_latency_ns
            ).snapshot().sample_count != 3 ||
            require_histogram(
                metrics,
                PipelineMetricNames::write_latency_ns
            ).snapshot().sample_count != 3 ||
            require_histogram(
                metrics,
                "stage.0.byte_increment.latency_ns"
            ).snapshot().sample_count != 3) {
            return fail("process/write/stage latency sample counts are wrong");
        }

        std::array<std::byte, 19> output{};
        const auto read_back = asyncdataloader::util::read_at(
            output_fd.get(),
            output.data(),
            output.size(),
            0
        );
        if (read_back.error_number != 0 ||
            read_back.bytes_read != output.size()) {
            return fail("could not read transformed output");
        }
        for (std::size_t index = 0; index < input.size(); ++index) {
            const unsigned int expected =
                std::to_integer<unsigned int>(input[index]) + 1U;
            if (output[index] !=
                std::byte{static_cast<unsigned char>(expected)}) {
                return fail("ByteIncrementStage output is incorrect");
            }
        }

        bool second_run_rejected = false;
        try {
            [[maybe_unused]] const auto second =
                executor.run(input_fd.get(), output_fd.get());
        } catch (const std::logic_error&) {
            second_run_rejected = true;
        }
        if (!second_run_rejected) {
            return fail("single-use executor accepted a second run");
        }
    } catch (const std::exception& error) {
        std::cerr << "stage10 pipeline-metrics test setup failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
