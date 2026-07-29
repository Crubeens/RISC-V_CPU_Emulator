#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>

#include "rv/devices/ram.hpp"
#include "rv64/platform/machine.hpp"

namespace {

constexpr std::uint64_t step_limit = 1'000'000ULL;
constexpr std::size_t tohost_offset = 0x10000U;

[[nodiscard]] std::uint32_t read_word(
    const rv::devices::Ram& ram,
    std::size_t offset)
{
    const auto bytes = ram.bytes();
    if (offset > bytes.size() ||
        sizeof(std::uint32_t) > bytes.size() - offset) {
        return 0;
    }
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

[[nodiscard]] bool read_image(
    const char* path,
    std::vector<std::uint8_t>& image)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    image.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    return !input.bad() && !image.empty();
}

void print_commit(const rv64::StepResult& result, std::uint64_t next_pc)
{
    std::cout
        << "RV64TRACE "
        << static_cast<unsigned int>(result.privilege) << ' '
        << std::hex << std::setfill('0')
        << std::setw(16) << result.pc << ' '
        << std::setw(8) << result.instruction << ' '
        << std::setw(16) << next_pc << ' ';
    if (result.register_write.enabled) {
        std::cout
            << 'x' << std::dec
            << static_cast<unsigned int>(result.register_write.index)
            << '=' << std::hex << std::setw(16)
            << result.register_write.value;
    } else if (result.floating_register_write.enabled) {
        std::cout
            << 'f' << std::dec
            << static_cast<unsigned int>(
                   result.floating_register_write.index)
            << '=' << std::hex << std::setw(16)
            << result.floating_register_write.value;
    } else {
        std::cout << '-';
    }
    std::cout << '\n';
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
            << "usage: rv64_architecture_runner "
            << "[--trace] [--reference] <binary>\n";
        return 2;
    }
    std::vector<std::uint8_t> image;
    if (!read_image(image_path, image)) {
        std::cerr << "cannot read RV64 architecture image\n";
        return 3;
    }

    rv64::platform::Machine machine(
        {
            .ram_size = 1024U * 1024U,
            .virtual_disk_size = 512U,
            .enable_framebuffer = false,
        });
    if (machine.load_image(
            image,
            rv64::platform::address_map::dram_base) != rv::BusFault::None) {
        std::cerr << "RV64 architecture image does not fit RAM\n";
        return 4;
    }
    machine.reset({.reset_pc = rv64::platform::address_map::dram_base});
    if (reference_mode) {
        machine.core().set_execution_mode(
            rv64::ExecutionMode::Reference);
    }

    for (std::uint64_t step = 0; step < step_limit; ++step) {
        const rv64::StepResult result = machine.step();
        if (trace_enabled &&
            result.status == rv64::StepStatus::Retired) {
            print_commit(result, machine.core().snapshot().pc);
        }
        const std::uint32_t tohost =
            read_word(machine.ram(), tohost_offset);
        if (tohost != 0U) {
            if (tohost == 1U) {
                return 0;
            }
            std::cerr
                << "RV64 architecture test failed; test="
                << (tohost >> 1U)
                << ", tohost=0x" << std::hex << tohost
                << ", pc=0x" << result.pc << std::dec << '\n';
            return 1;
        }
        if (result.status == rv64::StepStatus::Retired) {
            continue;
        }
        if (result.status == rv64::StepStatus::TrapTaken &&
            (result.instruction == 0x00100073U ||
             result.instruction == 0x00009002U)) {
            return 0;
        }
        if (result.status == rv64::StepStatus::TrapTaken ||
            result.status == rv64::StepStatus::WaitingForInterrupt) {
            continue;
        }
        std::cerr
            << "RV64 architecture guest stopped at 0x"
            << std::hex << result.pc
            << " with status " << std::dec
            << static_cast<unsigned int>(result.status) << '\n';
        return 5;
    }
    std::cerr << "RV64 architecture step limit reached\n";
    return 6;
}
