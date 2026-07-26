#include "rv32/core/frontend.hpp"

#include "rv32/core/mmu.hpp"

namespace rv32 {

FrontendResult fetch_decode(
    CpuBus& bus,
    std::uint32_t pc,
    const CpuSnapshot* state)
{
    // RV32I without the C extension uses IALIGN=32: the PC must be aligned
    // to a four-byte instruction boundary.
    if ((pc & 0x3U) != 0U) {
        return {
            .status = FrontendStatus::InstructionAddressMisaligned,
            .pc = pc,
            .instruction = 0,
            .decoded = {},
            .bus_fault = BusFault::Misaligned,
            .trap_value = pc,
        };
    }

    PhysAddr physical_address = static_cast<PhysAddr>(pc);
    if (state != nullptr) {
        const TranslationResult translation =
            translate_address(
                bus,
                *state,
                pc,
                MemoryAccessType::InstructionFetch);
        if (!translation.ready()) {
            return {
                .status =
                    translation.status == TranslationStatus::PageFault
                        ? FrontendStatus::InstructionPageFault
                        : FrontendStatus::InstructionAccessFault,
                .pc = pc,
                .instruction = 0,
                .decoded = {},
                .bus_fault = translation.bus_fault,
                .trap_value = pc,
            };
        }
        physical_address = translation.physical_address;
    }

    const ReadResult read_result = bus.read(
        physical_address,
        AccessWidth::Word,
        AccessKind::InstructionFetch);

    if (!read_result.ok()) {
        const FrontendStatus status =
            read_result.fault == BusFault::Misaligned
                ? FrontendStatus::InstructionAddressMisaligned
                : FrontendStatus::InstructionAccessFault;

        return {
            .status = status,
            .pc = pc,
            .instruction = 0,
            .decoded = {},
            .bus_fault = read_result.fault,
            .trap_value = pc,
        };
    }

    const std::uint32_t instruction =
        static_cast<std::uint32_t>(read_result.value);
    const DecodedInstruction decoded =
        decode_instruction(instruction);

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
