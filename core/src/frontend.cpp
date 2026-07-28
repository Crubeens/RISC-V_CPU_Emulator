#include "rv32/core/frontend.hpp"

#include "rv32/core/mmu.hpp"

namespace rv32 {

namespace {

struct HalfwordFetchResult {
    FrontendStatus status{FrontendStatus::InstructionAccessFault};
    std::uint16_t value{};
    BusFault bus_fault{BusFault::None};
    std::uint32_t trap_value{};

    [[nodiscard]] constexpr bool ready() const noexcept
    {
        return status == FrontendStatus::Ready;
    }
};

[[nodiscard]] HalfwordFetchResult fetch_halfword(
    CpuBus& bus,
    std::uint32_t virtual_address,
    const CpuSnapshot* state)
{
    PhysAddr physical_address =
        static_cast<PhysAddr>(virtual_address);
    if (state != nullptr) {
        const TranslationResult translation =
            translate_address(
                bus,
                *state,
                virtual_address,
                MemoryAccessType::InstructionFetch);
        if (!translation.ready()) {
            return {
                .status =
                    translation.status == TranslationStatus::PageFault
                        ? FrontendStatus::InstructionPageFault
                        : FrontendStatus::InstructionAccessFault,
                .value = 0,
                .bus_fault = translation.bus_fault,
                .trap_value = virtual_address,
            };
        }
        physical_address = translation.physical_address;
    }

    const ReadResult read_result = bus.read(
        physical_address,
        AccessWidth::HalfWord,
        AccessKind::InstructionFetch);
    if (!read_result.ok()) {
        return {
            .status =
                read_result.fault == BusFault::Misaligned
                    ? FrontendStatus::InstructionAddressMisaligned
                    : FrontendStatus::InstructionAccessFault,
            .value = 0,
            .bus_fault = read_result.fault,
            .trap_value = virtual_address,
        };
    }

    return {
        .status = FrontendStatus::Ready,
        .value = static_cast<std::uint16_t>(read_result.value),
        .bus_fault = BusFault::None,
        .trap_value = 0,
    };
}

[[nodiscard]] FrontendResult fetch_failure(
    std::uint32_t pc,
    std::uint32_t partial_instruction,
    const HalfwordFetchResult& failure)
{
    return {
        .status = failure.status,
        .pc = pc,
        .instruction = partial_instruction,
        .decoded = {},
        .bus_fault = failure.bus_fault,
        .trap_value = failure.trap_value,
    };
}

} // namespace

FrontendResult fetch_decode(
    CpuBus& bus,
    std::uint32_t pc,
    const CpuSnapshot* state)
{
    // RV32C uses IALIGN=16. Both compressed and 32-bit instructions may
    // begin at any two-byte boundary.
    if ((pc & 0x1U) != 0U) {
        return {
            .status = FrontendStatus::InstructionAddressMisaligned,
            .pc = pc,
            .instruction = 0,
            .decoded = {},
            .bus_fault = BusFault::Misaligned,
            .trap_value = pc,
        };
    }

    const HalfwordFetchResult first =
        fetch_halfword(bus, pc, state);
    if (!first.ready()) {
        return fetch_failure(pc, 0, first);
    }

    std::uint32_t instruction = first.value;
    DecodedInstruction decoded;
    if ((first.value & 0x3U) != 0x3U) {
        decoded = decode_compressed_instruction(first.value);
    } else {
        const std::uint32_t second_address = pc + 2U;
        const HalfwordFetchResult second =
            fetch_halfword(bus, second_address, state);
        if (!second.ready()) {
            return fetch_failure(pc, instruction, second);
        }
        instruction |=
            static_cast<std::uint32_t>(second.value) << 16U;
        decoded = decode_instruction(instruction);
    }

    if (!decoded.valid()) {
        return {
            .status = FrontendStatus::IllegalInstruction,
            .pc = pc,
            .instruction = instruction,
            .decoded = decoded,
            .bus_fault = BusFault::None,
            .trap_value = instruction,
        };
    }

    return {
        .status = FrontendStatus::Ready,
        .pc = pc,
        .instruction = instruction,
        .decoded = decoded,
        .bus_fault = BusFault::None,
        .trap_value = 0,
    };
}

} // namespace rv32
