#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <vector>

#include "rv32/devices/ram.hpp"
#include "rv32/platform/machine.hpp"

namespace {

constexpr std::size_t tohost_offset = 0x10000U;
constexpr std::uint64_t instruction_limit = 1'000'000U;

[[nodiscard]] std::uint32_t read_word(
    const rv32::devices::Ram& ram,
    std::size_t offset)
{
    const auto bytes = ram.bytes();
    if (offset > bytes.size() ||
        sizeof(std::uint32_t) > bytes.size() - offset) {
        return 0;
    }
    return
        static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] bool can_continue(rv32::StepStatus status) noexcept
{
    return status == rv32::StepStatus::Retired ||
           status == rv32::StepStatus::TrapTaken ||
           status == rv32::StepStatus::WaitingForInterrupt;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr
            << "usage: rv32_architecture_runner <raw-binary>\n";
        return 2;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open architecture-test image\n";
        return 3;
    }
    const std::vector<std::uint8_t> image{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (image.empty()) {
        std::cerr << "architecture-test image is empty\n";
        return 4;
    }

    rv32::platform::MachineConfig config;
    config.ram_size = 1024U * 1024U;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;
    rv32::platform::Machine machine(config);
    if (machine.ram().load_image(image) != rv32::BusFault::None) {
        std::cerr << "cannot load architecture-test image\n";
        return 5;
    }

    for (std::uint64_t step = 0;
         step < instruction_limit;
         ++step) {
        const auto result = machine.step();
        const std::uint32_t tohost =
            read_word(machine.ram(), tohost_offset);
        if (tohost != 0U) {
            if (tohost == 1U) {
                std::cout
                    << "riscv-tests case passed; retired="
                    << machine.core().snapshot().instructions_retired
                    << '\n';
                return 0;
            }

            std::cerr
                << "riscv-tests case failed; test="
                << (tohost >> 1U)
                << ", tohost=0x" << std::hex << tohost
                << ", pc=0x" << result.pc << std::dec << '\n';
            return 1;
        }

        if (!can_continue(result.status)) {
            std::cerr
                << "architecture test stopped unexpectedly; status="
                << static_cast<unsigned int>(result.status)
                << ", pc=0x" << std::hex << result.pc
                << ", instruction=0x" << result.instruction
                << std::dec << '\n';
            return 6;
        }
    }

    const auto state = machine.core().snapshot();
    std::cerr
        << "architecture-test instruction limit reached; pc=0x"
        << std::hex << state.pc
        << ", mcause=0x" << state.machine_csrs.mcause
        << ", mtval=0x" << state.machine_csrs.mtval
        << std::dec << '\n';
    return 7;
}
