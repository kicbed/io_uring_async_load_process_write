#include "pipeline/pipeline.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace {

class AffineStage final : public asyncdataloader::pipeline::Stage {
public:
    AffineStage(unsigned int multiplier, unsigned int offset)
        : multiplier_(multiplier),
          offset_(offset) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "custom_affine";
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
};

void print_block(
    std::string_view label,
    std::span<const std::byte> block
) {
    std::cout << label << '=';

    for (std::size_t index = 0; index < block.size(); ++index) {
        if (index != 0) {
            std::cout << ',';
        }

        std::cout << std::to_integer<unsigned int>(block[index]);
    }

    std::cout << '\n';
}

}  // namespace

int main() {
    std::array<std::byte, 3> block{
        static_cast<std::byte>(1),
        static_cast<std::byte>(2),
        static_cast<std::byte>(3),
    };

    print_block("input", std::span<const std::byte>{block});

    asyncdataloader::pipeline::Pipeline pipeline;
    auto custom_stage = std::make_unique<AffineStage>(2U, 1U);

    std::cout << "registered_stage=" << custom_stage->name() << '\n';
    pipeline.add_stage(std::move(custom_stage));
    std::cout << "stage_count=" << pipeline.stage_count() << '\n';

    pipeline.process(std::span<std::byte>{block});

    print_block("output", std::span<const std::byte>{block});
    return 0;
}
