#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace asyncdataloader::pipeline {

class Stage {
public:
    Stage() = default;
    virtual ~Stage() = default;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;
    Stage(Stage&&) = delete;
    Stage& operator=(Stage&&) = delete;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // The caller owns the block. A Stage may modify the borrowed bytes during
    // this call, but it must not retain the span or its data pointer.
    virtual void process(std::span<std::byte> block) = 0;
};

}  // namespace asyncdataloader::pipeline
