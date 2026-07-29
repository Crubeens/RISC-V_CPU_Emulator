#include "rv64/app/cli.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <conio.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#include "rv/devices/syscon.hpp"
#include "rv/devices/uart16550.hpp"
#include "rv/devices/virtio_block.hpp"
#include "rv64/platform/machine.hpp"

#if defined(RV_ENABLE_SDL)
#include "rv/app/sdl_frontend.hpp"
#endif

namespace rv64::app {

namespace {

constexpr std::uint64_t default_step_limit = 1'000'000ULL;
constexpr std::uint64_t default_boot_step_limit = 20'000'000ULL;
constexpr std::uint64_t unlimited_step_limit =
    std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t console_poll_interval = 1024ULL;

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

[[nodiscard]] bool read_disk_image(
    const char* path,
    std::vector<std::uint8_t>& image)
{
    if (!read_image(path, image)) {
        return false;
    }
    const auto sector_size = static_cast<std::size_t>(
        rv::devices::VirtioBlock::sector_size);
    if ((image.size() % sector_size) != 0U) {
        std::cerr
            << "RV64 virtual disk size must be a multiple of "
            << sector_size << " bytes: " << path << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool load_disk_image(
    platform::Machine& machine,
    const std::vector<std::uint8_t>& image)
{
    auto target = machine.virtio_block().disk_image();
    if (target.size() != image.size()) {
        std::cerr
            << "RV64 virtual disk capacity does not match the image size\n";
        return false;
    }
    std::copy(image.begin(), image.end(), target.begin());
    machine.virtio_block().clear_dirty();
    return true;
}

[[nodiscard]] bool write_disk_image(
    const char* path,
    std::span<const std::uint8_t> image)
{
    if (image.size() >
        static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max())) {
        std::cerr
            << "RV64 virtual disk image is too large to write: "
            << path << '\n';
        return false;
    }

    std::fstream output(
        path,
        std::ios::binary | std::ios::in | std::ios::out);
    if (!output) {
        std::cerr
            << "Cannot open RV64 virtual disk for writeback: "
            << path << '\n';
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(image.data()),
        static_cast<std::streamsize>(image.size()));
    output.flush();
    if (!output) {
        std::cerr
            << "Cannot write RV64 virtual disk: "
            << path << '\n';
        return false;
    }
    return true;
}

[[nodiscard]] bool write_back_disk(
    platform::Machine& machine,
    const char* path)
{
    auto& disk = machine.virtio_block();
    if (path == nullptr || !disk.dirty()) {
        return true;
    }
    if (!write_disk_image(path, disk.disk_image())) {
        return false;
    }
    disk.clear_dirty();
    std::cout
        << "RV64 virtual disk writeback completed: "
        << path << '\n';
    return true;
}

void forward_console_input(platform::Machine& machine)
{
    std::array<char, 256> input{};
    std::size_t size = 0;

#if defined(_WIN32)
    while (size < input.size() && _kbhit() != 0) {
        const int character = _getch();
        if (character == 0 || character == 0xE0) {
            if (_kbhit() != 0) {
                static_cast<void>(_getch());
            }
            continue;
        }
        input[size] = static_cast<char>(character);
        ++size;
    }
#else
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(STDIN_FILENO, &descriptors);
    timeval timeout{};
    const int ready = select(
        STDIN_FILENO + 1,
        &descriptors,
        nullptr,
        nullptr,
        &timeout);
    if (ready > 0 && FD_ISSET(STDIN_FILENO, &descriptors)) {
        const auto count =
            read(STDIN_FILENO, input.data(), input.size());
        if (count > 0) {
            size = static_cast<std::size_t>(count);
        }
    }
#endif

    if (size != 0U) {
        machine.uart().inject_received(
            std::string_view(input.data(), size));
    }
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
            << "usage: riscv_emulator --cpu rv64 --run-raw "
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
            << "usage: riscv_emulator --cpu rv64 --load-images "
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

int run_boot(
    int argc,
    char** argv,
    bool use_virtual_disk,
    bool use_gui)
{
    const int required_arguments = use_virtual_disk ? 6 : 5;
    if (argc != required_arguments &&
        argc != required_arguments + 1) {
        std::cerr
            << "usage: riscv_emulator --cpu rv64 "
            << (use_virtual_disk ? "--boot-disk " : "--boot ")
            << "<opensbi.bin> <kernel> <board.dtb> ";
        if (use_virtual_disk) {
            std::cerr << "<disk-image> ";
        }
        std::cerr << "[max-steps]\n";
        return 2;
    }

    std::uint64_t step_limit =
        use_virtual_disk
            ? unlimited_step_limit
            : default_boot_step_limit;
    const int step_limit_index = use_virtual_disk ? 6 : 5;
    if (argc == required_arguments + 1 &&
        !parse_limit(argv[step_limit_index], step_limit)) {
        std::cerr << "Invalid RV64 boot step limit\n";
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

    const char* virtual_disk_path = nullptr;
    std::vector<std::uint8_t> virtual_disk;
    platform::MachineConfig machine_config;
    if (use_virtual_disk) {
        virtual_disk_path = argv[5];
        if (!read_disk_image(virtual_disk_path, virtual_disk)) {
            return 3;
        }
        machine_config.virtual_disk_size =
            static_cast<std::uint64_t>(virtual_disk.size());
    }

    platform::Machine machine(machine_config);
    if (use_virtual_disk &&
        !load_disk_image(machine, virtual_disk)) {
        return 3;
    }

    const auto boot = machine.load_boot({
        .firmware = firmware,
        .kernel = kernel,
        .device_tree = device_tree,
    });
    if (!boot.ok()) {
        std::cerr
            << "Cannot prepare RV64 boot: "
            << platform::boot_error_message(boot.error)
            << '\n';
        return 4;
    }

    const auto initial = machine.core().snapshot();
    std::cout
        << "RV64 boot images loaded:\n"
        << "  OpenSBI     0x" << std::hex
        << boot.layout.firmware_address
        << " (" << std::dec << firmware.size() << " bytes)"
        << "\n  payload     0x" << std::hex
        << boot.layout.kernel_address
        << " (" << std::dec << kernel.size() << " bytes)"
        << "\n  DTB         0x" << std::hex
        << boot.layout.device_tree_address
        << " (" << std::dec << device_tree.size() << " bytes)"
        << "\nCPU ready: pc=0x" << std::hex << initial.pc
        << ", a0=0x" << initial.registers[10]
        << ", a1=0x" << initial.registers[11]
        << std::dec
        << '\n';
    if (use_virtual_disk) {
        std::cout
            << "RV64 virtual disk loaded: "
            << virtual_disk_path
            << " (" << virtual_disk.size()
            << " bytes, read-write)\n";
    }
    std::cout << "Starting RV64 guest; machine-step limit: ";
    if (step_limit == unlimited_step_limit) {
        std::cout << "unlimited\n";
    } else {
        std::cout << step_limit << '\n';
    }
    std::cout
        << "Host terminal input is connected to RV64 guest UART.\n\n";

    const auto finish =
        [&](int result) {
            return write_back_disk(machine, virtual_disk_path)
                       ? result
                       : 7;
        };

#if defined(RV_ENABLE_SDL)
    std::unique_ptr<rv::app::SdlFrontend> gui;
    if (use_gui) {
        gui = std::make_unique<rv::app::SdlFrontend>();
        if (!gui->ready()) {
            std::cerr
                << "Cannot start RV64 SDL graphical frontend: "
                << gui->error() << '\n';
            return finish(8);
        }
        gui->present(machine.framebuffer());
        std::cout
            << "RV64 SDL window enabled: F1=UART terminal, "
            << "F2=framebuffer, drag=select, "
            << "right-click=copy/paste, "
            << "Ctrl+Shift+C/V=copy/paste.\n";
    }
#else
    if (use_gui) {
        std::cerr
            << "This build does not include SDL graphical support.\n";
        return finish(8);
    }
#endif

    for (std::uint64_t step = 0; step < step_limit; ++step) {
        if ((step % console_poll_interval) == 0U) {
            forward_console_input(machine);
#if defined(RV_ENABLE_SDL)
            if (gui != nullptr && gui->active()) {
                const auto input = gui->poll_input();
                if (!input.empty()) {
                    machine.uart().inject_received(input);
                }
                gui->present(machine.framebuffer());
            }
#endif
        }
        const StepResult result = machine.step();
        const std::string output = machine.uart().take_transmitted();
        if (!output.empty()) {
            std::cout << output << std::flush;
        }
#if defined(RV_ENABLE_SDL)
        if (gui != nullptr && !output.empty()) {
            gui->append_uart(output);
        }
#endif

        const auto action = machine.syscon().requested_action();
        if (action != rv::devices::SystemAction::None) {
            const char* action_name =
                action == rv::devices::SystemAction::PowerOff
                    ? "power off"
                    : "reboot";
            std::cout
                << "\nRV64 guest requested " << action_name
                << " after " << step + 1U
                << " machine steps.\n";
            return finish(0);
        }

        if (result.status != StepStatus::Retired &&
            result.status != StepStatus::TrapTaken &&
            result.status != StepStatus::WaitingForInterrupt) {
            std::cerr
                << "\nRV64 guest stopped: status="
                << static_cast<unsigned int>(result.status)
                << ", pc=0x" << std::hex << result.pc
                << ", instruction=0x" << result.instruction
                << std::dec << '\n';
            return finish(5);
        }
    }

    const auto state = machine.core().snapshot();
    std::cerr
        << "\nRV64 boot step limit reached; retired="
        << state.instructions_retired
        << ", pc=0x" << std::hex << state.pc
        << std::dec << '\n';
    return finish(6);
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
    if (command == "--boot") {
        return run_boot(argc, argv, false, false);
    }
    if (command == "--boot-disk") {
        return run_boot(argc, argv, true, false);
    }
    if (command == "--gui") {
        if (argc >= 3 &&
            std::string_view(argv[2]) == "--boot") {
            return run_boot(argc - 1, argv + 1, false, true);
        }
        if (argc >= 3 &&
            std::string_view(argv[2]) == "--boot-disk") {
            return run_boot(argc - 1, argv + 1, true, true);
        }
        std::cerr
            << "RV64 --gui must be followed by "
            << "--boot or --boot-disk\n";
        return 2;
    }
    std::cerr
        << "usage:\n"
        << "  riscv_emulator --cpu rv64\n"
        << "  riscv_emulator --cpu rv64 --run-raw "
        << "<image.bin> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 --load-images "
        << "<firmware.bin> <kernel> <board.dtb>\n"
        << "  riscv_emulator --cpu rv64 --boot "
        << "<opensbi.bin> <kernel> <board.dtb> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 --boot-disk "
        << "<opensbi.bin> <kernel> <board.dtb> "
        << "<disk-image> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 --gui --boot "
        << "<opensbi.bin> <kernel> <board.dtb> [max-steps]\n"
        << "  riscv_emulator --cpu rv64 --gui --boot-disk "
        << "<opensbi.bin> <kernel> <board.dtb> "
        << "<disk-image> [max-steps]\n";
    return 2;
}

} // namespace rv64::app
