#include "pipeline/pipeline.h"

#include <stdexcept>
#include <utility>

namespace asyncdataloader::pipeline {

void Pipeline::add_stage(std::unique_ptr<Stage> stage) {
    if (!stage) {
        throw std::invalid_argument("pipeline stage must not be null");
    }

    stages_.push_back(std::move(stage));
}

void Pipeline::process(std::span<std::byte> block) {
    for (const auto& stage : stages_) {
        stage->process(block);
    }
}

std::size_t Pipeline::stage_count() const noexcept {
    return stages_.size();
}

}  // namespace asyncdataloader::pipeline
