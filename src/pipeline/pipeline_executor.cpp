#include "pipeline/pipeline_executor.h"

#include "backend/io_backend.h"
#include "buffer/aligned_buffer_pool.h"
#include "metrics/counter.h"
#include "metrics/gauge.h"
#include "metrics/histogram.h"
#include "metrics/metrics_registry.h"
#include "metrics/scoped_timer.h"
#include "pipeline/block_work_item.h"
#include "pipeline/pipeline.h"
#include "pipeline/spsc_queue.h"
#include "util/fd_guard.h"
#include "util/file_io.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace asyncdataloader::pipeline {
namespace {

using WorkQueue = SPSCQueue<BlockWorkItem>;

constexpr std::array<metrics::Histogram::Value, 7>
    kPipelineLatencyUpperBoundsNs{
        1'000,
        10'000,
        100'000,
        1'000'000,
        10'000'000,
        100'000'000,
        1'000'000'000,
    };

void validate_path(
    const std::filesystem::path& path,
    const char* empty_message,
    const char* nul_message
) {
    if (path.empty()) {
        throw std::invalid_argument(empty_message);
    }
    if (path.native().find('\0') != std::string::npos) {
        throw std::invalid_argument(nul_message);
    }
}

class AtomicOutputFile {
public:
    explicit AtomicOutputFile(std::filesystem::path final_path)
        : final_path_(std::move(final_path)) {
        validate_path(
            final_path_,
            "pipeline output path must not be empty",
            "pipeline output path must not contain a null byte"
        );

        const std::filesystem::path filename = final_path_.filename();
        if (filename.empty() || filename == "." || filename == "..") {
            throw std::invalid_argument(
                "pipeline output path must name a file"
            );
        }

        std::filesystem::path parent = final_path_.parent_path();
        if (parent.empty()) {
            parent = ".";
        }

        const int raw_parent_fd = ::open(
            parent.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC
        );
        if (raw_parent_fd < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "open pipeline output directory"
            );
        }
        parent_directory_fd_ = util::FdGuard{raw_parent_fd};

        const std::filesystem::path template_path =
            parent / ("." + filename.string() + ".tmp.XXXXXX");
        std::string mutable_template = template_path.string();
        mutable_template.push_back('\0');

        const int raw_fd = ::mkstemp(mutable_template.data());
        if (raw_fd < 0) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "create pipeline temporary output"
            );
        }

        util::FdGuard created_fd{raw_fd};
        try {
            temporary_path_ = std::filesystem::path{mutable_template.data()};
        } catch (...) {
            static_cast<void>(::unlink(mutable_template.data()));
            throw;
        }
        fd_ = std::move(created_fd);
    }

    ~AtomicOutputFile() noexcept {
        if (!committed_ && !temporary_path_.empty()) {
            static_cast<void>(::unlink(temporary_path_.c_str()));
        }
    }

    AtomicOutputFile(const AtomicOutputFile&) = delete;
    AtomicOutputFile& operator=(const AtomicOutputFile&) = delete;
    AtomicOutputFile(AtomicOutputFile&&) = delete;
    AtomicOutputFile& operator=(AtomicOutputFile&&) = delete;

    [[nodiscard]] int fd() const noexcept {
        return fd_.get();
    }

    void commit() {
        if (committed_) {
            throw std::logic_error("pipeline output was already committed");
        }

        const util::FsyncResult fsync_result = util::fsync_fd(fd_.get());
        if (fsync_result.error_number != 0) {
            throw std::system_error(
                fsync_result.error_number,
                std::generic_category(),
                "fsync pipeline temporary output"
            );
        }

        if (::rename(temporary_path_.c_str(), final_path_.c_str()) != 0) {
            const int error_number = errno;
            throw std::system_error(
                error_number,
                std::generic_category(),
                "rename pipeline temporary output"
            );
        }
        committed_ = true;

        const util::FsyncResult directory_fsync_result =
            util::fsync_fd(parent_directory_fd_.get());
        if (directory_fsync_result.error_number != 0) {
            throw std::system_error(
                directory_fsync_result.error_number,
                std::generic_category(),
                "fsync pipeline output directory"
            );
        }
    }

private:
    std::filesystem::path final_path_;
    std::filesystem::path temporary_path_;
    util::FdGuard fd_;
    util::FdGuard parent_directory_fd_;
    bool committed_{false};
};

void reject_same_input_and_output(
    int input_fd,
    const std::filesystem::path& final_output_path
) {
    struct stat input_status {};
    if (::fstat(input_fd, &input_status) != 0) {
        throw std::system_error(
            errno,
            std::generic_category(),
            "stat pipeline input"
        );
    }

    struct stat output_status {};
    if (::stat(final_output_path.c_str(), &output_status) != 0) {
        const int error_number = errno;
        if (error_number == ENOENT) {
            return;
        }
        throw std::system_error(
            error_number,
            std::generic_category(),
            "stat pipeline output"
        );
    }

    if (input_status.st_dev == output_status.st_dev &&
        input_status.st_ino == output_status.st_ino) {
        throw std::invalid_argument(
            "pipeline input and final output must be different files"
        );
    }
}

std::uint64_t size_to_u64(std::size_t value) {
    if constexpr (sizeof(std::size_t) > sizeof(std::uint64_t)) {
        if (value >
            static_cast<std::size_t>(
                std::numeric_limits<std::uint64_t>::max()
            )) {
            throw std::overflow_error("pipeline byte count exceeds uint64_t");
        }
    }
    return static_cast<std::uint64_t>(value);
}

std::uint64_t checked_add(
    std::uint64_t left,
    std::uint64_t right,
    const char* message
) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error(message);
    }
    return left + right;
}

std::size_t complete_read(
    backend::IOBackend& read_backend,
    int input_fd,
    std::span<std::byte> buffer,
    std::uint64_t file_offset
) {
    auto read_task = read_backend.read_at(input_fd, buffer, file_offset);
    read_task.start();
    while (!read_task.done()) {
        read_backend.wait_one();
    }
    return read_task.result();
}

void reader_loop(
    int input_fd,
    backend::IOBackend& read_backend,
    buffer::AlignedBufferPool& buffer_pool,
    WorkQueue& output,
    metrics::Counter* read_blocks,
    metrics::Counter* read_bytes,
    metrics::Histogram* read_latency
) {
    std::uint64_t block_index = 0;
    std::uint64_t file_offset = 0;

    while (true) {
        buffer::BufferHandle buffer = buffer_pool.acquire();
        std::size_t bytes_read = 0;
        {
            std::optional<metrics::ScopedTimer> timer;
            if (read_latency != nullptr) {
                timer.emplace(*read_latency);
            }
            bytes_read = complete_read(
                read_backend,
                input_fd,
                buffer.bytes(),
                file_offset
            );
        }

        if (bytes_read == 0) {
            output.close();
            return;
        }

        const std::uint64_t bytes_read_u64 = size_to_u64(bytes_read);
        if (read_blocks != nullptr) {
            read_blocks->increment();
            read_bytes->increment(bytes_read_u64);
        }
        const std::uint64_t next_file_offset = checked_add(
            file_offset,
            bytes_read_u64,
            "pipeline file offset overflow"
        );
        if (block_index == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("pipeline block index overflow");
        }
        const std::uint64_t next_block_index = block_index + 1;

        BlockWorkItem item{
            block_index,
            file_offset,
            bytes_read,
            std::move(buffer)
        };
        if (!output.push(std::move(item))) {
            throw std::logic_error(
                "read-to-process queue closed before reader reached EOF"
            );
        }

        block_index = next_block_index;
        file_offset = next_file_offset;
    }
}

void processor_loop(
    Pipeline& processing_pipeline,
    WorkQueue& input,
    WorkQueue& output,
    metrics::Counter* processed_blocks,
    metrics::Counter* processed_bytes,
    metrics::Histogram* process_latency
) {
    while (true) {
        std::optional<BlockWorkItem> item = input.pop();
        if (!item.has_value()) {
            output.close();
            return;
        }

        {
            std::optional<metrics::ScopedTimer> timer;
            if (process_latency != nullptr) {
                timer.emplace(*process_latency);
            }
            processing_pipeline.process(item->valid_data());
        }
        if (processed_blocks != nullptr) {
            processed_blocks->increment();
            processed_bytes->increment(size_to_u64(item->valid_bytes()));
        }
        if (!output.push(std::move(*item))) {
            throw std::logic_error(
                "process-to-write queue closed before processor finished"
            );
        }
    }
}

void writer_loop(
    int output_fd,
    WorkQueue& input,
    PipelineRunResult& run_result,
    metrics::Counter* written_blocks,
    metrics::Counter* written_bytes,
    metrics::Histogram* write_latency
) {
    while (true) {
        std::optional<BlockWorkItem> item = input.pop();
        if (!item.has_value()) {
            return;
        }

        const auto bytes = item->valid_data();
        util::WriteAtResult write_result;
        {
            std::optional<metrics::ScopedTimer> timer;
            if (write_latency != nullptr) {
                timer.emplace(*write_latency);
            }
            write_result = util::write_all_at(
                output_fd,
                bytes.data(),
                bytes.size(),
                item->file_offset()
            );
        }
        if (write_result.error_number != 0) {
            throw std::system_error(
                write_result.error_number,
                std::generic_category(),
                "pipeline writer pwrite"
            );
        }
        if (write_result.bytes_written != bytes.size()) {
            throw std::runtime_error(
                "write_all_at returned success without writing the full block"
            );
        }

        run_result.bytes_written = checked_add(
            run_result.bytes_written,
            size_to_u64(bytes.size()),
            "pipeline written-byte count overflow"
        );
        run_result.blocks_written = checked_add(
            run_result.blocks_written,
            1,
            "pipeline written-block count overflow"
        );
        if (written_blocks != nullptr) {
            written_blocks->increment();
            written_bytes->increment(size_to_u64(bytes.size()));
        }
        // item is destroyed here and its BufferHandle returns the lease.
    }
}

}  // namespace

PipelineExecutor::PipelineExecutor(
    config::PipelineConfig config,
    backend::IOBackend& read_backend,
    Pipeline& processing_pipeline
)
    : config_(std::move(config)),
      read_backend_(read_backend),
      processing_pipeline_(processing_pipeline) {
    config_.validate();
}

PipelineExecutor::PipelineExecutor(
    config::PipelineConfig config,
    backend::IOBackend& read_backend,
    Pipeline& processing_pipeline,
    metrics::MetricsRegistry& metrics
)
    : PipelineExecutor(
          std::move(config),
          read_backend,
          processing_pipeline
      ) {
    register_runtime_metrics(metrics);
}

void PipelineExecutor::register_runtime_metrics(
    metrics::MetricsRegistry& metrics
) {
    constexpr std::array<std::string_view, 12> names{
        PipelineMetricNames::read_blocks,
        PipelineMetricNames::read_bytes,
        PipelineMetricNames::processed_blocks,
        PipelineMetricNames::processed_bytes,
        PipelineMetricNames::written_blocks,
        PipelineMetricNames::written_bytes,
        PipelineMetricNames::inflight_buffers,
        PipelineMetricNames::read_process_queue_depth,
        PipelineMetricNames::process_write_queue_depth,
        PipelineMetricNames::read_latency_ns,
        PipelineMetricNames::process_latency_ns,
        PipelineMetricNames::write_latency_ns,
    };

    if (metrics.size() > metrics::MetricsRegistry::kMaxMetricCount -
            names.size()) {
        throw std::length_error(
            "MetricsRegistry has no room for pipeline runtime metrics"
        );
    }
    for (const std::string_view name : names) {
        if (metrics.find_counter(name) != nullptr ||
            metrics.find_gauge(name) != nullptr ||
            metrics.find_histogram(name) != nullptr) {
            throw std::invalid_argument(
                "pipeline runtime metric name is already registered"
            );
        }
    }

    metrics_.read_blocks =
        &metrics.add_counter(PipelineMetricNames::read_blocks);
    metrics_.read_bytes =
        &metrics.add_counter(PipelineMetricNames::read_bytes);
    metrics_.processed_blocks =
        &metrics.add_counter(PipelineMetricNames::processed_blocks);
    metrics_.processed_bytes =
        &metrics.add_counter(PipelineMetricNames::processed_bytes);
    metrics_.written_blocks =
        &metrics.add_counter(PipelineMetricNames::written_blocks);
    metrics_.written_bytes =
        &metrics.add_counter(PipelineMetricNames::written_bytes);
    metrics_.inflight_buffers =
        &metrics.add_gauge(PipelineMetricNames::inflight_buffers);
    metrics_.read_process_queue_depth =
        &metrics.add_gauge(PipelineMetricNames::read_process_queue_depth);
    metrics_.process_write_queue_depth =
        &metrics.add_gauge(PipelineMetricNames::process_write_queue_depth);
    metrics_.read_latency_ns = &metrics.add_histogram(
        PipelineMetricNames::read_latency_ns,
        kPipelineLatencyUpperBoundsNs
    );
    metrics_.process_latency_ns = &metrics.add_histogram(
        PipelineMetricNames::process_latency_ns,
        kPipelineLatencyUpperBoundsNs
    );
    metrics_.write_latency_ns = &metrics.add_histogram(
        PipelineMetricNames::write_latency_ns,
        kPipelineLatencyUpperBoundsNs
    );
}

PipelineRunResult PipelineExecutor::run(int input_fd, int output_fd) {
    if (input_fd < 0 || output_fd < 0) {
        throw std::invalid_argument(
            "pipeline input and output descriptors must be valid"
        );
    }
    if (input_fd == output_fd) {
        throw std::invalid_argument(
            "pipeline input and output descriptors must be different"
        );
    }
    if (processing_pipeline_.stage_count() == 0) {
        throw std::invalid_argument(
            "pipeline executor requires at least one processing stage"
        );
    }
    if (started_.exchange(true)) {
        throw std::logic_error("PipelineExecutor may only run once");
    }

    // Declaration order is deliberate: worker threads die first, then queues,
    // and the pool dies last, after every queued handle has been released.
    std::unique_ptr<buffer::AlignedBufferPool> buffer_pool;
    if (metrics_.inflight_buffers == nullptr) {
        buffer_pool =
            std::make_unique<buffer::AlignedBufferPool>(config_);
    } else {
        buffer_pool = std::make_unique<buffer::AlignedBufferPool>(
            config_,
            *metrics_.inflight_buffers
        );
    }

    std::unique_ptr<WorkQueue> read_to_process;
    std::unique_ptr<WorkQueue> process_to_write;
    if (metrics_.read_process_queue_depth == nullptr) {
        read_to_process = std::make_unique<WorkQueue>(config_.queue_depth);
        process_to_write = std::make_unique<WorkQueue>(config_.queue_depth);
    } else {
        read_to_process = std::make_unique<WorkQueue>(
            config_.queue_depth,
            *metrics_.read_process_queue_depth
        );
        process_to_write = std::make_unique<WorkQueue>(
            config_.queue_depth,
            *metrics_.process_write_queue_depth
        );
    }
    PipelineRunResult run_result;

    std::mutex failure_mutex;
    std::exception_ptr first_failure;

    const auto fail_all = [&](std::exception_ptr failure) {
        std::exception_ptr canonical_failure;
        {
            const std::lock_guard lock(failure_mutex);
            if (!first_failure) {
                first_failure = std::move(failure);
            }
            canonical_failure = first_failure;
        }

        read_to_process->fail(canonical_failure);
        process_to_write->fail(canonical_failure);
    };

    const auto run_guarded = [&](auto&& operation) {
        try {
            std::forward<decltype(operation)>(operation)();
        } catch (...) {
            fail_all(std::current_exception());
        }
    };

    std::jthread reader_thread;
    std::jthread processor_thread;
    std::jthread writer_thread;

    const auto join_workers = [&] {
        if (reader_thread.joinable()) {
            reader_thread.join();
        }
        if (processor_thread.joinable()) {
            processor_thread.join();
        }
        if (writer_thread.joinable()) {
            writer_thread.join();
        }
    };

    try {
        // Start consumers first so a newly started producer always has a
        // downstream worker ready to drain bounded queues.
        writer_thread = std::jthread([&] {
            run_guarded([&] {
                writer_loop(
                    output_fd,
                    *process_to_write,
                    run_result,
                    metrics_.written_blocks,
                    metrics_.written_bytes,
                    metrics_.write_latency_ns
                );
            });
        });
        processor_thread = std::jthread([&] {
            run_guarded([&] {
                processor_loop(
                    processing_pipeline_,
                    *read_to_process,
                    *process_to_write,
                    metrics_.processed_blocks,
                    metrics_.processed_bytes,
                    metrics_.process_latency_ns
                );
            });
        });
        reader_thread = std::jthread([&] {
            run_guarded([&] {
                reader_loop(
                    input_fd,
                    read_backend_,
                    *buffer_pool,
                    *read_to_process,
                    metrics_.read_blocks,
                    metrics_.read_bytes,
                    metrics_.read_latency_ns
                );
            });
        });
    } catch (...) {
        fail_all(std::current_exception());
        join_workers();
        std::rethrow_exception(first_failure);
    }

    join_workers();
    if (first_failure) {
        std::rethrow_exception(first_failure);
    }
    return run_result;
}

PipelineRunResult PipelineExecutor::run_file(
    const std::filesystem::path& input_path,
    const std::filesystem::path& final_output_path
) {
    validate_path(
        input_path,
        "pipeline input path must not be empty",
        "pipeline input path must not contain a null byte"
    );
    validate_path(
        final_output_path,
        "pipeline output path must not be empty",
        "pipeline output path must not contain a null byte"
    );

    util::OpenFileResult input = util::open_read_only(input_path.c_str());
    if (input.error_number != 0) {
        throw std::system_error(
            input.error_number,
            std::generic_category(),
            "open pipeline input"
        );
    }

    reject_same_input_and_output(input.fd.get(), final_output_path);
    AtomicOutputFile output(final_output_path);

    PipelineRunResult result = run(input.fd.get(), output.fd());
    output.commit();
    return result;
}

}  // namespace asyncdataloader::pipeline
