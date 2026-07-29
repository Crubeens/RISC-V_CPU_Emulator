#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#include "rv/devices/ram.hpp"
#include "rv32/platform/machine.hpp"

namespace {

constexpr std::size_t tohost_offset = 0x10000U;
constexpr std::uint64_t instruction_limit = 1'000'000U;

[[nodiscard]] std::uint32_t read_word(
    const rv::devices::Ram& ram,
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

void print_commit_trace(const rv32::CommitTrace& trace)
{
    if (!trace.valid) {
        return;
    }

    std::cout
        << "RV32TRACE "
        << static_cast<unsigned int>(trace.privilege)
        << ' ' << std::hex << std::setw(8) << std::setfill('0')
        << trace.pc
        << ' ' << std::setw(
                      static_cast<int>(
                          trace.instruction_length) *
                      2)
        << trace.instruction
        << ' ' << std::setw(8) << trace.next_pc;
    if (trace.register_write.enabled) {
        std::cout
            << " x" << std::dec << trace.register_write.index
            << '=' << std::hex << std::setw(8)
            << trace.register_write.value;
    } else {
        std::cout << " -";
    }
    std::cout << std::setfill(' ') << std::dec << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    bool trace_enabled = false;
    bool reference_mode = false;
    const char* image_path = nullptr;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--trace") {
            trace_enabled = true;
        } else if (argument == "--reference") {
            reference_mode = true;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "unknown option: " << argument << '\n';
            return 2;
        } else if (image_path == nullptr) {
            image_path = argv[index];
        } else {
            image_path = nullptr;
            break;
        }
    }
    if (image_path == nullptr) {
        std::cerr
            << "usage: rv32_architecture_runner "
            << "[--trace] [--reference] <raw-binary>\n";
        return 2;
    }

    std::ifstream input(image_path, std::ios::binary);
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
    if (reference_mode) {
        machine.core().set_execution_mode(
            rv32::ExecutionMode::Reference);
    }
    if (machine.ram().load_image(image) != rv32::BusFault::None) {
        std::cerr << "cannot load architecture-test image\n";
        return 5;
    }

    for (std::uint64_t step = 0;
         step < instruction_limit;
         ++step) {
        const auto result = machine.step();
        if (trace_enabled) {
            print_commit_trace(result.commit);
        }
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
