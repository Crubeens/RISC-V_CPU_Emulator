#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <string_view>
#include <vector>

#include "rv64/platform/machine.hpp"

namespace {

constexpr std::uint64_t step_limit = 1'000'000ULL;

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
    } else {
        std::cout << '-';
    }
    std::cout << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3 || std::string_view(argv[1]) != "--trace") {
        std::cerr << "usage: rv64_architecture_runner --trace <binary>\n";
        return 2;
    }
    std::vector<std::uint8_t> image;
    if (!read_image(argv[2], image)) {
        std::cerr << "cannot read RV64 architecture image\n";
        return 3;
    }

    rv64::platform::Machine machine({.enable_framebuffer = false});
    if (machine.load_image(
            image,
            rv64::platform::address_map::dram_base) != rv::BusFault::None) {
        std::cerr << "RV64 architecture image does not fit RAM\n";
        return 4;
    }
    machine.reset({.reset_pc = rv64::platform::address_map::dram_base});

    for (std::uint64_t step = 0; step < step_limit; ++step) {
        const rv64::StepResult result = machine.step();
        if (result.status == rv64::StepStatus::Retired) {
            print_commit(result, machine.core().snapshot().pc);
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
