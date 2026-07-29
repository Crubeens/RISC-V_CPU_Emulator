#include "rv64/app/cli.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <vector>

#include "rv64/platform/machine.hpp"

namespace rv64::app {

namespace {

constexpr std::uint64_t default_step_limit = 1'000'000ULL;

[[nodiscard]] bool read_image(
    const char* path,
    std::vector<std::uint8_t>& image)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "Cannot open RV64 image: " << path << '\n';
        return false;
    }
    image.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (input.bad() || image.empty()) {
        std::cerr << "Cannot read RV64 image: " << path << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool parse_limit(
    std::string_view text,
    std::uint64_t& limit)
{
    const auto result = std::from_chars(
        text.data(),
        text.data() + text.size(),
        limit);
    return result.ec == std::errc{} &&
           result.ptr == text.data() + text.size() &&
           limit != 0U;
}

int run_smoke()
{
    platform::Machine machine({
        .ram_size = 1ULL * 1024ULL * 1024ULL,
        .virtual_disk_size = 512ULL,
        .enable_framebuffer = false,
    });
    constexpr std::uint32_t nop = 0x00000013U;
    const std::uint8_t image[]{
        static_cast<std::uint8_t>(nop),
        static_cast<std::uint8_t>(nop >> 8U),
        static_cast<std::uint8_t>(nop >> 16U),
        static_cast<std::uint8_t>(nop >> 24U),
    };
    if (machine.load_image(
            image,
            platform::address_map::dram_base) != rv::BusFault::None) {
        std::cerr << "Failed to load the RV64 smoke instruction\n";
        return 1;
    }
    machine.reset({
        .reset_pc = platform::address_map::dram_base,
    });
    const auto result = machine.step();
    const auto state = machine.core().snapshot();
    if (result.status != StepStatus::Retired ||
        state.pc != platform::address_map::dram_base + 4U) {
        std::cerr << "RV64 framework smoke test failed\n";
        return 1;
    }
    std::cout
        << "RV64 CPU selected\n"
        << "One RV64I NOP retired; pc=0x"
        << std::hex << state.pc << std::dec << '\n';
    return 0;
}

int run_raw(int argc, char** argv)
{
    if (argc < 3 || argc > 4) {
        std::cerr
            << "usage: rv32_emulator --cpu rv64 --run-raw "
            << "<image.bin> [max-steps]\n";
        return 2;
    }
    std::uint64_t limit = default_step_limit;
    if (argc == 4 &&
        !parse_limit(argv[3], limit)) {
        std::cerr << "Invalid RV64 step limit\n";
        return 2;
    }

    std::vector<std::uint8_t> image;
    if (!read_image(argv[2], image)) {
        return 3;
    }
    platform::Machine machine({
        .enable_framebuffer = false,
    });
    if (machine.load_image(
            image,
            platform::address_map::dram_base) != rv::BusFault::None) {
        std::cerr << "RV64 image does not fit guest RAM\n";
        return 4;
    }
    machine.reset({
        .reset_pc = platform::address_map::dram_base,
    });

    for (std::uint64_t step = 0; step < limit; ++step) {
        const StepResult result = machine.step();
        if (result.status == StepStatus::Retired) {
            continue;
        }
        if (result.status == StepStatus::TrapTaken &&
            (result.instruction == 0x00100073U ||
             result.instruction == 0x00009002U)) {
            const auto state = machine.core().snapshot();
            std::cout
                << "RV64 bare-metal program completed after "
                << state.instructions_retired
                << " retired instructions\n";
            return 0;
        }
        if (result.status == StepStatus::TrapTaken ||
            result.status == StepStatus::WaitingForInterrupt) {
            continue;
        }
        std::cerr
            << "RV64 guest stopped: status="
            << static_cast<unsigned int>(result.status)
            << ", pc=0x" << std::hex << result.pc
            << ", instruction=0x" << result.instruction
            << std::dec << '\n';
        return 5;
    }
    std::cerr << "RV64 machine-step limit reached\n";
    return 6;
}

int load_images(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr
            << "usage: rv32_emulator --cpu rv64 --load-images "
            << "<firmware.bin> <kernel> <board.dtb>\n";
        return 2;
    }
    std::vector<std::uint8_t> firmware;
    std::vector<std::uint8_t> kernel;
    std::vector<std::uint8_t> device_tree;
    if (!read_image(argv[2], firmware) ||
        !read_image(argv[3], kernel) ||
        !read_image(argv[4], device_tree)) {
        return 3;
    }

    platform::Machine machine;
    const auto result = machine.load_boot({
        .firmware = firmware,
        .kernel = kernel,
        .device_tree = device_tree,
    });
    if (!result.ok()) {
        std::cerr
            << "Cannot prepare RV64 boot: "
            << platform::boot_error_message(result.error)
            << '\n';
        return 4;
    }
    const auto state = machine.core().snapshot();
    std::cout
        << "RV64 boot images loaded:\n"
        << "  firmware=0x" << std::hex
        << result.layout.firmware_address
        << "\n  kernel=0x" << result.layout.kernel_address
        << "\n  dtb=0x" << result.layout.device_tree_address
        << "\n  a0=0x" << state.registers[10]
        << ", a1=0x" << state.registers[11]
        << std::dec << '\n';
    return 0;
}

} // namespace

int run_cli(int argc, char** argv)
{
    if (argc == 1) {
        return run_smoke();
    }
    const std::string_view command = argv[1];
    if (command == "--run-raw") {
        return run_raw(argc, argv);
    }
    if (command == "--load-images") {
        return load_images(argc, argv);
    }
    if (command == "--boot" || command == "--boot-disk" ||
        command == "--gui") {
        std::cerr
            << "RV64 privileged boot is scheduled for RV64-M5; "
            << "M1 supports --run-raw and --load-images\n";
        return 2;
    }
    std::cerr
        << "usage:\n"
        << "  rv32_emulator --cpu rv64\n"
        << "  rv32_emulator --cpu rv64 --run-raw "
        << "<image.bin> [max-steps]\n"
        << "  rv32_emulator --cpu rv64 --load-images "
        << "<firmware.bin> <kernel> <board.dtb>\n";
    return 2;
}

} // namespace rv64::app
