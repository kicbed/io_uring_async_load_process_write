#include "pipeline/builtin_stages.h"
#include "pipeline/pipeline.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>

namespace {

int fail(std::string_view message) {
    std::cerr << "stage7 built-in stages test failed: " << message << '\n';
    return 1;
}

[[nodiscard]] unsigned int byte_value(std::byte value) {
    return std::to_integer<unsigned int>(value);
}

}  // namespace

int main() {
    using asyncdataloader::pipeline::ChecksumStage;
    using asyncdataloader::pipeline::NoOpStage;
    using asyncdataloader::pipeline::NormalizeStage;
    using asyncdataloader::pipeline::Pipeline;

    NoOpStage noop;
    NormalizeStage normalize;
    ChecksumStage checksum;

    if (noop.name() != "noop" ||
        normalize.name() != "normalize" ||
        checksum.name() != "checksum") {
        return fail("a built-in stage returned the wrong name");
    }

    std::array<std::byte, 3> noop_block{
        static_cast<std::byte>(10),
        static_cast<std::byte>(20),
        static_cast<std::byte>(30),
    };
    const auto noop_original = noop_block;
    noop.process(std::span<std::byte>{noop_block});

    if (noop_block != noop_original) {
        return fail("NoOpStage modified its input");
    }

    std::array<std::byte, 3> normalize_block{
        static_cast<std::byte>(10),
        static_cast<std::byte>(20),
        static_cast<std::byte>(30),
    };
    normalize.process(std::span<std::byte>{normalize_block});

    if (byte_value(normalize_block[0]) != 0 ||
        byte_value(normalize_block[1]) != 127 ||
        byte_value(normalize_block[2]) != 255) {
        return fail("NormalizeStage produced the wrong min-max mapping");
    }

    std::array<std::byte, 3> constant_block{
        static_cast<std::byte>(42),
        static_cast<std::byte>(42),
        static_cast<std::byte>(42),
    };
    normalize.process(std::span<std::byte>{constant_block});

    if (byte_value(constant_block[0]) != 0 ||
        byte_value(constant_block[1]) != 0 ||
        byte_value(constant_block[2]) != 0) {
        return fail("NormalizeStage did not zero a constant block");
    }

    normalize.process(std::span<std::byte>{});

    std::array<std::byte, 5> hello{
        static_cast<std::byte>('h'),
        static_cast<std::byte>('e'),
        static_cast<std::byte>('l'),
        static_cast<std::byte>('l'),
        static_cast<std::byte>('o'),
    };
    const auto hello_original = hello;
    checksum.process(std::span<std::byte>{hello});

    if (checksum.last_checksum() != 0xA430D84680AABD0BULL) {
        return fail("ChecksumStage produced the wrong FNV-1a value");
    }
    if (hello != hello_original) {
        return fail("ChecksumStage modified its input");
    }

    Pipeline pipeline;
    pipeline.add_stage(std::make_unique<NoOpStage>());
    pipeline.add_stage(std::make_unique<NormalizeStage>());

    auto final_checksum = std::make_unique<ChecksumStage>();
    ChecksumStage* const checksum_observer = final_checksum.get();
    pipeline.add_stage(std::move(final_checksum));

    std::array<std::byte, 4> pipeline_block{
        static_cast<std::byte>(10),
        static_cast<std::byte>(20),
        static_cast<std::byte>(30),
        static_cast<std::byte>(99),
    };
    pipeline.process(std::span<std::byte>{pipeline_block}.first(3));

    if (pipeline.stage_count() != 3) {
        return fail("Pipeline did not retain all built-in stages");
    }
    if (byte_value(pipeline_block[0]) != 0 ||
        byte_value(pipeline_block[1]) != 127 ||
        byte_value(pipeline_block[2]) != 255) {
        return fail("built-in stages produced the wrong pipeline output");
    }
    if (byte_value(pipeline_block[3]) != 99) {
        return fail("built-in stages modified bytes outside the borrowed span");
    }
    if (checksum_observer->last_checksum() != 0xD87637186B5800ADULL) {
        return fail("ChecksumStage did not observe the normalized block");
    }

    return 0;
}
