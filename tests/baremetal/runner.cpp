#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "rv32/core/trap.hpp"
#include "rv32/devices/ram.hpp"
#include "rv32/platform/machine.hpp"

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: rv32_baremetal_runner <raw-binary>\n";
        return 2;
    }

    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open raw binary\n";
        return 3;
    }

    const std::vector<std::uint8_t> image{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    if (image.empty()) {
        std::cerr << "bare-metal image is empty\n";
        return 4;
    }

    rv32::platform::MachineConfig config;
    config.ram_size = 1024U * 1024U;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;
    rv32::platform::Machine machine(config);

    const auto load_fault = machine.ram().load_image(image);
    if (load_fault != rv32::BusFault::None) {
        std::cerr << "cannot load bare-metal image\n";
        return 5;
    }

    constexpr std::uint64_t instruction_limit = 10'000U;
    for (std::uint64_t step = 0; step < instruction_limit; ++step) {
        const auto result = machine.step();
        const auto state = machine.core().snapshot();
        const bool breakpoint_trap =
            result.status == rv32::StepStatus::TrapTaken &&
            state.machine_csrs.mcause ==
                static_cast<std::uint32_t>(
                    rv32::ExceptionCause::Breakpoint) &&
            state.machine_csrs.mepc == result.pc;
        if (breakpoint_trap) {
            if (state.registers[10] != 0U) {
                std::cerr
                    << "bare-metal program failed; a0="
                    << state.registers[10] << '\n';
                return 1;
            }

            std::cout
                << "bare-metal program passed; retired="
                << state.instructions_retired << '\n';
            return 0;
        }

        if (result.status != rv32::StepStatus::Retired) {
            std::cerr
                << "unexpected CPU status at pc=0x"
                << std::hex << result.pc << '\n';
            return 6;
        }
    }

    std::cerr << "bare-metal instruction limit reached\n";
    return 7;
}
