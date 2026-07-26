#pragma once

#include <cstdint>

#include "rv32/core/bus.hpp"
#include "rv32/core/decode.hpp"
#include "rv32/core/types.hpp"

namespace rv32 {

enum class FrontendStatus : std::uint8_t {
    Ready,
    InstructionAddressMisaligned,
    InstructionAccessFault,
    InstructionPageFault,
    IllegalInstruction,
};

struct FrontendResult {
    FrontendStatus status{FrontendStatus::InstructionAccessFault};
    std::uint32_t pc{};
    std::uint32_t instruction{};
    DecodedInstruction decoded{};
    BusFault bus_fault{BusFault::None};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == FrontendStatus::Ready;
    }
};

[[nodiscard]] FrontendResult fetch_decode(
    CpuBus& bus,
    std::uint32_t pc,
    const CpuSnapshot* state = nullptr);

} // namespace rv32
