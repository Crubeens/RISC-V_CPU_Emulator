#include <iomanip>
#include <iostream>

#include "rv32/core/core.hpp"
#include "rv32/platform/machine.hpp"

int main()
{
    rv32::platform::Machine machine;
    const auto reset_snapshot = machine.core().snapshot();

    std::cout
        << "RISC-V32 CPU Emulator framework v0.1.0\n"
        << "CPU profile: " << rv32::Core::isa_string() << "\n"
        << "Mapped devices:\n";

    for (const auto& device : machine.device_map()) {
        std::cout
            << "  " << std::left << std::setw(22) << device.name
            << " [0x" << std::right << std::hex << std::setw(8)
            << std::setfill('0') << device.range.base
            << ", 0x" << std::setw(8)
            << device.range.end_exclusive() << ")\n"
            << std::setfill(' ') << std::dec;
    }

    constexpr std::uint32_t nop = 0x00000013U;
    const auto load_fault = machine.bus().dma_write(
        reset_snapshot.pc,
        rv32::AccessWidth::Word,
        nop);
    if (load_fault != rv32::BusFault::None) {
        std::cerr << "Failed to load the framework NOP instruction\n";
        return 1;
    }

    const auto step_result = machine.step();
    const auto snapshot = machine.core().snapshot();
    const bool retired =
        step_result.status == rv32::StepStatus::Retired;

    std::cout
        << "Reset PC: 0x" << std::hex
        << reset_snapshot.pc << "\n"
        << "Current PC: 0x" << snapshot.pc << std::dec << "\n"
        << "Framework step status: "
        << (retired
                ? "one RV32I NOP retired"
                : "unexpected")
        << "\n"
        << "Framework assembly completed successfully.\n";

    return retired ? 0 : 1;
}
