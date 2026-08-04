#include "pipeline/pipeline.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

class AffineStage final : public asyncdataloader::pipeline::Stage {
public:
    AffineStage(
        unsigned int multiplier,
        unsigned int offset,
        bool& destroyed
    )
        : multiplier_(multiplier),
          offset_(offset),
          destroyed_(&destroyed) {}

    ~AffineStage() override {
        *destroyed_ = true;
    }

    [[nodiscard]] std::string_view name() const noexcept override {
        return "affine";
    }

    void process(std::span<std::byte> block) override {
        for (std::byte& value : block) {
            const unsigned int input = std::to_integer<unsigned int>(value);
            const unsigned int output =
                (input * multiplier_ + offset_) & 0xFFU;
            value = static_cast<std::byte>(output);
        }
    }

private:
    unsigned int multiplier_;
    unsigned int offset_;
    bool* destroyed_;
};

class ProbeStage final : public asyncdataloader::pipeline::Stage {
public:
    ProbeStage(bool& called, bool should_throw)
        : called_(&called),
          should_throw_(should_throw) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "probe";
    }

    void process(std::span<std::byte>) override {
        *called_ = true;

        if (should_throw_) {
            throw std::runtime_error("expected stage failure");
        }
    }

private:
    bool* called_;
    bool should_throw_;
};

int fail(std::string_view message) {
    std::cerr << "stage7 pipeline registration test failed: " << message
              << '\n';
    return 1;
}

[[nodiscard]] unsigned int byte_value(std::byte value) {
    return std::to_integer<unsigned int>(value);
}

}  // namespace

int main() {
    using asyncdataloader::pipeline::Pipeline;
    using asyncdataloader::pipeline::Stage;

    static_assert(std::is_abstract_v<Stage>);
    static_assert(std::has_virtual_destructor_v<Stage>);
    static_assert(!std::is_copy_constructible_v<Stage>);
    static_assert(!std::is_move_constructible_v<Stage>);
    static_assert(!std::is_copy_constructible_v<Pipeline>);
    static_assert(std::is_move_constructible_v<Pipeline>);

    Pipeline invalid_pipeline;
    bool rejected_null_stage = false;
    try {
        invalid_pipeline.add_stage(std::unique_ptr<Stage>{});
    } catch (const std::invalid_argument&) {
        rejected_null_stage = true;
    }

    if (!rejected_null_stage) {
        return fail("add_stage() accepted a null owner");
    }
    if (invalid_pipeline.stage_count() != 0) {
        return fail("rejected registration changed the stage count");
    }

    bool add_stage_destroyed = false;
    bool multiply_stage_destroyed = false;

    {
        Pipeline pipeline;
        auto add_stage = std::make_unique<AffineStage>(
            1U,
            1U,
            add_stage_destroyed
        );

        if (add_stage->name() != "affine") {
            return fail("virtual name() dispatch returned the wrong name");
        }

        pipeline.add_stage(std::move(add_stage));
        pipeline.add_stage(
            std::make_unique<AffineStage>(2U, 0U, multiply_stage_destroyed)
        );

        if (pipeline.stage_count() != 2) {
            return fail("registered stage count is incorrect");
        }

        std::array<std::byte, 4> block{
            static_cast<std::byte>(1),
            static_cast<std::byte>(2),
            static_cast<std::byte>(3),
            static_cast<std::byte>(99),
        };

        pipeline.process(std::span<std::byte>{block}.first(3));

        if (byte_value(block[0]) != 4 ||
            byte_value(block[1]) != 6 ||
            byte_value(block[2]) != 8) {
            return fail("stages did not run in registration order");
        }
        if (byte_value(block[3]) != 99) {
            return fail("a stage modified bytes outside the borrowed span");
        }
        if (add_stage_destroyed || multiply_stage_destroyed) {
            return fail("Pipeline destroyed a registered stage too early");
        }
    }

    if (!add_stage_destroyed || !multiply_stage_destroyed) {
        return fail("Pipeline did not destroy its owned stages");
    }

    bool throwing_stage_called = false;
    bool later_stage_called = false;
    Pipeline failing_pipeline;
    failing_pipeline.add_stage(
        std::make_unique<ProbeStage>(throwing_stage_called, true)
    );
    failing_pipeline.add_stage(
        std::make_unique<ProbeStage>(later_stage_called, false)
    );

    bool stage_error_propagated = false;
    try {
        failing_pipeline.process(std::span<std::byte>{});
    } catch (const std::runtime_error& error) {
        stage_error_propagated =
            std::string_view{error.what()} == "expected stage failure";
    }

    if (!throwing_stage_called || !stage_error_propagated) {
        return fail("a stage exception was not propagated to the caller");
    }
    if (later_stage_called) {
        return fail("Pipeline ran a stage after an earlier stage failed");
    }

    return 0;
}
