#pragma once

#include "pipeline/stage.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace asyncdataloader::pipeline {

class Pipeline {
public:
    Pipeline() = default;
    ~Pipeline() = default;

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) noexcept = default;
    Pipeline& operator=(Pipeline&&) noexcept = default;

    // Transfers unique ownership to the Pipeline. Null stages are rejected.
    void add_stage(std::unique_ptr<Stage> stage);

    // Runs stages in registration order. If a stage throws, the exception is
    // propagated and later stages are not run.
    void process(std::span<std::byte> block);

    [[nodiscard]] std::size_t stage_count() const noexcept;

private:
    std::vector<std::unique_ptr<Stage>> stages_;
};

}  // namespace asyncdataloader::pipeline
