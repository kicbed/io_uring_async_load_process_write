#pragma once

#include "pipeline/stage.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace asyncdataloader::pipeline {

class NoOpStage final : public Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    void process(std::span<std::byte> block) override;
};

// A deterministic, block-boundary-independent preprocessing stage used by the
// end-to-end demo. Every byte is incremented modulo 256, so output correctness
// can be checked in a second bounded streaming pass.
class ByteIncrementStage final : public Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    void process(std::span<std::byte> block) override;
};

class NormalizeStage final : public Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override;

    // Performs per-block min-max normalization into the byte range [0, 255].
    // An empty block is unchanged; a constant block is mapped to all zeroes.
    void process(std::span<std::byte> block) override;
};

class ChecksumStage final : public Stage {
public:
    [[nodiscard]] std::string_view name() const noexcept override;

    // Computes FNV-1a for the current block without modifying it. The stored
    // value is replaced on every call and is not synchronized for concurrency.
    void process(std::span<std::byte> block) override;

    [[nodiscard]] std::uint64_t last_checksum() const noexcept;

private:
    static constexpr std::uint64_t kOffsetBasis{14695981039346656037ULL};
    static constexpr std::uint64_t kPrime{1099511628211ULL};

    std::uint64_t last_checksum_{kOffsetBasis};
};

}  // namespace asyncdataloader::pipeline
