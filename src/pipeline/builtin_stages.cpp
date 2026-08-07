#include "pipeline/builtin_stages.h"

#include <cstddef>

namespace asyncdataloader::pipeline {

std::string_view NoOpStage::name() const noexcept {
    return "noop";
}

void NoOpStage::process(std::span<std::byte>) {}

std::string_view ByteIncrementStage::name() const noexcept {
    return "byte_increment";
}

void ByteIncrementStage::process(std::span<std::byte> block) {
    for (std::byte& value : block) {
        const unsigned int numeric = std::to_integer<unsigned int>(value);
        value = std::byte{static_cast<unsigned char>(numeric + 1U)};
    }
}

std::string_view NormalizeStage::name() const noexcept {
    return "normalize";
}

void NormalizeStage::process(std::span<std::byte> block) {
    if (block.empty()) {
        return;
    }

    unsigned int minimum = std::to_integer<unsigned int>(block.front());
    unsigned int maximum = minimum;

    for (const std::byte value : block) {
        const unsigned int numeric = std::to_integer<unsigned int>(value);

        if (numeric < minimum) {
            minimum = numeric;
        }
        if (numeric > maximum) {
            maximum = numeric;
        }
    }

    if (minimum == maximum) {
        for (std::byte& value : block) {
            value = static_cast<std::byte>(0);
        }
        return;
    }

    const unsigned int range = maximum - minimum;

    for (std::byte& value : block) {
        const unsigned int numeric = std::to_integer<unsigned int>(value);
        const unsigned int normalized =
            ((numeric - minimum) * 255U) / range;
        value = static_cast<std::byte>(normalized);
    }
}

std::string_view ChecksumStage::name() const noexcept {
    return "checksum";
}

void ChecksumStage::process(std::span<std::byte> block) {
    std::uint64_t checksum = kOffsetBasis;

    for (const std::byte value : block) {
        checksum ^= std::to_integer<unsigned int>(value);
        checksum *= kPrime;
    }

    last_checksum_ = checksum;
}

std::uint64_t ChecksumStage::last_checksum() const noexcept {
    return last_checksum_;
}

}  // namespace asyncdataloader::pipeline
